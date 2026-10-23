// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Read access to the game's database objects (people, contracts, teams, clubs); offsets are in
// game.h. All reads are fault-tolerant (see memory.h).
#pragma once

#include "buffer.h"

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

// The player record (db::ACTUAL_PLAYER or db::ACTUAL_PLAYER_AND_NON_PLAYER) that `person` is part
// of, found through the object's RTTI, or NULL if the person is not a player.
uint8_t *database_playerRecord(const uint8_t *person);

// "Forename Surname"; returns false (and writes "") if the names cannot be read.
bool database_personName(const uint8_t *person, char *out, size_t size);

// The common name ("Diego Carlos"); returns false (and writes "") if the person has none.
bool database_personCommonName(const uint8_t *person, char *out, size_t size);

// The short name of a team's club, or "?".
void database_clubShortName(const uint8_t *team, char *out, size_t size);

// A team (db::TEAM) named by `spec`: its unique ID in decimal, or its club's short name (case is
// ignored; a part of the name will do if no club's name matches in full), which picks the club's
// first team. On failure, `error` says why (for an ambiguous name it lists the clubs).
bool database_findTeam(const char *spec, uint8_t **team, char *error, size_t errorSize);

// Every club's first team as "uniqueId<TAB>short name" lines (the specs database_findTeam
// accepts); `count` is the number of lines. Returns false if memory runs out.
bool database_listTeams(ByteBuffer *out, size_t *count);

// Every player (database_playerRecord) as "uniqueId<TAB>forename<TAB>surname<TAB>common
// name<TAB>club short name" lines, the last two empty when the player has none; `count` is the
// number of lines. Returns false if memory runs out.
bool database_listPlayers(ByteBuffer *out, size_t *count);

// A team's unique ID, or 0 if it cannot be read.
uint32_t database_teamUniqueId(const uint8_t *team);
