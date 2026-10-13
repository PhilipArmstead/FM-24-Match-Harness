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

// The game stream worker `slot` (0 = the job thread, < HARNESS_MAX_THREADS) loads matches through;
// created on first use and kept. NULL if it cannot be created.
DataBuffer *match_inputStream(unsigned slot);

// Loads serialised match bytes into a new match on the job thread. Free with match_delete().
uint8_t *match_deserialize(ByteBuffer *bytes);

// The same through worker `slot`'s stream, for workers that load matches concurrently.
uint8_t *match_deserializeOn(unsigned slot, ByteBuffer *bytes);
void match_delete(uint8_t *match);

// Hands both teams' in-match changes (substitutions etc.) to the game's AI, as when the game
// simulates a match nobody watches. A human-managed team's flags are reset to
// MANAGER_HUMAN_NO_PLAN: the human is no longer in control, and the match plan settings of whoever
// built the input are dropped. AI-managed teams are left as they are.
void match_handToAssistant(const uint8_t *match);

// Prepares the match and plays it to the end with GAME_MATCH__Run.
void match_play(uint8_t *match);

MatchResult match_result(const uint8_t *match);

// Where the seed (a u32) sits in serialised match bytes: after a 13-byte header, a u32-length label
// and 8 more bytes. Returns false if the bytes do not look like a serialised match.
bool match_seedOffset(const uint8_t *bytes, size_t size, size_t *offset);
