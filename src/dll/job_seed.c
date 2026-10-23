// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Job "seed_development": makes the game's own next development of one player draw from a chosen
// seed, so its result can be compared with the row `develop` writes for that seed from the same
// save. It redirects the daily scheduler's two calls of the development routine to a stub that, for
// that player, seeds the game thread's PCG exactly as job_develop.c does, then puts the calls back.
// The game is otherwise untouched; the redirect stays until the player develops or FM24 exits.

#include "database.h"
#include "game.h"
#include "job.h"
#include "labels.h"
#include "log.h"
#include "memory.h"
#include "rng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>


enum {
	CALL_LENGTH = 5, // E8 rel32
	CALL_OPCODE = 0xE8,
	STUB_SIZE = 4096,
	ALLOCATION_STEP = 0x10000,
	NEAR_RANGE = 0x70000000, // how far below fm.exe to look for the stub: well within rel32's reach
	STUB_HOOK_SLOT = 28,     // offsets in stubCode of its two absolute targets
	STUB_ROUTINE_SLOT = 36,
};

// Saves the routine's three arguments, calls the hook with the player still in rcx, restores them
// and jumps on to the routine. rsp is 8 mod 16 on entry, so three pushes and 0x20 of shadow space
// align it for the call.
static const uint8_t stubCode[STUB_HOOK_SLOT] = {
	0x51, // push rcx
	0x52, // push rdx
	0x41,
	0x50, // push r8
	0x48,
	0x83,
	0xEC,
	0x20, // sub rsp, 20h
	0xFF,
	0x15,
	0x0E,
	0x00,
	0x00,
	0x00, // call [rip+14]: the hook slot
	0x48,
	0x83,
	0xC4,
	0x20, // add rsp, 20h
	0x41,
	0x58, // pop r8
	0x5A, // pop rdx
	0x59, // pop rcx
	0xFF,
	0x25,
	0x08,
	0x00,
	0x00,
	0x00, // jmp [rip+8]: the routine slot
};

static const uint32_t callSites[] = {RVA_DEVELOP_CALL_DAY, RVA_DEVELOP_CALL_PERSON};

static uint8_t *stub;           // allocated once, near fm.exe, and kept
static volatile LONG armed;     // 1 while the calls are redirected
static volatile LONG armedUid;  // the player's person unique ID (as a LONG)
static volatile LONG armedSeed; // the seed (as a LONG)
// Optional: develop's simulation for these seeds, run by the hook first (develop_runHere).
static uint32_t *predictSeeds;
static size_t predictCount;
static wchar_t predictOutput[HARNESS_PATH_LENGTH];

static bool makeStub(void);
static bool redirect(bool toStub);
static void onDevelop(uint8_t *player);

void jobs_seedDevelopment(const Job *job) {
	PeopleIndex people;
	if (!database_loadPeople(&people)) {
		job_writeResult("error: cannot read the people list; is a save loaded?");
		return;
	}
	const uint8_t *person = database_findPerson(&people, job->personId);
	const uint8_t *player = person ? database_playerRecord(person) : NULL;
	char name[PLAYER_NAME_LENGTH] = "";
	if (person && !database_personName(person, name, sizeof name)) {
		(void)snprintf(name, sizeof name, "(unnamed, UID %u)", job->personId);
	}
	database_freePeople(&people);
	if (!player) {
		job_writeResult("error: no player with unique ID %u", job->personId);
		return;
	}
	if (!stub && !makeStub()) {
		job_writeResult("error: cannot set up the hook; see harness.log");
		return;
	}
	uint32_t *seeds = job->seedCount ? malloc(job->seedCount * sizeof *seeds) : NULL;
	if (job->seedCount && !seeds) {
		job_writeResult("error: out of memory");
		return;
	}
	if (seeds) {
		memcpy(seeds, job->seeds, job->seedCount * sizeof *seeds);
	}
	free(predictSeeds); // the hook only reads these when it fires, never while a job runs
	predictSeeds = seeds;
	predictCount = job->seedCount;
	memcpy(predictOutput, job->output, sizeof predictOutput);
	InterlockedExchange(&armedUid, (LONG)job->personId);
	InterlockedExchange(&armedSeed, (LONG)job->seed);
	if (!InterlockedCompareExchange(&armed, 1, 0) && !redirect(true)) {
		InterlockedExchange(&armed, 0);
		job_writeResult("error: the scheduler's calls are not the ones the harness knows");
		return;
	}
	log_message(
		"seed_development: %s (%u) will develop with seed %u", name, job->personId, job->seed
	);
	job_writeResult(
		"ok: %s (%u) will develop with seed %u; advance past their development day",
		name,
		job->personId,
		job->seed
	);
}

