// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "database.h"

#include "game.h"
#include "memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


enum { MAX_NAME_LENGTH = 200 };

static bool readString(const uint8_t *object, char *out, size_t size);
static bool readPersonString(const uint8_t *person, ptrdiff_t offset, char *out, size_t size);
static bool peopleRange(uint8_t ***start, uint8_t ***end);
static int comparePeople(const void *a, const void *b);

bool database_loadPeople(PeopleIndex *index) {
	*index = (PeopleIndex){0};
	uint8_t **start = NULL;
	uint8_t **end = NULL;
	if (!peopleRange(&start, &end)) {
		return false;
	}
	index->people = malloc((size_t)(end - start) * sizeof(Person));
	if (!index->people) {
		return false;
	}
	for (uint8_t **entry = start; entry < end; ++entry) {
		uint8_t *person = NULL;
		uint32_t uniqueId = 0;
		if (memory_readPointer(entry, 0, &person) &&
				memory_readU32(person, PERSON_UNIQUE_ID, &uniqueId)) {
			index->people[index->count++] = (Person){uniqueId, person};
		}
	}
	qsort(index->people, index->count, sizeof(Person), comparePeople);
	return true;
}

void database_freePeople(PeopleIndex *index) {
	free(index->people);
	*index = (PeopleIndex){0};
}

uint8_t *database_findPerson(const PeopleIndex *index, uint32_t uniqueId) {
	Person key = {uniqueId, NULL};
	const Person *found = bsearch(&key, index->people, index->count, sizeof(Person), comparePeople);
	return found ? found->address : NULL;
}

uint8_t *database_findHumanTeam(void) {
	uint8_t **start = NULL;
	uint8_t **end = NULL;
	if (!peopleRange(&start, &end)) {
		return NULL;
	}
	uintptr_t humanVtable = (uintptr_t)game_address(RVA_HUMAN_NON_PLAYER_VTABLE);
	for (uint8_t **entry = start; entry < end; ++entry) {
		uint8_t *person = NULL;
		uint8_t *vtable = NULL;
		uint8_t *contract = NULL;
		uint8_t *team = NULL;
		if (memory_readPointer(entry, 0, &person) && memory_readPointer(person, 0, &vtable) &&
				(uintptr_t)vtable == humanVtable &&
				memory_readPointer(person, PERSON_CONTRACT, &contract) &&
				memory_readPointer(contract, CONTRACT_TEAM, &team)) {
			return team; // single-player saves have exactly one human manager
		}
	}
	return NULL;
}

bool database_personName(const uint8_t *person, char *out, size_t size) {
	char forename[MAX_NAME_LENGTH + 1] = "";
	char surname[MAX_NAME_LENGTH + 1] = "";
	bool ok = readPersonString(person, PERSON_FORENAME, forename, sizeof forename) &&
						readPersonString(person, PERSON_SURNAME, surname, sizeof surname);
	int written = snprintf(out, size, "%s %s", forename, surname);
	return ok && written > 0 && (size_t)written < size;
}

void database_clubShortName(const uint8_t *team, char *out, size_t size) {
	uint8_t *club = NULL;
	uint8_t *string = NULL;
	if (!memory_readPointer(team, TEAM_CLUB, &club) ||
			!memory_readPointer(club, CLUB_NAME_SHORT, &string) || !readString(string, out, size)) {
		(void)snprintf(out, size, "?");
	}
}

uint8_t *database_playerRatings(uint8_t *person) {
	return person + PLAYER_FROM_PERSON + PLAYER_RATINGS;
}

// A game string object: u32 byte length followed by UTF-8 bytes.
static bool readString(const uint8_t *object, char *out, size_t size) {
	uint32_t length = 0;
	if (!memory_readU32(object, 0, &length) || length == 0 || length > MAX_NAME_LENGTH ||
			length >= size) {
		return false;
	}
	if (!memory_read(object + sizeof length, out, length)) {
		return false;
	}
	out[length] = 0;
	return true;
}

// Person names are doubly indirect: person+offset -> name object -> string object.
static bool readPersonString(const uint8_t *person, ptrdiff_t offset, char *out, size_t size) {
	uint8_t *nameObject = NULL;
	uint8_t *string = NULL;
	return memory_readPointer(person, offset, &nameObject) &&
				 memory_readPointer(nameObject, 0, &string) && readString(string, out, size);
}

// The people list: [fm.exe + RVA_PEOPLE_LIST] + 0x80 -> {start, end} of an array of person
// pointers.
static bool peopleRange(uint8_t ***start, uint8_t ***end) {
	uint8_t *holder = NULL;
	uint8_t *list = NULL;
	if (!memory_readPointer(game_address(RVA_PEOPLE_LIST), 0, &holder) ||
			!memory_readPointer(holder, PEOPLE_LIST_HOLDER, &list) ||
			!memory_readPointer(list, 0, (uint8_t **)start) ||
			!memory_readPointer(list, sizeof(void *), (uint8_t **)end)) {
		return false;
	}
	return *end > *start;
}

static int comparePeople(const void *a, const void *b) {
	uint32_t left = ((const Person *)a)->uniqueId;
	uint32_t right = ((const Person *)b)->uniqueId;
	return (left > right) - (left < right);
}
