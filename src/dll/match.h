// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// GAME_MATCH objects: building one from a fixture, loading one from serialised bytes, playing it
// with the game's engine and reading the result.
#pragma once

#include "buffer.h"
#include "stream.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


typedef struct MatchResult {
	uint32_t seed;
	unsigned homeGoals;
	unsigned awayGoals;
	uint32_t clockTicks;
	bool finished;
	uint64_t finalRngState;
} MatchResult;

// Builds a match from a fixture exactly as MatchBuilder_Create (mode 1) does, with the thread-local
// RNG seeded from `seed` so the build is reproducible. Free with match_freeBuilt().
uint8_t *match_build(uint8_t *fixture, uint32_t seed);
void match_freeBuilt(uint8_t *match);

// Loads serialised match bytes into a new match. Free with match_delete().
uint8_t *match_deserialize(ByteBuffer *bytes);

// The same through the caller's stream, for threads that load matches concurrently.
uint8_t *match_deserializeWith(DataBuffer *stream, ByteBuffer *bytes);
void match_delete(uint8_t *match);

// Prepares the match and plays it to the end with GAME_MATCH__Run.
void match_play(uint8_t *match);

MatchResult match_result(const uint8_t *match);

// Where the seed (a u32) sits in serialised match bytes: after a 13-byte header, a u32-length label
// and 8 more bytes. Returns false if the bytes do not look like a serialised match.
bool match_seedOffset(const uint8_t *bytes, size_t size, size_t *offset);
