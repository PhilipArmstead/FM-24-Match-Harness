// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "buffer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


enum { FIXTURE_TEXT = 512, FIXTURE_PLAYERS = 256, FIXTURE_SLOTS = 11 };

typedef struct FixtureNumber {
	uint64_t value;
	bool known;
} FixtureNumber;

enum {
	VENUE_WIDTH,
	VENUE_LENGTH,
	VENUE_LONGITUDE,
	VENUE_LATITUDE,
	VENUE_CAPACITY,
	VENUE_SEATS,
	VENUE_HOME_FANS,
	VENUE_AWAY_FANS,
	VENUE_SEASON_TICKETS,
	VENUE_SURFACE,
	VENUE_CONDITION,
	VENUE_FIELD_COUNT,
};

typedef struct FixturePlayer {
	char name[FIXTURE_TEXT];
	bool present;
	FixtureNumber morale;
	FixtureNumber condition;
	FixtureNumber sharpness[2];
	FixtureNumber fatigue;
} FixturePlayer;

typedef struct FixtureSlot {
	FixtureNumber position;
	FixtureNumber role;
} FixtureSlot;

typedef struct FixtureTeam {
	char name[FIXTURE_TEXT];
	char tactic[FIXTURE_TEXT];
	char formation[64];
	FixtureNumber managerFlags;
	FixtureNumber playerCount;
	FixtureSlot slots[FIXTURE_SLOTS];
	FixturePlayer players[FIXTURE_PLAYERS];
	bool lineupKnown;
} FixtureTeam;

typedef struct FixtureData {
	char competition[FIXTURE_TEXT];
	char matchType[64];
	char venue[FIXTURE_TEXT];
	char source[FIXTURE_TEXT];
	char when[128];
	char officials[5][FIXTURE_TEXT];              // official[0..3], then referee
	FixtureNumber venueValues[VENUE_FIELD_COUNT]; // floats retain their serialised u32 bits
	FixtureTeam teams[2];
} FixtureData;

// No game or Windows dependencies. Reads numeric values, never the advisory "flags" strings.
bool fixtureData_read(
	const char *json,
	size_t length,
	FixtureData *out,
	char *error,
	size_t errorSize
);

float fixtureData_toFloat(uint32_t bits);
uint32_t fixtureData_fromFloat(float value);
bool fixtureData_validateVenue(
	const FixtureNumber values[VENUE_FIELD_COUNT],
	char *error,
	size_t errorSize
);
// Patches numeric tokens only, retaining all other fields, annotations and replacement-team IDs.
bool fixtureData_writeVenue(
	const char *json,
	size_t length,
	const FixtureNumber values[VENUE_FIELD_COUNT],
	ByteBuffer *out,
	char *error,
	size_t errorSize
);

void fixtureData_position(uint64_t mask, char *out, size_t size);
const char *fixtureData_role(uint64_t mask);
const char *fixtureData_duty(uint64_t mask);
