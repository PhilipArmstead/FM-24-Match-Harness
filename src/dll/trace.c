// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "trace.h"

#include "game.h"
#include "log.h"
#include "stream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>


enum {
	TRACE_DEPTH = 16, // stack frames kept per write
	WRITE_SLOT = 5,   // the stream vtable's write method
	VTABLE_SLOTS = 6,
	PATH_BUCKETS = 4096,  // power of two
	CHAIN_CAPACITY = 256, // TRACE_DEPTH RVAs of at most 8 hex digits plus separators
};

typedef int64_t (*StreamWriteFn)(void *self, const void *bytes, int64_t length);

typedef struct PathEntry {
	uint32_t rvas[TRACE_DEPTH - 1];
	uint32_t index;
	size_t count;
} PathEntry;

// State of the trace in progress (serialising is single-threaded and not re-entrant).
static Serialised *active;
static StreamWriteFn originalWrite;
static DataBuffer *outputBuffer;
static PathEntry pathBuckets[PATH_BUCKETS];
static bool traceFailed;

static uint32_t hashChain(const uint32_t *rvas, size_t count);
static bool appendPath(Serialised *serialised, const char *chain);
static uint32_t internPath(const uint32_t *rvas, size_t count);
static uint32_t pathOf(void *const *frames, int count);
static int64_t tracedWrite(void *self, const void *bytes, int64_t length);
static bool recordsTileBytes(const Serialised *serialised);

bool trace_appendRecord(Serialised *serialised, TraceRecord record) {
	if (serialised->recordCount == serialised->recordCapacity) {
		size_t capacity = serialised->recordCapacity ? serialised->recordCapacity * 2 : 65536;
		TraceRecord *records = realloc(serialised->records, capacity * sizeof *records);
		if (!records) {
			return false;
		}
		serialised->records = records;
		serialised->recordCapacity = capacity;
	}
	serialised->records[serialised->recordCount++] = record;
	return true;
}

bool trace_serialize(uint8_t *match, Serialised *out, bool traced) {
	if (!outputBuffer && !(outputBuffer = stream_create())) {
		return false;
	}
	stream_resetOutput(outputBuffer);
	void ***vtableSlot = (void ***)outputBuffer->output;
	void **vtable = *vtableSlot;
	static void *tracingVtable[VTABLE_SLOTS + 1];
	if (traced) {
		tracingVtable[0] = vtable[-1]; // RTTI locator, in case anything looks
		memcpy(&tracingVtable[1], vtable, VTABLE_SLOTS * sizeof(void *));
		originalWrite = (StreamWriteFn)game_functionAt(vtable[WRITE_SLOT]);
		StreamWriteFn replacement = tracedWrite;
		memcpy(
			&tracingVtable[1 + WRITE_SLOT], &replacement, sizeof replacement
		); // code address as a slot
		memset(pathBuckets, 0, sizeof pathBuckets);
		traceFailed = false;
		active = out;
		*vtableSlot = &tracingVtable[1];
	}
	bool ok =
		((MatchStreamFn)game_function(RVA_GAME_MATCH_SERIALIZE))(match, &outputBuffer->output, 0);
	if (traced) {
		*vtableSlot = vtable;
		active = NULL;
		if (traceFailed) {
			log_message("cannot record the serialisation trace");
			return false;
		}
	}
	size_t size = stream_outputSize(outputBuffer);
	if (!ok || size == 0) {
		log_message("serialize failed (ok=%d size=%zu)", ok, size);
		return false;
	}
	if (!buffer_append(&out->bytes, stream_outputData(outputBuffer), size)) {
		return false;
	}
	if (traced) {
		if (!recordsTileBytes(out)) {
			return false;
		}
		log_message(
			"serialised %zu bytes as %zu traced writes, %zu call chains",
			size,
			out->recordCount,
			out->pathCount
		);
	}
	return true;
}

void trace_free(Serialised *serialised) {
	buffer_free(&serialised->bytes);
	free(serialised->records);
	for (size_t i = 0; i < serialised->pathCount; ++i) {
		free(serialised->paths[i]);
	}
	free(serialised->paths);
	*serialised = (Serialised){0};
}

static uint32_t hashChain(const uint32_t *rvas, size_t count) {
	uint32_t hash = 2166136261U;
	for (size_t i = 0; i < count; ++i) {
		hash = (hash ^ rvas[i]) * 16777619U;
	}
	return hash;
}

