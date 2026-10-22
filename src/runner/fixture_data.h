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

// A player's tactical familiarity with one tactic setting category, as {setting, amount} entries
// (docs/FIXTURE.md#tactical-familiarity). Categories, in file order: mentality, passing style,
// creative freedom, trigger press, marking, tempo, width.
enum { FIXTURE_FAMILIARITY_CATEGORIES = 7, FIXTURE_FAMILIARITY_ENTRIES = 24 };

typedef struct FixtureFamiliarity {
	bool known; // its count was read and every entry fits
	uint8_t count;
	int8_t setting[FIXTURE_FAMILIARITY_ENTRIES];  // 1-20 scale
	uint16_t amount[FIXTURE_FAMILIARITY_ENTRIES]; // 0-10000
} FixtureFamiliarity;

typedef struct FixturePlayer {
	char name[FIXTURE_TEXT];
	bool present;
	FixtureNumber morale;
	FixtureNumber condition;
	FixtureNumber sharpness[2];
	FixtureNumber fatigue;
	FixtureFamiliarity familiarity[FIXTURE_FAMILIARITY_CATEGORIES];
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
	FixtureNumber mentality;       // plan[0]
	FixtureNumber planByte18;      // plan[0]'s unnamed u8 before the mentality: creative freedom
	FixtureNumber instructions[3]; // plan[0] team instructions
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

enum { FIXTURE_MAX_HAPPINESS = 1, FIXTURE_MAX_CONDITION = 2, FIXTURE_MAX_FAMILIARITY = 4 };

typedef struct FixturePlayerEdits {
	uint8_t players[2][FIXTURE_PLAYERS];
} FixturePlayerEdits;

bool fixtureData_hasPlayerEdits(const FixturePlayerEdits *edits);
bool fixtureData_writePlayers(
	const char *json,
	size_t length,
	const FixturePlayerEdits *edits,
	ByteBuffer *out,
	char *error,
	size_t errorSize
);

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

// The tactic's setting (1-20 scale) for each familiarity category, as the game works it out from
// plan[0]'s mentality, team instructions and creative freedom byte. False if any is missing.
bool fixtureData_familiaritySettings(
	const FixtureTeam *team,
	int settings[FIXTURE_FAMILIARITY_CATEGORIES]
);
// How familiar the player is with one category of the team's tactic, 0-10000, as the game's tactic
// familiarity panel shows it. False if the player's or the tactic's values are missing.
bool fixtureData_familiarity(
	const FixtureTeam *team,
	const FixturePlayer *player,
	unsigned category,
	unsigned *value
);

void fixtureData_position(uint64_t mask, char *out, size_t size);
const char *fixtureData_role(uint64_t mask);
const char *fixtureData_duty(uint64_t mask);
