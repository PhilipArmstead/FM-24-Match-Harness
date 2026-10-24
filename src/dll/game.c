// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "game.h"

#include "log.h"

#include <string.h>
#include <windows.h>


enum {
	IMAGE_SPAN = 0x1E000000, // generous upper bound on fm.exe's mapped size
	TEB_TLS_POINTER = 0x58,  // TEB.ThreadLocalStoragePointer
	JMP_REL32 = 0xE9,
	JMP_REL32_LENGTH = 5,
	ALLOCATION_STEP = 0x10000,
	NEAR_RANGE = 0x70000000, // how far below fm.exe to look: well within rel32's reach
};

typedef struct Signature {
	const char *name;
	const char *bytes;
	size_t length;
	uint32_t rva;
	bool thunk; // rva is a `jmp rel32` thunk; bytes are the prologue of its target
} Signature;

#define SIGNATURE(rva, name, bytes) {(name), (bytes), sizeof(bytes) - 1, (rva), false}
// A thunk's displacement differs between builds (the Epic build's prepare_to_play target is
// 0x25D10 further on, docs/PLAN.md Phase 20), so only the opcode and the target are checked.
#define SIGNATURE_THUNK(rva, name, bytes) {(name), (bytes), sizeof(bytes) - 1, (rva), true}

// The first bytes of every function the harness calls. A mismatch means a different game build,
// or a software breakpoint (0xCC) planted by a debugger.
static const Signature signatures[] = {
	SIGNATURE(RVA_GAME_MATCH_CREATE, "GAME_MATCH__create", "\x48\x83\xEC\x28\x48\x8B\x05\xC5\x8C"),
	SIGNATURE(
		RVA_GAME_MATCH_DESERIALIZE,
		"GAME_MATCH__deserialize",
		"\x41\x56\x56\x57\x53\x48\x83\xEC\x38"
	),
	SIGNATURE_THUNK(
		RVA_GAME_MATCH_PREPARE,
		"GAME_MATCH__prepare_to_play",
		"\x56\x57\x48\x83\xEC\x28\x48\x89\xCE"
	),
	SIGNATURE(RVA_GAME_MATCH_RUN, "GAME_MATCH__Run", "\x41\x57\x41\x56\x41\x55\x41\x54\x56"),
	SIGNATURE(RVA_DATA_BUFFER_CTOR, "DATA_BUFFER__ctor", "\x55\x56\x57\x53\x48\x83\xEC\x38\x48"),
	SIGNATURE(
		RVA_ALLOC_FROM_POOL,
		"GAME_MATCH__alloc_from_pool",
		"\x56\x48\x83\xEC\x40\x8B\x05\xE5\xCB"
	),
	SIGNATURE(RVA_MATCH_CTOR, "MATCH__ctor", "\x55\x41\x57\x41\x56\x41\x54\x56\x57"),
	SIGNATURE(
		RVA_FIXTURE_PREPARE_TEAMS,
		"fixture_prepare_teams",
		"\x56\x57\x53\x48\x83\xEC\x30\x48\x89"
	),
	SIGNATURE(
		RVA_INIT_FROM_FIXTURE,
		"GAME_MATCH__init_from_fixture",
		"\x55\x41\x57\x41\x56\x41\x55\x41\x54"
	),
	SIGNATURE(
		RVA_GAME_MATCH_SERIALIZE,
		"GAME_MATCH__serialize",
		"\x41\x56\x56\x57\x55\x53\x48\x83\xEC"
	),
	SIGNATURE(RVA_GAME_MATCH_DTOR, "GAME_MATCH__dtor", "\x41\x57\x41\x56\x56\x57\x53\x48\x83"),
	SIGNATURE(
		RVA_GAME_MATCH_POOL_FREE,
		"GAME_MATCH__pool_free",
		"\x56\x48\x83\xEC\x40\x48\x89\xCE\x8B"
	),
};

// Where the older build's code and data lie relative to the Steam build's, from `fm24ctl probe`.
// From `start` (a Steam RVA) on, an address is `delta` bytes lower, until the next entry. The
// boundaries are guesses between the probe's nearest functions; functions the probe could not
// match exactly (the relocated .arch code) are not translated, so call sites in them don't match.
typedef struct Shift {
	uint32_t start;
	int32_t delta;
} Shift;

static const Shift oldBuildShifts[] = {
	{0, 0},
	{0x1800000, -64},
	{0x2000000, -528},
	{0x3800000, -848},
	{0x557F000, 0}, // .00cfg: data
	{0x5A40000, -32},
	{0x6264000, 0}, // .shared
};

static uint8_t *base;
static const Shift *shifts; // NULL on the Steam and Epic builds
static size_t shiftCount;

static bool checkTimestamp(void);
static bool checkSignatures(void);

