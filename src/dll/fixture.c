// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixture.h"

#include "database.h"
#include "game.h"
#include "memory.h"

#include <stdio.h>


enum {
	DAY_MASK = 0x1FF,
	TIME_SHIFT = 9,
	TIME_MASK = 0x7F,
	QUARTER_HOUR = 15,
	FIRST_SLOT_HOUR = 6, // time codes count quarter hours from 06:00, plus one
};

typedef struct Search {
	const uint8_t *team;
	uint64_t now;
	uint8_t *best;
	uint64_t bestKey;
} Search;

static uint64_t dateKey(uint16_t dayWord, uint16_t year);
static void considerFixture(void *context, uint8_t *fixture);
static void describe(NextFixture *next, uint64_t key);

bool fixture_findNext(NextFixture *next, const char **error) {
	*next = (NextFixture){0};
	Search search = {.bestKey = UINT64_MAX};
	search.team = database_findHumanTeam();
	if (!search.team) {
		*error = "no human manager/team found";
		return false;
	}
	uint16_t dayWord = 0;
	uint16_t year = 0;
	(void)memory_readU16(game_address(RVA_CURRENT_DATE), 0, &dayWord);
	(void)memory_readU16(game_address(RVA_CURRENT_DATE), 2, &year);
	search.now = dateKey(dayWord, year);
	memory_scanForQword((uintptr_t)game_address(RVA_FIXTURE_VTABLE), considerFixture, &search);
	if (!search.best) {
		*error = "no upcoming fixture";
		return false;
	}
	next->fixture = search.best;
	describe(next, search.bestKey);
	return true;
}

// Orders dates: year, then day of year, then kick-off time code.
static uint64_t dateKey(uint16_t dayWord, uint16_t year) {
	return ((uint64_t)year << 32) | ((uint64_t)(dayWord & DAY_MASK) << 16) |
				 (uint64_t)(dayWord >> TIME_SHIFT);
}

static void considerFixture(void *context, uint8_t *fixture) {
	Search *search = context;
	uint8_t *home = NULL;
	uint8_t *away = NULL;
	uint16_t dayWord = 0;
	uint16_t year = 0;
	if (!memory_readPointer(fixture, FIXTURE_HOME, &home) ||
			!memory_readPointer(fixture, FIXTURE_AWAY, &away) ||
			(home != search->team && away != search->team) ||
			!memory_readU16(fixture, FIXTURE_DATE, &dayWord) ||
			!memory_readU16(fixture, FIXTURE_DATE + 2, &year)) {
		return;
	}
	uint64_t key = dateKey(dayWord, year);
	if (key >= search->now && key < search->bestKey) {
		search->bestKey = key;
		search->best = fixture;
	}
}

static void describe(NextFixture *next, uint64_t key) {
	uint8_t *home = NULL;
	uint8_t *away = NULL;
	char homeName[64];
	char awayName[64];
	(void)memory_readPointer(next->fixture, FIXTURE_HOME, &home);
	(void)memory_readPointer(next->fixture, FIXTURE_AWAY, &away);
	database_clubShortName(home, homeName, sizeof homeName);
	database_clubShortName(away, awayName, sizeof awayName);
	(void)snprintf(next->description, sizeof next->description, "%s vs %s", homeName, awayName);

	unsigned year = (unsigned)(key >> 32);
	unsigned day = (unsigned)(key >> 16) & DAY_MASK;
	unsigned timeCode = (unsigned)key & TIME_MASK;
	unsigned minutes = timeCode ? FIRST_SLOT_HOUR * 60 + (timeCode - 1) * QUARTER_HOUR : 0;
	(void)snprintf(
		next->when,
		sizeof next->when,
		"%u day %u %02u:%02u",
		year,
		day,
		(minutes / 60) % 24,
		minutes % 60
	);
}
