// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Serialising a match, optionally with a trace of every write the game's serialiser makes.
//
// While tracing, the output stream's vtable is swapped for a copy whose write method (slot 5)
// records each write's offset, length and the game code that issued it. Each serialiser call site
// writes one field, so the call site identifies the field; replaying the writes in order rebuilds
// the bytes exactly, which is what makes the JSON match config lossless.
#pragma once

#include "buffer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


typedef struct TraceRecord {
	uint32_t offset; // in the serialised bytes
	uint32_t length;
	uint32_t site;   // RVA of the write's call site
	uint32_t caller; // RVA of the call site's caller
	uint32_t path;   // index into Serialised.paths
} TraceRecord;

typedef struct Serialised {
	ByteBuffer bytes;
	TraceRecord *records;
	size_t recordCount;
	size_t recordCapacity;
	// Distinct call chains, "outer>...>inner" return-address RVAs starting inside
	// GAME_MATCH__serialize (whose call site names the part of the match being written).
	char **paths;
	size_t pathCount;
	size_t pathCapacity;
} Serialised;

// Serialises `match` into `out` (which must be zeroed or freed first); with `traced`, also fills
// the records and paths and checks that the records tile the bytes exactly.
bool trace_serialize(uint8_t *match, Serialised *out, bool traced);

bool trace_appendRecord(Serialised *serialised, TraceRecord record);

// Index of `chain` in the path table, adding it if new (UINT32_MAX if out of memory).
uint32_t trace_findPath(Serialised *serialised, const char *chain);

void trace_free(Serialised *serialised);
