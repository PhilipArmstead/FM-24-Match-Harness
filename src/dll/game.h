// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Everything the harness knows about fm.exe (Football Manager 24, build 24.2): code addresses,
// object layouts and the signatures of the game functions it calls. Addresses are RVAs
// (offsets from the module base); the image has no ASLR, so the base is
// always 0x140000000, but nothing here relies on that.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// PE timestamp of the only build these addresses are valid for.
enum { GAME_PE_TIMESTAMP = 1744804480 };

// Game functions.
enum GameFunction {
	RVA_GAME_MATCH_CREATE = 0x1A8FAF0,
	RVA_GAME_MATCH_DESERIALIZE = 0x1AC7840,
	RVA_GAME_MATCH_PREPARE = 0x1A92070, // jmp thunk into the relocated (.arch) implementation
	RVA_GAME_MATCH_RUN = 0x1A98930,
	RVA_GAME_MATCH_SERIALIZE = 0x1AC76A0,
	RVA_GAME_MATCH_SERIALIZE_END = 0x1AC7837,
	RVA_DATA_BUFFER_CTOR = 0x19253A0,
	RVA_ALLOC_FROM_POOL = 0x34F81D0,
	RVA_MATCH_CTOR = 0x1A8FC30,
	RVA_FIXTURE_PREPARE_TEAMS = 0x34F3610,
	RVA_INIT_FROM_FIXTURE = 0x3AB8350,
	RVA_GAME_MATCH_DTOR = 0x1A90D30,
	RVA_GAME_MATCH_POOL_FREE = 0x34F82E0,
	RVA_TLS_ON_DEMAND_INIT = 0x54E8E38, // TlsCallback_0(0, DLL_THREAD_ATTACH)
};

// Game data.
enum GameData {
	RVA_GAME_MATCH_VTABLE = 0x56083C8,
	RVA_HUMAN_NON_PLAYER_VTABLE = 0x5A6A388, // db::HUMAN_NON_PLAYER
	RVA_FIXTURE_VTABLE = 0x5A25F58,          // sicomps::FIXTURE
	RVA_PEOPLE_LIST = 0x642ED38,             // [p] + 0x80 -> {start, end}
	RVA_CURRENT_DATE = 0x631D5BC,            // u16 day | time << 9, u16 year
	RVA_TLS_INDEX = 0x638EEC0,               // fm.exe's _tls_index
};

// GAME_MATCH layout.
enum MatchLayout {
	MATCH_SEED = 32,          // u32
	MATCH_CLOCK = 2536,       // u32, 4 ticks per second
	MATCH_FLAGS = 2724,       // u32, bit 2 = finished
	MATCH_TEAM_HOME = 52728,  // MATCH_TEAM *
	MATCH_TEAM_AWAY = 52736,  // MATCH_TEAM *
	MATCH_RNG_STATE = 58320,  // u64
	MATCH_BUILD_FLAG = 58928, // byte MatchBuilder_Create clears after the constructor
	MATCH_TEAM_GOALS = 0x248, // u8 in MATCH_TEAM
	MATCH_TEAM_MANAGER =
		312,                // MATCH_TEAM: the manager (its tactic etc.; "<side>.manager" in a config)
	MANAGER_FLAGS = 7600, // u32 in the manager: the first field of "<side>.manager"
	MANAGER_HUMAN_IN_CONTROL =
		0x1, // a human makes the team's in-match changes (docs/PLAN.md Phase 18)
};

// Database layouts.
enum DatabaseLayout {
	PERSON_UNIQUE_ID = 0x0C,
	PERSON_FORENAME = 0x58, // doubly indirect string
	PERSON_SURNAME = 0x60,  // doubly indirect string
	PERSON_CONTRACT = 0xC8,
	PLAYER_FROM_PERSON = -0x278,
	PLAYER_RATINGS = 0x208, // 15 positions (1-20) + 54 attributes (1-100)
	CONTRACT_TEAM = 0x10,
	TEAM_CLUB = 0x30,
	CLUB_NAME_SHORT = 0xC8, // singly indirect string
	FIXTURE_HOME = 0x08,
	FIXTURE_AWAY = 0x10,
	FIXTURE_DATE = 0x4C, // u16 day | time << 9, u16 year
	PEOPLE_LIST_HOLDER = 0x80,
};

// Thread-local PCG32 the pre-match setup uses when no RNG is passed in.
enum ThreadRngLayout {
	TLS_INIT_FLAG = 60, // u8, set once the thread's TLS is initialised
	TLS_PCG = 352,      // object: +8 state, +16 increment
};

enum { PLAYER_RATING_COUNT = 69 };

// Second argument of GAME_MATCH__init_from_fixture, as laid out on MatchBuilder_Create's stack.
typedef struct InitArgs {
	uint8_t *fixture;
	uint32_t seed;
	uint32_t padding0;
	void *scratch; // 16 bytes filled by fixture_prepare_teams
	uint8_t mode;
	uint8_t padding1[7];
	uint64_t zero1;
	uint64_t zero2;
	uint8_t zero3;
	uint8_t padding2[7];
} InitArgs;

typedef uint8_t *(*MatchCreateFn)(void);
typedef char (*MatchStreamFn)(uint8_t *match, void **streamHandle, unsigned flags);
typedef void (*MatchPrepareFn)(uint8_t *match, char unknown);
typedef int (*MatchRunFn)(uint8_t *match, unsigned mode, int untilTick);
typedef void (*MatchFn)(uint8_t *match);
typedef void (*MatchDeleteFn)(uint8_t *match, int flags);
typedef void (*FixturePrepareFn)(uint8_t *fixture, void *scratch);
typedef void (*MatchInitFn)(uint8_t *match, InitArgs *args, uint8_t *flags);
typedef void *(*DataBufferCtorFn)(void *self);
typedef void (*VoidFn)(void);

// Finds fm.exe in this process and checks it is the expected build; logs the reason if not.
bool game_init(void);

uintptr_t game_base(void);

// The address of an RVA in the loaded image.
void *game_address(uint32_t rva);

// A game function at an RVA, as a generic function pointer; cast it to the function's type.
VoidFn game_function(uint32_t rva);

// The same conversion for a code address read from memory (e.g. a vtable slot).
VoidFn game_functionAt(const void *address);

// The RVA of an address inside fm.exe, or 0 if it is outside the image.
uint32_t game_rva(const void *address);

// Runs the calling thread's thread_local constructors in fm.exe if the game has not done so yet
// (every thread that calls into the engine needs them); returns the thread's TLS block.
uint8_t *game_initThread(void);
