// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-seed match inputs that match the game's own build with each seed (docs/PLAN.md Phase 19).
//
// The game's match seed is used twice: GAME_MATCH__init_from_fixture draws the pre-match values
// from it (a random number per player, the officials' match-day ratings, attendance, weather), and
// the deserialiser seeds the in-match RNG with it. An input file carries the pre-match values of
// the seed it was built with, so patching only its seed field does not reproduce the game. A seed
// builder rebuilds the input's fixture with each seed instead and re-applies the input's edits:
// every field where the input differs from a build with its own seed keeps the input's value.
#pragma once

#include "buffer.h"
#include "trace.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>


typedef struct SeedBuilder {
	uint8_t *fixture; // the human manager's next fixture (the input's)
	Serialised input; // the input, deserialised and serialised again (traced)
	bool *edited;     // per input record: differs from a build with the input's own seed
	size_t editedCount;
	CRITICAL_SECTION lock; // builds share the game's fixture and the trace hooks
} SeedBuilder;

// Prepares to rebuild `input` (serialised match bytes built with `inputSeed`). Fails, with a
// message in `error`, unless the input is the human manager's next fixture with the same layout
// as a fresh build. Must run on the job thread (it loads matches through stream slot 0).
bool seedBuilder_open(
	SeedBuilder *builder,
	const ByteBuffer *input,
	uint32_t inputSeed,
	char *error,
	size_t errorSize
);

// The match input for `seed`: the game's build with `seed`, with the input's edits re-applied.
// Thread-safe (builds one at a time).
bool seedBuilder_build(SeedBuilder *builder, uint32_t seed, ByteBuffer *out);

void seedBuilder_close(SeedBuilder *builder);