bool game_init(void) {
	base = (uint8_t *)GetModuleHandleW(L"fm.exe");
	if (!base) {
		log_message("not loaded into fm.exe");
		return false;
	}
	log_message("loaded into fm.exe at %p", (void *)base);
	if (game_timestamp() == GAME_PE_TIMESTAMP_STEAM_OLD) {
		shifts = oldBuildShifts;
		shiftCount = sizeof oldBuildShifts / sizeof oldBuildShifts[0];
	}
	return checkTimestamp() && checkSignatures();
}

uintptr_t game_base(void) {
	return (uintptr_t)base;
}

uint32_t game_timestamp(void) {
	const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
	const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
	return nt->FileHeader.TimeDateStamp;
}

void *game_address(uint32_t rva) {
	for (size_t i = shiftCount; i-- > 0;) {
		if (rva >= shifts[i].start) {
			return base + rva + shifts[i].delta;
		}
	}
	return base + rva;
}

VoidFn game_functionAt(const void *address) {
	// ISO C has no object-to-function pointer conversion; copying the representation is the
	// portable way to do what every Windows compiler supports.
	VoidFn function = NULL;
	memcpy(&function, &address, sizeof function);
	return function;
}

VoidFn game_function(uint32_t rva) {
	return game_functionAt(game_address(rva));
}

uint32_t game_rva(const void *address) {
	uintptr_t value = (uintptr_t)address;
	uintptr_t start = (uintptr_t)base;
	if (value <= start || value >= start + IMAGE_SPAN) {
		return 0;
	}
	uint32_t rva = (uint32_t)(value - start);
	for (size_t i = shiftCount; i-- > 0;) {
		if ((int64_t)rva >= (int64_t)shifts[i].start + shifts[i].delta) {
			return (uint32_t)((int64_t)rva - shifts[i].delta);
		}
	}
	return rva;
}

uint8_t *game_initThread(void) {
	uint8_t **slots = NULL;
	memcpy(&slots, (const uint8_t *)NtCurrentTeb() + TEB_TLS_POINTER, sizeof slots);
	uint32_t index = *(const uint32_t *)game_address(RVA_TLS_INDEX);
	uint8_t *tls = slots[index];
	if (tls[TLS_INIT_FLAG] == 0) {
		// What the game does on first use: run this thread's thread_local constructors.
		((VoidFn)game_function(RVA_TLS_ON_DEMAND_INIT))();
	}
	return tls;
}

// Below fm.exe, where nothing else is mapped.
uint8_t *game_allocateNear(size_t size) {
	uintptr_t start = (uintptr_t)base;
	for (uintptr_t address = (start - ALLOCATION_STEP) & ~(uintptr_t)(ALLOCATION_STEP - 1);
		address > start - NEAR_RANGE;
		address -= ALLOCATION_STEP) {
		void *at = NULL;
		memcpy(&at, &address, sizeof at);
		uint8_t *memory = VirtualAlloc(at, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
		if (memory) {
			return memory;
		}
	}
	log_message("game: no free page within reach of fm.exe");
	return NULL;
}

static bool checkTimestamp(void) {
	uint32_t timestamp = game_timestamp();
	if (timestamp == GAME_PE_TIMESTAMP_STEAM) {
		log_message("fm.exe build %u (Steam)", (unsigned)timestamp);
		return true;
	}
	if (timestamp == GAME_PE_TIMESTAMP_STEAM_OLD) {
		log_message("fm.exe build %u (older Steam)", (unsigned)timestamp);
		return true;
	}
	if (timestamp == GAME_PE_TIMESTAMP_EPIC) {
		log_message("fm.exe build %u (Epic Games)", (unsigned)timestamp);
		return true;
	}
	log_message(
		"refusing: fm.exe timestamp %u, expected %d (Steam), %d (older Steam) or %d (Epic Games)",
		(unsigned)timestamp,
		GAME_PE_TIMESTAMP_STEAM,
		GAME_PE_TIMESTAMP_STEAM_OLD,
		GAME_PE_TIMESTAMP_EPIC
	);
	return false;
}

static bool checkSignatures(void) {
	for (size_t i = 0; i < sizeof signatures / sizeof signatures[0]; ++i) {
		const Signature *signature = &signatures[i];
		const uint8_t *code = game_address(signature->rva);
		if (signature->thunk) {
			if (code[0] != JMP_REL32) {
				log_message(
					"refusing: %s at fm.exe+0x%x is not a jmp thunk (software breakpoint?)",
					signature->name,
					signature->rva
				);
				return false;
			}
			int32_t displacement = 0;
			memcpy(&displacement, code + 1, sizeof displacement);
			code += JMP_REL32_LENGTH + displacement;
		}
		if (memcmp(code, signature->bytes, signature->length) != 0) {
			log_message(
				"refusing: prologue of %s at fm.exe+0x%x does not match (software breakpoint?)",
				signature->name,
				game_rva(code)
			);
			return false;
		}
	}
	return true;
}