// The stub goes within rel32 reach of the call sites: below fm.exe, where nothing else is mapped.
static bool makeStub(void) {
	uintptr_t base = game_base();
	for (uintptr_t address = (base - ALLOCATION_STEP) & ~(uintptr_t)(ALLOCATION_STEP - 1);
		address > base - NEAR_RANGE;
		address -= ALLOCATION_STEP) {
		void *at = NULL;
		memcpy(&at, &address, sizeof at);
		uint8_t *memory = VirtualAlloc(at, STUB_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
		if (memory) {
			void (*hook)(uint8_t *) = onDevelop;
			void *routine = game_address(RVA_PLAYER_DEVELOP);
			memcpy(memory, stubCode, sizeof stubCode);
			memcpy(memory + STUB_HOOK_SLOT, &hook, sizeof hook);
			memcpy(memory + STUB_ROUTINE_SLOT, &routine, sizeof routine);
			FlushInstructionCache(GetCurrentProcess(), memory, STUB_SIZE);
			stub = memory;
			return true;
		}
	}
	log_message("seed_development: no free page within reach of fm.exe");
	return false;
}

// Points both calls at the stub, or back at the routine. Each must be a call of either one.
static bool redirect(bool toStub) {
	const uint8_t *routine = game_address(RVA_PLAYER_DEVELOP);
	for (size_t i = 0; i < sizeof callSites / sizeof callSites[0]; ++i) {
		uint8_t *call = game_address(callSites[i]);
		int32_t displacement = 0;
		memcpy(&displacement, call + 1, sizeof displacement);
		const uint8_t *target = call + CALL_LENGTH + displacement;
		if (call[0] != CALL_OPCODE || (target != routine && target != stub)) {
			log_message("seed_development: fm.exe+0x%x is not a call of the routine", callSites[i]);
			return false;
		}
	}
	for (size_t i = 0; i < sizeof callSites / sizeof callSites[0]; ++i) {
		uint8_t *call = game_address(callSites[i]);
		const uint8_t *target = toStub ? stub : routine;
		int32_t displacement = (int32_t)(target - (call + CALL_LENGTH));
		DWORD protection = 0;
		if (!VirtualProtect(call + 1, sizeof displacement, PAGE_EXECUTE_READWRITE, &protection)) {
			log_message(
				"seed_development: cannot unprotect the call (Windows error %lu)", GetLastError()
			);
			return false;
		}
		memcpy(call + 1, &displacement, sizeof displacement);
		VirtualProtect(call + 1, sizeof displacement, protection, &protection);
		FlushInstructionCache(GetCurrentProcess(), call, CALL_LENGTH);
	}
	return true;
}

// Runs on the game's thread for every player it develops while armed.
static void onDevelop(uint8_t *player) {
	uint8_t *offsets = NULL;
	int32_t personOffset = 0;
	uint32_t uid = 0;
	if (!armed || !memory_readPointer(player, 8, &offsets) ||
			!memory_read(offsets + 4, &personOffset, sizeof personOffset) ||
			!memory_readU32(player + personOffset + 8, PERSON_UNIQUE_ID, &uid) ||
			uid != (uint32_t)armedUid || !InterlockedCompareExchange(&armed, 0, 1)) {
		return;
	}
	if (predictCount) {
		// develop's simulation of this very development, on this thread and at this moment; it
		// puts the player's simulated values back before the game's own call runs.
		bool predicted = develop_runHere(player, predictSeeds, predictCount, predictOutput);
		log_message(
			"seed_development: simulated %zu seeds here first%s",
			predictCount,
			predicted ? "" : " (failed)"
		);
	}
	rng_seedThread((uint32_t)armedSeed); // as job_develop.c does before each seed
	redirect(false);                     // the call that got here has already run
	log_message("seed_development: player %u developing now with seed %u", uid, (uint32_t)armedSeed);
}