static bool appendPath(Serialised *serialised, const char *chain) {
	if (serialised->pathCount == serialised->pathCapacity) {
		size_t capacity = serialised->pathCapacity ? serialised->pathCapacity * 2 : 256;
		char **paths = realloc(serialised->paths, capacity * sizeof *paths);
		if (!paths) {
			return false;
		}
		serialised->paths = paths;
		serialised->pathCapacity = capacity;
	}
	size_t size = strlen(chain) + 1;
	char *copy = malloc(size);
	if (!copy) {
		return false;
	}
	memcpy(copy, chain, size);
	serialised->paths[serialised->pathCount++] = copy;
	return true;
}

// Index of a chain in the path table, adding it if new (UINT32_MAX if out of memory or full).
static uint32_t internPath(const uint32_t *rvas, size_t count) {
	uint32_t slot = hashChain(rvas, count) & (PATH_BUCKETS - 1);
	for (uint32_t probe = 0; probe < PATH_BUCKETS; ++probe, slot = (slot + 1) & (PATH_BUCKETS - 1)) {
		PathEntry *entry = &pathBuckets[slot];
		if (entry->index == 0) {
			char chain[CHAIN_CAPACITY] = "";
			size_t length = 0;
			for (size_t i = 0; i < count; ++i) {
				int written =
					snprintf(chain + length, sizeof chain - length, "%s%x", i ? ">" : "", rvas[i]);
				if (written < 0 || (size_t)written >= sizeof chain - length) {
					return UINT32_MAX;
				}
				length += (size_t)written;
			}
			if (!appendPath(active, chain)) {
				return UINT32_MAX;
			}
			memcpy(entry->rvas, rvas, count * sizeof *rvas);
			entry->count = count;
			entry->index = (uint32_t)active->pathCount;
			return (uint32_t)active->pathCount - 1;
		}
		if (entry->count == count && memcmp(entry->rvas, rvas, count * sizeof *rvas) == 0) {
			return entry->index - 1;
		}
	}
	return UINT32_MAX;
}

// frames[0] is the write's call site; the chain is frames[1..k] from the outside in, where frame k
// is the return address inside GAME_MATCH__serialize.
static uint32_t pathOf(void *const *frames, int count) {
	uint32_t rvas[TRACE_DEPTH - 1];
	size_t length = 0;
	for (int k = count - 1; k >= 1; --k) {
		uint32_t rva = game_rva(frames[k]);
		bool top = rva >= RVA_GAME_MATCH_SERIALIZE && rva < RVA_GAME_MATCH_SERIALIZE_END;
		if (length == 0 && !top) {
			continue;
		}
		rvas[length++] = rva;
	}
	return internPath(rvas, length);
}

static int64_t tracedWrite(void *self, const void *bytes, int64_t length) {
	if (length <= 0 || traceFailed) {
		return originalWrite(self, bytes, length);
	}
	void *frames[TRACE_DEPTH] = {0};
	USHORT count =
		RtlCaptureStackBackTrace(1, TRACE_DEPTH, frames, NULL); // [0] = serialiser call site
	uint32_t offset = (uint32_t)stream_outputSize(outputBuffer);
	int64_t written = originalWrite(self, bytes, length);
	if (count == 0 || (uint64_t)length > UINT32_MAX - offset) {
		traceFailed = true;
	} else {
		TraceRecord record = {
			.offset = offset,
			.length = (uint32_t)length,
			.site = game_rva(frames[0]),
			.caller = game_rva(frames[1]),
			.path = pathOf(frames, count),
		};
		traceFailed = record.path == UINT32_MAX || !trace_appendRecord(active, record);
	}
	return written;
}

// The records must tile the bytes exactly, or the config would not re-encode losslessly.
static bool recordsTileBytes(const Serialised *serialised) {
	size_t expected = 0;
	for (size_t i = 0; i < serialised->recordCount; ++i) {
		const TraceRecord *record = &serialised->records[i];
		if (record->offset != expected || record->path >= serialised->pathCount ||
				record->length == 0 || record->length > serialised->bytes.length - expected) {
			log_message("invalid serialisation trace at record %zu", i);
			return false;
		}
		expected += record->length;
	}
	if (expected != serialised->bytes.length) {
		log_message(
			"trace does not cover the output (%zu of %zu bytes)", expected, serialised->bytes.length
		);
		return false;
	}
	return true;
}
