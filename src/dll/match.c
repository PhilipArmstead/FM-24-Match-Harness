// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "match.h"

#include "game.h"
#include "memory.h"
#include "protocol.h"
#include "rng.h"
#include "stream.h"

#include <string.h>
#include <xmmintrin.h>


enum {
	SEED_HEADER_SIZE = 13,
	SEED_LABEL_GAP = 8,
	MAX_LABEL_LENGTH = 256,
	BUILD_MODE = 1, // MatchBuilder_Create's mode for the user's own match (build, serialise)
	RUN_TO_END = 1,
};

// Input streams by worker slot, created on first use and kept: the game owns their storage.
static DataBuffer *inputStreams[HARNESS_MAX_THREADS];

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

DataBuffer *match_inputStream(unsigned slot) {
	if (slot >= HARNESS_MAX_THREADS) {
		return NULL;
	}
	if (!inputStreams[slot]) {
		inputStreams[slot] = stream_create();
	}
	return inputStreams[slot];
}

uint8_t *match_deserialize(ByteBuffer *bytes) {
	return match_deserializeOn(0, bytes);
}

uint8_t *match_deserializeOn(unsigned slot, ByteBuffer *bytes) {
	DataBuffer *stream = match_inputStream(slot);
	if (!stream) {
		return NULL;
	}
	stream_attachInput(stream, bytes->data, bytes->length);
	uint8_t *match = ((MatchCreateFn)game_function(RVA_GAME_MATCH_CREATE))();
	bool ok =
		match && ((MatchStreamFn)game_function(RVA_GAME_MATCH_DESERIALIZE))(match, &stream->input, 0);
	stream_detachInput(stream);
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

void match_handToAssistant(const uint8_t *match) {
	for (int side = 0; side < 2; ++side) {
		const uint8_t *team = *(uint8_t *const *)(match + (side ? MATCH_TEAM_AWAY : MATCH_TEAM_HOME));
		uint8_t *manager = *(uint8_t *const *)(team + MATCH_TEAM_MANAGER);
		uint32_t flags = memory_loadU32(manager + MANAGER_FLAGS) & ~(uint32_t)MANAGER_HUMAN_IN_CONTROL;
		memcpy(manager + MANAGER_FLAGS, &flags, sizeof flags);
	}
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
