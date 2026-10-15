// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


enum { FIXTURE_TEXT = 512, FIXTURE_PLAYERS = 256, FIXTURE_SLOTS = 11 };

typedef struct FixtureNumber {
	uint64_t value;
	bool known;
} FixtureNumber;

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
	char officials[5][FIXTURE_TEXT]; // official[0..3], then referee
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

void fixtureData_position(uint64_t mask, char *out, size_t size);
const char *fixtureData_role(uint64_t mask);
const char *fixtureData_duty(uint64_t mask);
