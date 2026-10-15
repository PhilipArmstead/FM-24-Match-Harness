// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "database.h"

#include "game.h"
#include "memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


enum { MAX_NAME_LENGTH = 200, MAX_CLUBS_LISTED = 8 };

// Teams found by a heap scan for the db::TEAM vtable (freed pool entries lose their vtable).
typedef struct TeamSearch {
	bool byId;
	uint32_t uniqueId;
	const char *name;
	uint8_t *team;                    // byId: the match
	uint8_t *exact[MAX_CLUBS_LISTED]; // by name: distinct clubs whose name matches in full
	size_t exactCount;
	uint8_t *partial[MAX_CLUBS_LISTED]; // ... or contains the name
	size_t partialCount;
} TeamSearch;

static bool readString(const uint8_t *object, char *out, size_t size);
static bool readPersonString(const uint8_t *person, ptrdiff_t offset, char *out, size_t size);
static bool peopleRange(uint8_t ***start, uint8_t ***end);
static int comparePeople(const void *a, const void *b);
static bool parseUniqueId(const char *spec, uint32_t *uniqueId);
static bool containsIgnoringCase(const char *text, const char *part);
static void addClub(uint8_t **clubs, size_t *count, uint8_t *club);
static void considerTeam(void *context, uint8_t *team);
static bool firstTeam(const uint8_t *club, uint8_t **team);
static void listClubs(uint8_t *const *clubs, size_t count, char *error, size_t errorSize);

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

bool database_findTeam(const char *spec, uint8_t **team, char *error, size_t errorSize) {
	*team = NULL;
	TeamSearch search = {.name = spec};
	search.byId = parseUniqueId(spec, &search.uniqueId);
	memory_scanForQword((uintptr_t)game_address(RVA_TEAM_VTABLE), considerTeam, &search);
	if (search.byId) {
		*team = search.team;
		if (!*team) {
			(void)snprintf(error, errorSize, "no team with unique ID %u", search.uniqueId);
		}
		return *team != NULL;
	}
	bool exact = search.exactCount > 0;
	uint8_t **clubs = exact ? search.exact : search.partial;
	size_t count = exact ? search.exactCount : search.partialCount;
	if (count == 0) {
		(void)snprintf(error, errorSize, "no club is called \"%s\"", spec);
		return false;
	}
	if (count > 1) {
		listClubs(clubs, count, error, errorSize);
		return false;
	}
	if (!firstTeam(clubs[0], team)) {
		(void)snprintf(error, errorSize, "cannot read the club's first team");
		return false;
	}
	return true;
}

uint32_t database_teamUniqueId(const uint8_t *team) {
	uint32_t uniqueId = 0;
	(void)memory_readU32(team, TEAM_UNIQUE_ID, &uniqueId);
	return uniqueId;
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

// A decimal unique ID (digits only).
static bool parseUniqueId(const char *spec, uint32_t *uniqueId) {
	if (!*spec || strspn(spec, "0123456789") != strlen(spec)) {
		return false;
	}
	unsigned long long value = strtoull(spec, NULL, 10);
	*uniqueId = (uint32_t)value;
	return value <= UINT32_MAX;
}

// ASCII case-insensitive substring test (other UTF-8 bytes must match exactly).
static bool containsIgnoringCase(const char *text, const char *part) {
	size_t length = strlen(part);
	for (const char *start = text; *start; ++start) {
		if (_strnicmp(start, part, length) == 0) {
			return true;
		}
	}
	return length == 0;
}

static void addClub(uint8_t **clubs, size_t *count, uint8_t *club) {
	for (size_t i = 0; i < *count && i < MAX_CLUBS_LISTED; ++i) {
		if (clubs[i] == club) {
			return;
		}
	}
	if (*count < MAX_CLUBS_LISTED) {
		clubs[*count] = club;
	}
	++*count;
}

static void considerTeam(void *context, uint8_t *team) {
	TeamSearch *search = context;
	uint8_t *club = NULL;
	if (!memory_readPointer(team, TEAM_CLUB, &club)) {
		return;
	}
	if (search->byId) {
		if (!search->team && database_teamUniqueId(team) == search->uniqueId) {
			search->team = team;
		}
		return;
	}
	char name[MAX_NAME_LENGTH + 1];
	database_clubShortName(team, name, sizeof name);
	if (_stricmp(name, search->name) == 0) {
		addClub(search->exact, &search->exactCount, club);
	} else if (containsIgnoringCase(name, search->name)) {
		addClub(search->partial, &search->partialCount, club);
	}
}

// The first of a club's teams (the senior side), checked to be a db::TEAM.
static bool firstTeam(const uint8_t *club, uint8_t **team) {
	uint8_t *teams = NULL;
	uint8_t *vtable = NULL;
	return memory_readPointer(club, CLUB_TEAMS, &teams) && memory_readPointer(teams, 0, team) &&
				 memory_readPointer(*team, 0, &vtable) &&
				 (uintptr_t)vtable == (uintptr_t)game_address(RVA_TEAM_VTABLE);
}

// "N clubs match: Name (ID), ...", for an ambiguous name.
static void listClubs(uint8_t *const *clubs, size_t count, char *error, size_t errorSize) {
	int written = snprintf(error, errorSize, "%zu clubs match; give a team ID:", count);
	size_t length = written > 0 ? (size_t)written : 0;
	size_t listed = count < MAX_CLUBS_LISTED ? count : MAX_CLUBS_LISTED;
	for (size_t i = 0; i < listed && length < errorSize; ++i) {
		uint8_t *team = NULL;
		char name[MAX_NAME_LENGTH + 1];
		if (!firstTeam(clubs[i], &team)) {
			continue;
		}
		database_clubShortName(team, name, sizeof name);
		written = snprintf(
			error + length,
			errorSize - length,
			"%s %s (%u)",
			i ? "," : "",
			name,
			database_teamUniqueId(team)
		);
		length += written > 0 ? (size_t)written : 0;
	}
	if (count > listed && length < errorSize) {
		(void)snprintf(error + length, errorSize - length, ", ...");
	}
}
