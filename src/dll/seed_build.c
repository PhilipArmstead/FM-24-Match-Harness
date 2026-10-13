// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "seed_build.h"

#include "fixture.h"
#include "log.h"
#include "match.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static bool buildTraced(uint8_t *fixture, uint32_t seed, Serialised *out);
static bool roundTrip(const ByteBuffer *bytes, Serialised *out);
static bool sameLayout(const Serialised *a, const Serialised *b);
static bool sameRecord(const Serialised *a, const Serialised *b, size_t i);
static bool appendRecord(ByteBuffer *out, const Serialised *from, size_t i);
static bool
mergeEdits(const SeedBuilder *builder, const Serialised *built, uint32_t seed, ByteBuffer *out);

bool seedBuilder_open(
	SeedBuilder *builder,
	const ByteBuffer *input,
	uint32_t inputSeed,
	char *error,
	size_t errorSize
) {
	*builder = (SeedBuilder){0};
	NextFixture next;
	const char *why = NULL;
	if (!fixture_findNext(&next, &why)) {
		(void)snprintf(error, errorSize, "no next fixture to rebuild: %s", why);
		return false;
	}
	builder->fixture = next.fixture;
	// Compare the input with a build of its own seed, both loaded and serialised again so that the
	// serialiser's own differences (a header flag the deserialiser sets) cancel out.
	Serialised built = {0};
	Serialised reference = {0};
	bool ok = buildTraced(builder->fixture, inputSeed, &built) &&
						roundTrip(&built.bytes, &reference) && roundTrip(input, &builder->input);
	trace_free(&built);
	if (!ok) {
		(void)snprintf(error, errorSize, "building or loading the match failed; see harness.log");
	} else if (!sameLayout(&reference, &builder->input)) {
		(void)snprintf(
			error,
			errorSize,
			"the input is not the game's next fixture (%s) or its layout was edited",
			next.description
		);
		ok = false;
	}
	if (ok) {
		builder->editedCount = builder->input.recordCount;
		builder->edited = calloc(builder->editedCount, sizeof *builder->edited);
		ok = builder->edited != NULL;
	}
	size_t edits = 0;
	for (size_t i = 0; ok && i < builder->editedCount; ++i) {
		builder->edited[i] = !sameRecord(&reference, &builder->input, i);
		edits += builder->edited[i];
	}
	trace_free(&reference);
	if (!ok) {
		seedBuilder_close(builder);
		return false;
	}
	InitializeCriticalSection(&builder->lock);
	log_message("seed builds: %s; %zu fields differ from the game's build", next.description, edits);
	return true;
}

bool seedBuilder_build(SeedBuilder *builder, uint32_t seed, ByteBuffer *out) {
	Serialised built = {0};
	EnterCriticalSection(&builder->lock);
	bool ok = buildTraced(builder->fixture, seed, &built);
	LeaveCriticalSection(&builder->lock);
	ok = ok && mergeEdits(builder, &built, seed, out);
	trace_free(&built);
	return ok;
}

void seedBuilder_close(SeedBuilder *builder) {
	if (builder->edited) {
		DeleteCriticalSection(&builder->lock);
	}
	trace_free(&builder->input);
	free(builder->edited);
	*builder = (SeedBuilder){0};
}

// The game's build of `fixture` with `seed`, serialised with its write records.
static bool buildTraced(uint8_t *fixture, uint32_t seed, Serialised *out) {
	uint8_t *match = match_build(fixture, seed);
	if (!match) {
		return false;
	}
	bool ok = trace_serialize(match, out, true);
	match_freeBuilt(match);
	return ok;
}

// Loads serialised match bytes and serialises the match again, with its write records.
static bool roundTrip(const ByteBuffer *bytes, Serialised *out) {
	ByteBuffer copy = {0};
	if (!buffer_append(&copy, bytes->data, bytes->length)) {
		return false;
	}
	uint8_t *match = match_deserialize(&copy);
	bool ok = match && trace_serialize(match, out, true);
	if (match) {
		match_delete(match);
	}
	buffer_free(&copy);
	return ok;
}

// The same writes in the same order (values, and so string lengths, may differ).
static bool sameLayout(const Serialised *a, const Serialised *b) {
	if (a->recordCount != b->recordCount) {
		return false;
	}
	for (size_t i = 0; i < a->recordCount; ++i) {
		if (a->records[i].site != b->records[i].site) {
			return false;
		}
	}
	return true;
}

static bool sameRecord(const Serialised *a, const Serialised *b, size_t i) {
	const TraceRecord *x = &a->records[i];
	const TraceRecord *y = &b->records[i];
	return x->length == y->length &&
				 memcmp(a->bytes.data + x->offset, b->bytes.data + y->offset, x->length) == 0;
}

static bool appendRecord(ByteBuffer *out, const Serialised *from, size_t i) {
	const TraceRecord *record = &from->records[i];
	return buffer_append(out, from->bytes.data + record->offset, record->length);
}

// The game's build for `seed` with the input's edits re-applied. The build's structure can differ
// from the input's (the AI manager picks its tactic with the seed, which changes how many player
// instructions it writes), so the two are aligned on their common start and end. An edit between
// those cannot be placed and fails the seed.
static bool
mergeEdits(const SeedBuilder *builder, const Serialised *built, uint32_t seed, ByteBuffer *out) {
	const Serialised *input = &builder->input;
	size_t inputCount = input->recordCount;
	size_t builtCount = built->recordCount;
	size_t shorter = inputCount < builtCount ? inputCount : builtCount;
	size_t head = 0;
	while (head < shorter && input->records[head].site == built->records[head].site) {
		++head;
	}
	size_t tail = 0;
	while (head + tail < shorter &&
				 input->records[inputCount - 1 - tail].site == built->records[builtCount - 1 - tail].site) {
		++tail;
	}
	for (size_t i = head; i < inputCount - tail; ++i) {
		if (builder->edited[i]) {
			log_message(
				"seed %u: the game's build has a different structure around edited field #%zu", seed, i
			);
			return false;
		}
	}
	buffer_clear(out);
	for (size_t j = 0; j < builtCount; ++j) {
		size_t i = SIZE_MAX;
		if (j < head) {
			i = j;
		} else if (j >= builtCount - tail) {
			i = j - builtCount + inputCount;
		}
		bool fromInput = i != SIZE_MAX && builder->edited[i];
		if (!appendRecord(out, fromInput ? input : built, fromInput ? i : j)) {
			return false;
		}
	}
	return true;
}
