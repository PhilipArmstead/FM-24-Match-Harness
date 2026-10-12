// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "game.h"

#include "log.h"

#include <string.h>
#include <windows.h>


enum {
	IMAGE_SPAN = 0x1E000000, // generous upper bound on fm.exe's mapped size
	TEB_TLS_POINTER = 0x58,  // TEB.ThreadLocalStoragePointer
};

typedef struct Signature {
	const char *name;
	const char *bytes;
	size_t length;
	uint32_t rva;
} Signature;

#define SIGNATURE(rva, name, bytes) {(name), (bytes), sizeof(bytes) - 1, (rva)}

// The first bytes of every function the harness calls. A mismatch means a different game build,
// or a software breakpoint (0xCC) planted by a debugger.
static const Signature signatures[] = {
	SIGNATURE(RVA_GAME_MATCH_CREATE, "GAME_MATCH__create", "\x48\x83\xEC\x28\x48\x8B\x05\xC5\x8C"),
	SIGNATURE(
		RVA_GAME_MATCH_DESERIALIZE,
		"GAME_MATCH__deserialize",
		"\x41\x56\x56\x57\x53\x48\x83\xEC\x38"
	),
	SIGNATURE(RVA_GAME_MATCH_PREPARE, "GAME_MATCH__prepare_to_play", "\xE9\xDB\x1E\x05\x10"),
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

static uint8_t *base;

static bool checkTimestamp(void);
static bool checkSignatures(void);

bool game_init(void) {
	base = (uint8_t *)GetModuleHandleW(L"fm.exe");
	if (!base) {
		log_message("not loaded into fm.exe");
		return false;
	}
	log_message("loaded into fm.exe at %p", (void *)base);
	return checkTimestamp() && checkSignatures();
}

uintptr_t game_base(void) {
	return (uintptr_t)base;
}

void *game_address(uint32_t rva) {
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
	return game_functionAt(base + rva);
}

uint32_t game_rva(const void *address) {
	uintptr_t value = (uintptr_t)address;
	uintptr_t start = (uintptr_t)base;
	return value > start && value < start + IMAGE_SPAN ? (uint32_t)(value - start) : 0;
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

static bool checkTimestamp(void) {
	const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
	const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(base + dos->e_lfanew);
	if (nt->FileHeader.TimeDateStamp != GAME_PE_TIMESTAMP) {
		log_message(
			"refusing: fm.exe timestamp %lu, expected %d (different game build)",
			nt->FileHeader.TimeDateStamp,
			GAME_PE_TIMESTAMP
		);
		return false;
	}
	return true;
}

static bool checkSignatures(void) {
	for (size_t i = 0; i < sizeof signatures / sizeof signatures[0]; ++i) {
		const Signature *signature = &signatures[i];
		if (memcmp(game_address(signature->rva), signature->bytes, signature->length) != 0) {
			log_message(
				"refusing: prologue of %s at fm.exe+0x%x does not match (software breakpoint?)",
				signature->name,
				signature->rva
			);
			return false;
		}
	}
	return true;
}
