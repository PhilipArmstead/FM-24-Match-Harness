// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "match.h"

#include "game.h"
#include "memory.h"
#include "rng.h"
#include "stream.h"

#include <xmmintrin.h>


enum {
	SEED_HEADER_SIZE = 13,
	SEED_LABEL_GAP = 8,
	MAX_LABEL_LENGTH = 256,
	BUILD_MODE = 1, // MatchBuilder_Create's mode for the user's own match (build, serialise)
	RUN_TO_END = 1,
};

static DataBuffer *inputBuffer;

uint8_t *match_build(uint8_t *fixture, uint32_t seed) {
	rng_seedThread(seed);
	uint8_t scratch[16] __attribute__((aligned(16))) = {0};
	((FixturePrepareFn)game_function(RVA_FIXTURE_PREPARE_TEAMS))(fixture, scratch);
	uint8_t *match = ((MatchCreateFn)game_function(RVA_ALLOC_FROM_POOL))();
	if (!match) {
		return NULL;
	}
	((MatchFn)game_function(RVA_MATCH_CTOR))(match);
	*(uintptr_t *)match = (uintptr_t)game_address(RVA_GAME_MATCH_VTABLE);
	match[MATCH_BUILD_FLAG] = 0;
	InitArgs args = {.fixture = fixture, .seed = seed, .scratch = scratch, .mode = BUILD_MODE};
	uint8_t flags[8] = {0, BUILD_MODE, 0xFF};
	((MatchInitFn)game_function(RVA_INIT_FROM_FIXTURE))(match, &args, flags);
	return match;
}

void match_freeBuilt(uint8_t *match) {
	((MatchFn)game_function(RVA_GAME_MATCH_DTOR))(match);
	((MatchFn)game_function(RVA_GAME_MATCH_POOL_FREE))(match);
}

uint8_t *match_deserialize(ByteBuffer *bytes) {
	if (!inputBuffer && !(inputBuffer = stream_create())) {
		return NULL;
	}
	stream_attachInput(inputBuffer, bytes->data, bytes->length);
	uint8_t *match = ((MatchCreateFn)game_function(RVA_GAME_MATCH_CREATE))();
	bool ok =
		match &&
		((MatchStreamFn)game_function(RVA_GAME_MATCH_DESERIALIZE))(match, &inputBuffer->input, 0);
	stream_detachInput(inputBuffer);
	if (!ok && match) {
		match_delete(match);
	}
	return ok ? match : NULL;
}

void match_delete(uint8_t *match) {
	// The scalar deleting destructor (vtable slot 0), as the game's session code uses.
	MatchDeleteFn destroy = **(MatchDeleteFn **)match;
	destroy(match, 1);
}

void match_play(uint8_t *match) {
	unsigned csr = _mm_getcsr();
	((MatchPrepareFn)game_function(RVA_GAME_MATCH_PREPARE))(match, 0);
	((MatchRunFn)game_function(RVA_GAME_MATCH_RUN))(match, RUN_TO_END, -1);
	_mm_setcsr(csr); // Run switches to round-to-nearest + flush-to-zero; restore ours
}

MatchResult match_result(const uint8_t *match) {
	const uint8_t *home = *(uint8_t *const *)(match + MATCH_TEAM_HOME);
	const uint8_t *away = *(uint8_t *const *)(match + MATCH_TEAM_AWAY);
	return (MatchResult){
		.seed = memory_loadU32(match + MATCH_SEED),
		.homeGoals = home[MATCH_TEAM_GOALS],
		.awayGoals = away[MATCH_TEAM_GOALS],
		.clockTicks = memory_loadU32(match + MATCH_CLOCK),
		.finished = (memory_loadU32(match + MATCH_FLAGS) >> 2) & 1,
		.finalRngState = memory_loadU64(match + MATCH_RNG_STATE),
	};
}

bool match_seedOffset(const uint8_t *bytes, size_t size, size_t *offset) {
	if (size < 40 || bytes[0] != 0x09 || bytes[1] != 0x08 || bytes[6] != '!') {
		return false;
	}
	uint32_t labelLength = memory_loadU32(bytes + SEED_HEADER_SIZE);
	*offset = SEED_HEADER_SIZE + sizeof(uint32_t) + (size_t)labelLength + SEED_LABEL_GAP;
	return labelLength < MAX_LABEL_LENGTH && *offset + sizeof(uint32_t) <= size;
}
