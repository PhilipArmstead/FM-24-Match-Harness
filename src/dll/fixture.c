// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixture.h"

#include "database.h"
#include "game.h"
#include "memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>


enum {
	SCAN_CHUNK = 1 << 20,
	DAY_MASK = 0x1FF,
	TIME_SHIFT = 9,
	TIME_MASK = 0x7F,
	QUARTER_HOUR = 15,
	FIRST_SLOT_HOUR = 6, // time codes count quarter hours from 06:00, plus one
};

typedef struct Search {
	uintptr_t fixtureVtable;
	const uint8_t *team;
	uint64_t now;
	uint8_t *best;
	uint64_t bestKey;
} Search;

static const uintptr_t userSpaceEnd = 0x7FFFFFFF0000ULL;

static uint64_t dateKey(uint16_t dayWord, uint16_t year);
static void considerFixture(Search *search, uint8_t *fixture);
static void scanRegion(Search *search, uint8_t *base, size_t size, uint8_t *chunk);
static void scanHeap(Search *search);
static void describe(NextFixture *next, uint64_t key);

bool fixture_findNext(NextFixture *next, const char **error) {
	*next = (NextFixture){0};
	Search search = {
		.fixtureVtable = (uintptr_t)game_address(RVA_FIXTURE_VTABLE), .bestKey = UINT64_MAX};
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
	scanHeap(&search);
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

static void considerFixture(Search *search, uint8_t *fixture) {
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

// Scans one committed region in chunks for qwords equal to the fixture vtable.
static void scanRegion(Search *search, uint8_t *base, size_t size, uint8_t *chunk) {
	for (size_t offset = 0; offset < size; offset += SCAN_CHUNK) {
		size_t length = size - offset < SCAN_CHUNK ? size - offset : SCAN_CHUNK;
		if (!memory_read(base + offset, chunk, length)) {
			continue;
		}
		for (size_t i = 0; i + sizeof(uintptr_t) <= length; i += sizeof(uintptr_t)) {
			uintptr_t value = 0;
			memcpy(&value, chunk + i, sizeof value);
			if (value == search->fixtureVtable) {
				considerFixture(search, base + offset + i);
			}
		}
	}
}

// Walks the process's private read/write memory looking for sicomps::FIXTURE objects.
static void scanHeap(Search *search) {
	uint8_t *chunk = malloc(SCAN_CHUNK);
	if (!chunk) {
		return;
	}
	MEMORY_BASIC_INFORMATION info;
	for (uint8_t *address = NULL; VirtualQuery(address, &info, sizeof info);
		address = (uint8_t *)info.BaseAddress + info.RegionSize) {
		if (info.State == MEM_COMMIT && info.Type == MEM_PRIVATE && info.Protect == PAGE_READWRITE) {
			scanRegion(search, info.BaseAddress, info.RegionSize, chunk);
		}
		if ((uintptr_t)info.BaseAddress + info.RegionSize >= userSpaceEnd) {
			break;
		}
	}
	free(chunk);
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
