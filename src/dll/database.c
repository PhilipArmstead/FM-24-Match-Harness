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

// The distinct clubs of every team found by the scan (unsorted, with repeats until deduplicated).
typedef struct ClubList {
	uint8_t **clubs;
	size_t count;
	size_t capacity;
	bool failed; // out of memory
} ClubList;

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
static void collectClub(void *context, uint8_t *team);
static int comparePointers(const void *a, const void *b);
static bool appendTeam(ByteBuffer *out, const uint8_t *team);
static bool appendPlayer(ByteBuffer *out, const uint8_t *person);
static void spaceControls(char *text);

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

uint8_t *database_playerRecord(const uint8_t *person) {
	// MSVC RTTI: vtable[-1] is the complete object locator: u32 signature (1 on x64), u32 offset of
	// this subobject in the complete object, u32 constructor displacement offset, u32 RVA of the
	// class's type descriptor, whose decorated name starts at +0x10.
	static const char *const playerClasses[] = {
		".?AVACTUAL_PLAYER@db@@",
		".?AVACTUAL_PLAYER_AND_NON_PLAYER@db@@",
	};
	uint8_t *vtable = NULL;
	uint8_t *locator = NULL;
	uint32_t header[4] = {0};
	char name[48] = "";
	if (!memory_readPointer(person, 0, &vtable) || !memory_readPointer(vtable, -8, &locator) ||
			!memory_read(locator, header, sizeof header) || header[0] != 1 ||
			!memory_read((uint8_t *)game_address(header[3]) + 0x10, name, sizeof name - 1)) {
		return NULL;
	}
	for (size_t i = 0; i < sizeof playerClasses / sizeof playerClasses[0]; ++i) {
		if (strcmp(name, playerClasses[i]) == 0) {
			return (uint8_t *)person - header[1];
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

bool database_listTeams(ByteBuffer *out, size_t *count) {
	*count = 0;
	ClubList list = {0};
	memory_scanForQword((uintptr_t)game_address(RVA_TEAM_VTABLE), collectClub, &list);
	if (list.count) {
		qsort(list.clubs, list.count, sizeof list.clubs[0], comparePointers);
	}
	bool ok = !list.failed;
	for (size_t i = 0; ok && i < list.count; ++i) {
		uint8_t *team = NULL;
		if ((i && list.clubs[i] == list.clubs[i - 1]) || !firstTeam(list.clubs[i], &team)) {
			continue;
		}
		ok = appendTeam(out, team);
		*count += ok;
	}
	free(list.clubs);
	return ok;
}

bool database_listPlayers(ByteBuffer *out, size_t *count) {
	*count = 0;
	uint8_t **start = NULL;
	uint8_t **end = NULL;
	if (!peopleRange(&start, &end)) {
		return true;
	}
	for (uint8_t **entry = start; entry < end; ++entry) {
		uint8_t *person = NULL;
		if (!memory_readPointer(entry, 0, &person) || !database_playerRecord(person)) {
			continue;
		}
		if (!appendPlayer(out, person)) {
			return false;
		}
		++*count;
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

static void collectClub(void *context, uint8_t *team) {
	ClubList *list = context;
	uint8_t *club = NULL;
	if (list->failed || !memory_readPointer(team, TEAM_CLUB, &club) || !club) {
		return;
	}
	if (list->count == list->capacity) {
		size_t grown = list->capacity ? list->capacity * 2 : 4096;
		uint8_t **clubs = realloc(list->clubs, grown * sizeof *clubs);
		if (!clubs) {
			list->failed = true;
			return;
		}
		list->clubs = clubs;
		list->capacity = grown;
	}
	list->clubs[list->count++] = club;
}

static int comparePointers(const void *a, const void *b) {
	uintptr_t left = (uintptr_t)*(uint8_t *const *)a;
	uintptr_t right = (uintptr_t)*(uint8_t *const *)b;
	return (left > right) - (left < right);
}

// One "uniqueId<TAB>name" line; control characters in the name become spaces.
static bool appendTeam(ByteBuffer *out, const uint8_t *team) {
	char name[MAX_NAME_LENGTH + 1];
	database_clubShortName(team, name, sizeof name);
	spaceControls(name);
	char line[MAX_NAME_LENGTH + 32];
	int length = snprintf(line, sizeof line, "%u\t%s\n", database_teamUniqueId(team), name);
	return length > 0 && buffer_append(out, line, (size_t)length);
}

// One "uniqueId<TAB>forename<TAB>surname<TAB>common name<TAB>club" line; a name that cannot be read
// (as most common names are absent) is empty, and control characters become spaces.
static bool appendPlayer(ByteBuffer *out, const uint8_t *person) {
	static const ptrdiff_t nameOffsets[3] = {PERSON_FORENAME, PERSON_SURNAME, PERSON_COMMON_NAME};
	char names[4][MAX_NAME_LENGTH + 1] = {"", "", "", ""};
	for (size_t i = 0; i < 3; ++i) {
		if (!readPersonString(person, nameOffsets[i], names[i], sizeof names[i])) {
			names[i][0] = 0;
		}
	}
	uint8_t *contract = NULL;
	uint8_t *team = NULL;
	if (memory_readPointer(person, PERSON_CONTRACT, &contract) &&
			memory_readPointer(contract, CONTRACT_TEAM, &team)) {
		database_clubShortName(team, names[3], sizeof names[3]);
	}
	if (strcmp(names[3], "?") == 0) {
		names[3][0] = 0;
	}
	for (size_t i = 0; i < 4; ++i) {
		spaceControls(names[i]);
	}
	uint32_t uniqueId = 0;
	(void)memory_readU32(person, PERSON_UNIQUE_ID, &uniqueId);
	char line[4 * (MAX_NAME_LENGTH + 1) + 32];
	int length = snprintf(
		line, sizeof line, "%u\t%s\t%s\t%s\t%s\n", uniqueId, names[0], names[1], names[2], names[3]
	);
	return length > 0 && (size_t)length < sizeof line && buffer_append(out, line, (size_t)length);
}

// Tabs, newlines and other control characters become spaces, keeping the lines' fields apart.
static void spaceControls(char *text) {
	for (char *c = text; *c; ++c) {
		if ((unsigned char)*c < ' ') {
			*c = ' ';
		}
	}
}
