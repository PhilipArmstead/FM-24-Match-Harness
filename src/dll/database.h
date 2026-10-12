// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Read access to the game's database objects (people, contracts, teams, clubs); offsets are in
// game.h. All reads are fault-tolerant (see memory.h).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


typedef struct Person {
	uint32_t uniqueId;
	uint8_t *address;
} Person;

// Every person in the database, sorted by unique ID.
typedef struct PeopleIndex {
	Person *people;
	size_t count;
} PeopleIndex;

bool database_loadPeople(PeopleIndex *index);
void database_freePeople(PeopleIndex *index);

// The person with this unique ID, or NULL.
uint8_t *database_findPerson(const PeopleIndex *index, uint32_t uniqueId);

// The first human manager's (db::HUMAN_NON_PLAYER) current team, or NULL.
uint8_t *database_findHumanTeam(void);

// "Forename Surname"; returns false (and writes "") if the names cannot be read.
bool database_personName(const uint8_t *person, char *out, size_t size);

// The short name of a team's club, or "?".
void database_clubShortName(const uint8_t *team, char *out, size_t size);

// A person's player ratings (15 positions then 54 attributes, PLAYER_RATING_COUNT bytes).
uint8_t *database_playerRatings(uint8_t *person);
