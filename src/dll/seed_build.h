// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Per-seed match inputs that match the game's own build with each seed.
//
// The game's match seed is used twice: GAME_MATCH__init_from_fixture draws the pre-match values
// from it (a random number per player, the officials' match-day ratings, attendance, weather), and
// the deserialiser seeds the in-match RNG with it. An input file carries the pre-match values of
// the seed it was built with, so patching only its seed field does not reproduce the game. A seed
// builder rebuilds the input's fixture with each seed instead and re-applies the input's edits:
// every field where the input differs from a build with its own seed keeps the input's value.
#pragma once

#include "buffer.h"
#include "fixture.h"
#include "trace.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <windows.h>


typedef struct SeedBuilder {
	NextFixture next;        // the human manager's next fixture (the input's)
	const uint8_t *teams[2]; // home/away db::TEAM overrides, or NULL for the fixture's own
	Serialised input;        // the input, deserialised and serialised again (traced)
	bool *edited;            // per input record: differs from a build with the input's own seed
	size_t editCount;
	CRITICAL_SECTION lock; // builds share the game's fixture and the trace hooks
} SeedBuilder;

// Prepares to rebuild `input` (serialised match bytes built with `inputSeed`), with optional
// home/away team overrides. Fails, with a message in `error`,
// unless the input is the human manager's next fixture with the same layout as a fresh build.
// Must run on the job thread (it loads matches through stream slot 0).
bool seedBuilder_open(
	SeedBuilder *builder,
	const ByteBuffer *input,
	uint32_t inputSeed,
	const uint8_t *const teams[2],
	char *error,
	size_t errorSize
);

// Forgets the input's edits to the selected team's block (0: home, 1: away).
bool seedBuilder_dropTeamEdits(SeedBuilder *builder, unsigned side, size_t *dropped);

// How many of the input's fields are re-applied to each build.
size_t seedBuilder_editCount(const SeedBuilder *builder);

// The match input for `seed`: the game's build with `seed` (with builder->teams, which may be
// changed between builds), with the input's edits re-applied. Thread-safe (builds one at a
// time). The build and the input are aligned part by part (see seed_build.c), so their structures
// may differ, e.g. another team; an edit in a part that cannot be aligned fails the build.
bool seedBuilder_build(SeedBuilder *builder, uint32_t seed, ByteBuffer *out);

void seedBuilder_close(SeedBuilder *builder);
