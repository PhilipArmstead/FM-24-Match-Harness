// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"
#include "tests.h"

#include <stdlib.h>


static int traceAllocationsUntilFailure = -1;
static void *traceRealloc(void *pointer, size_t size);

#define realloc traceRealloc
#include "../src/dll/trace.c" // NOLINT(bugprone-suspicious-include): inject trace allocation failures.
#undef realloc

#include "../src/dll/config_writer.c" // NOLINT(bugprone-suspicious-include): exercise UTF-8 recognition.
#include "../src/dll/labels.c" // NOLINT(bugprone-suspicious-include): exercise label walk state.
#include "../src/dll/match_stats.c" // NOLINT(bugprone-suspicious-include): use synthetic engine objects.
#include "../src/dll/seed_build.c" // NOLINT(bugprone-suspicious-include): exercise record alignment.


static ByteBuffer engineOutput;
static SharedBuffer engineShared;
static DataBuffer engineStream;
static void *engineVtable[VTABLE_SLOTS + 1];
static void **engineOutputVtable;
static uint32_t builtSeed;
static bool usedTrace;
static bool hasFixture;
static unsigned serialisations;
static unsigned tracedSerialisations;
static uint8_t *humanMatchesHolder;
static const uint8_t *expectedBuildTeams[2];
static bool failBuild;

static void *traceRealloc(void *pointer, size_t size) {
	if (traceAllocationsUntilFailure == 0) {
		return NULL;
	}
	if (traceAllocationsUntilFailure > 0) {
		--traceAllocationsUntilFailure;
	}
	return realloc(pointer, size);
}

static int64_t engineWrite(void *self, const void *bytes, int64_t length) {
	(void)self;
	if (length < 0 || !buffer_append(&engineOutput, bytes, (size_t)length)) {
		return -1;
	}
	return length;
}

// NOLINTBEGIN(readability-non-const-parameter): test doubles retain the game's signatures.
static char engineSerialise(uint8_t *match, void **handle, unsigned flags) {
	(void)match;
	(void)flags;
	void **vtable = *(void ***)*handle;
	usedTrace = vtable != &engineVtable[1];
	++serialisations;
	tracedSerialisations += usedTrace;
	StreamWriteFn write = NULL;
	memcpy(&write, &vtable[WRITE_SLOT], sizeof write);
	return (char)(write(*handle, &builtSeed, sizeof builtSeed) == sizeof builtSeed);
}

VoidFn game_function(uint32_t rva) {
	ASSERT_INT_EQ((int)rva, RVA_GAME_MATCH_SERIALIZE);
	MatchStreamFn serialise = engineSerialise;
	VoidFn function = NULL;
	memcpy(&function, &serialise, sizeof function);
	return function;
}

VoidFn game_functionAt(const void *address) {
	VoidFn function = NULL;
	memcpy(&function, &address, sizeof function);
	return function;
}

void *game_address(uint32_t rva) {
	if (rva == RVA_HUMAN_MATCHES_HOLDER) {
		return &humanMatchesHolder;
	}
	return (void *)(uintptr_t)rva; // NOLINT(performance-no-int-to-ptr): synthetic engine address.
}

uint32_t game_rva(const void *address) {
	uintptr_t value = (uintptr_t)address;
	return value <= UINT32_MAX ? (uint32_t)value : 0;
}

uint32_t game_timestamp(void) {
	return GAME_PE_TIMESTAMP_STEAM;
}

DataBuffer *stream_create(void) {
	StreamWriteFn write = engineWrite;
	memcpy(&engineVtable[1 + WRITE_SLOT], &write, sizeof write);
	engineOutputVtable = &engineVtable[1];
	engineStream.shared = &engineShared;
	engineStream.output = &engineOutputVtable;
	return &engineStream;
}

void stream_resetOutput(DataBuffer *buffer) {
	(void)buffer;
	buffer_clear(&engineOutput);
}

const uint8_t *stream_outputData(const DataBuffer *buffer) {
	(void)buffer;
	return engineOutput.data;
}

size_t stream_outputSize(const DataBuffer *buffer) {
	(void)buffer;
	return engineOutput.length;
}

uint8_t *match_build(uint8_t *fixture, uint32_t seed) {
	for (unsigned side = 0; side < 2; ++side) {
		if (expectedBuildTeams[side]) {
			uint8_t *team = NULL;
			ASSERT_BOOL_EQ(memory_readPointer(fixture, side ? FIXTURE_AWAY : FIXTURE_HOME, &team), true);
			ASSERT_BOOL_EQ(team == expectedBuildTeams[side], true);
		}
	}
	builtSeed = seed;
	return failBuild ? NULL : (uint8_t *)&builtSeed;
}

void match_freeBuilt(uint8_t *match) {
	(void)match;
}

uint8_t *match_deserialize(ByteBuffer *bytes) {
	ASSERT_BOOL_EQ(bytes->length >= sizeof builtSeed, true);
	if (bytes->length < sizeof builtSeed) {
		return NULL;
	}
	memcpy(&builtSeed, bytes->data, sizeof builtSeed);
	return (uint8_t *)&builtSeed;
}

void match_delete(uint8_t *match) {
	(void)match;
}

bool match_seedOffset(const uint8_t *bytes, size_t size, size_t *offset) {
	(void)bytes;
	(void)size;
	*offset = 0;
	return false;
}

bool fixture_findNext(NextFixture *next, const char **error) {
	if (!hasFixture) {
		*error = "no test fixture";
		return false;
	}
	*next = (NextFixture){.fixture = (uint8_t *)&builtSeed};
	return true;
}

bool database_loadPeople(PeopleIndex *index) {
	*index = (PeopleIndex){0};
	return true;
}

void database_freePeople(PeopleIndex *index) {
	(void)index;
}

uint8_t *database_findPerson(const PeopleIndex *index, uint32_t uniqueId) {
	(void)index;
	(void)uniqueId;
	return NULL;
}

bool database_personName(const uint8_t *person, char *out, size_t size) {
	(void)person;
	(void)snprintf(out, size, "player");
	return true;
}

void database_clubShortName(const uint8_t *team, char *out, size_t size) {
	(void)team;
	(void)snprintf(out, size, "team");
}
// NOLINTEND(readability-non-const-parameter)

TEST(interns_numeric_paths) {
	Serialised serialised = {0};
	active = &serialised;
	memset(pathBuckets, 0, sizeof pathBuckets);
	uint32_t first[] = {SITE_PART_HOME, SITE_TEAM_PLAYER};
	uint32_t collision[] = {SITE_PART_HOME, SITE_TEAM_PLAYER + PATH_BUCKETS};
	ASSERT_BOOL_EQ(
		(hashChain(first, 2) & (PATH_BUCKETS - 1)) == (hashChain(collision, 2) & (PATH_BUCKETS - 1)),
		true
	);
	ASSERT_INT_EQ((int)internPath(first, 2), 0);
	ASSERT_INT_EQ((int)internPath(collision, 2), 1);
	for (unsigned i = 0; i < 1000; ++i) {
		ASSERT_INT_EQ((int)internPath(first, 2), 0);
	}
	ASSERT_BOOL_EQ(serialised.pathCount == 2, true);
	if (serialised.pathCount == 2) {
		ASSERT_BOOL_EQ(strcmp(serialised.paths[0], "1ac77d5>1b6f456") == 0, true);
	}
	void *frames[] = {
		game_address(SITE_PLAYER_UID),
		game_address(SITE_TEAM_PLAYER),
		game_address(SITE_PART_HOME),
		NULL,
	};
	ASSERT_INT_EQ((int)pathOf(frames, 4), 0);
	ASSERT_INT_EQ((int)internPath(first, 0), 2);
	trace_free(&serialised);
	memset(pathBuckets, 0, sizeof pathBuckets);
	for (uint32_t i = 0; i < PATH_BUCKETS; ++i) {
		ASSERT_BOOL_EQ(internPath(&i, 1) != UINT32_MAX, true);
	}
	uint32_t extra = PATH_BUCKETS;
	ASSERT_BOOL_EQ(internPath(&extra, 1) == UINT32_MAX, true);
	trace_free(&serialised);
	active = NULL;
}

TEST(rejects_incomplete_traces) {
	uint8_t bytes[3] = {0};
	TraceRecord records[] = {{.length = 1}, {.offset = 1, .length = 2}, {.offset = 3}};
	Serialised serialised = {
		.bytes = {.data = bytes, .length = sizeof bytes},
		.records = records,
		.recordCount = 2,
		.pathCount = 1,
	};
	ASSERT_BOOL_EQ(recordsTileBytes(&serialised), true);
	records[1].length = 3;
	ASSERT_BOOL_EQ(recordsTileBytes(&serialised), false);
	records[1].length = 2;
	records[1].path = 1;
	ASSERT_BOOL_EQ(recordsTileBytes(&serialised), false);
	records[1].path = 0;
	serialised.recordCount = 3;
	ASSERT_BOOL_EQ(recordsTileBytes(&serialised), false);
}

TEST(reports_trace_allocation_failures) {
	for (int failure = 0; failure < 2; ++failure) {
		Serialised serialised = {0};
		traceAllocationsUntilFailure = failure;
		ASSERT_BOOL_EQ(trace_serialize((uint8_t *)&builtSeed, &serialised, true), false);
		ASSERT_BOOL_EQ(active == NULL && engineOutputVtable == &engineVtable[1], true);
		trace_free(&serialised);
	}
	traceAllocationsUntilFailure = -1;
	Serialised serialised = {0};
	ASSERT_BOOL_EQ(trace_serialize((uint8_t *)&builtSeed, &serialised, true), true);
	trace_free(&serialised);
}

TEST(only_traces_setup_round_trips) {
	uint32_t seed = 99;
	ByteBuffer input = {.data = (uint8_t *)&seed, .length = sizeof seed};
	SeedBuilder builder;
	char error[256] = "";
	serialisations = 0;
	tracedSerialisations = 0;
	hasFixture = true;
	bool ok = seedBuilder_open(&builder, &input, seed, NULL, error, sizeof error);
	hasFixture = false;
	ASSERT_BOOL_EQ(ok, true);
	ASSERT_BOOL_EQ(serialisations == 3 && tracedSerialisations == 2, true);
	if (ok) {
		ASSERT_BOOL_EQ(seedBuilder_editCount(&builder) == 0, true);
		seedBuilder_close(&builder);
	}
}

TEST(drops_only_replaced_team_edits) {
	TraceRecord records[] = {{.path = 0}, {.path = 1}, {.path = 1}};
	char *paths[] = {"1ac77d5", "1ac77e8"};
	bool edited[3];
	SeedBuilder builder = {
		.input = {.records = records, .recordCount = 3, .paths = paths, .pathCount = 2},
		.edited = edited,
	};
	size_t dropped = 0;
	for (unsigned side = 0; side < 2; ++side) {
		edited[0] = edited[1] = edited[2] = true;
		builder.editCount = 3;
		ASSERT_BOOL_EQ(seedBuilder_dropTeamEdits(&builder, side, &dropped), true);
		ASSERT_BOOL_EQ(dropped == (side ? 2u : 1u), true);
		ASSERT_BOOL_EQ(seedBuilder_editCount(&builder) == (side ? 1u : 2u), true);
		ASSERT_BOOL_EQ(
			edited[0] == (side != 0) && edited[1] == (side == 0) && edited[2] == (side == 0), true
		);
	}
	records[0].path = 2;
	ASSERT_BOOL_EQ(seedBuilder_dropTeamEdits(&builder, 1, &dropped), false);
	ASSERT_BOOL_EQ(dropped == 0 && seedBuilder_editCount(&builder) == 1, true);
}

static void storePointer(uint8_t *base, size_t offset, const void *value) {
	memcpy(base + offset, &value, sizeof value);
}

TEST(swaps_either_or_both_teams_and_restores_game_state) {
	uint8_t fixture[FIXTURE_AWAY + sizeof(void *)] = {0};
	uint8_t copies[8][sizeof fixture];
	uint8_t teams[2][TEAM_IN_GAME_DATA + sizeof(void *)] = {0};
	uint8_t nextMatch[2][NEXT_MATCH_OPPONENT + sizeof(void *)] = {0};
	uint8_t replacements[2] = {0};
	uint8_t holder[HUMAN_MATCHES + 2 * sizeof(void *)] = {0};
	uint8_t *copyPointers[8];
	uint8_t **entries[8];
	for (unsigned side = 0; side < 2; ++side) {
		storePointer(fixture, side ? FIXTURE_AWAY : FIXTURE_HOME, teams[side]);
		storePointer(teams[side], TEAM_IN_GAME_DATA, nextMatch[side]);
		storePointer(nextMatch[side], NEXT_MATCH_OPPONENT, teams[1 - side]);
	}
	for (unsigned i = 0; i < 8; ++i) {
		memcpy(copies[i], fixture, sizeof fixture);
		copyPointers[i] = copies[i];
		entries[i] = &copyPointers[i];
	}
	storePointer(holder, HUMAN_MATCHES, entries);
	storePointer(holder, HUMAN_MATCHES + sizeof(void *), entries + 1);
	humanMatchesHolder = holder;
	const uint8_t *overrides[][2] = {{&replacements[0], NULL},
		{NULL, &replacements[1]},
		{&replacements[0], &replacements[1]},
		{teams[1], &replacements[1]},
		{&replacements[0], teams[0]},
		{teams[1], teams[0]}};
	const uint8_t *opponents[][2] = {{teams[1], &replacements[0]},
		{&replacements[1], teams[0]},
		{&replacements[1], &replacements[0]},
		{&replacements[1], &replacements[1]},
		{&replacements[0], &replacements[0]},
		{teams[1], teams[0]}};
	for (unsigned scenario = 0; scenario < sizeof overrides / sizeof overrides[0]; ++scenario) {
		SeedBuilder builder = {.next = {.fixture = fixture}};
		for (unsigned side = 0; side < 2; ++side) {
			builder.teams[side] = overrides[scenario][side];
			expectedBuildTeams[side] = builder.teams[side] ? builder.teams[side] : teams[side];
		}
		Swap swaps[MAX_SWAPS];
		size_t count = 0;
		ASSERT_BOOL_EQ(swapTeams(&builder, swaps, &count), true);
		for (unsigned side = 0; side < 2; ++side) {
			uint8_t *value = NULL;
			ASSERT_BOOL_EQ(memory_readPointer(fixture, side ? FIXTURE_AWAY : FIXTURE_HOME, &value), true);
			ASSERT_BOOL_EQ(value == expectedBuildTeams[side], true);
			ASSERT_BOOL_EQ(
				memory_readPointer(copies[0], side ? FIXTURE_AWAY : FIXTURE_HOME, &value), true
			);
			ASSERT_BOOL_EQ(value == expectedBuildTeams[side], true);
			ASSERT_BOOL_EQ(memory_readPointer(nextMatch[side], NEXT_MATCH_OPPONENT, &value), true);
			ASSERT_BOOL_EQ(value == opponents[scenario][side], true);
		}
		restoreSwaps(swaps, count);
		for (unsigned failure = 0; failure < 2; ++failure) {
			Serialised serialised = {0};
			failBuild = failure != 0;
			ASSERT_BOOL_EQ(buildSerialised(&builder, 99, &serialised, false), !failBuild);
			trace_free(&serialised);
			for (unsigned side = 0; side < 2; ++side) {
				uint8_t *value = NULL;
				ASSERT_BOOL_EQ(
					memory_readPointer(fixture, side ? FIXTURE_AWAY : FIXTURE_HOME, &value), true
				);
				ASSERT_BOOL_EQ(value == teams[side], true);
				ASSERT_BOOL_EQ(
					memory_readPointer(copies[0], side ? FIXTURE_AWAY : FIXTURE_HOME, &value), true
				);
				ASSERT_BOOL_EQ(value == teams[side], true);
				ASSERT_BOOL_EQ(memory_readPointer(nextMatch[side], NEXT_MATCH_OPPONENT, &value), true);
				ASSERT_BOOL_EQ(value == teams[1 - side], true);
			}
		}
	}
	SeedBuilder builder = {
		.next = {.fixture = fixture}, .teams = {&replacements[0], &replacements[1]}};
	storePointer(holder, HUMAN_MATCHES + sizeof(void *), entries + 8);
	Swap swaps[MAX_SWAPS];
	size_t count = 0;
	ASSERT_BOOL_EQ(swapTeams(&builder, swaps, &count), false);
	for (unsigned i = 0; i < 8; ++i) {
		ASSERT_INT_EQ(memcmp(copies[i], fixture, sizeof fixture), 0);
	}
	for (unsigned side = 0; side < 2; ++side) {
		uint8_t *value = NULL;
		ASSERT_BOOL_EQ(memory_readPointer(fixture, side ? FIXTURE_AWAY : FIXTURE_HOME, &value), true);
		ASSERT_BOOL_EQ(value == teams[side], true);
	}
	builder.teams[0] = teams[1];
	builder.teams[1] = NULL;
	ASSERT_BOOL_EQ(swapTeams(&builder, swaps, &count), false);
	ASSERT_BOOL_EQ(count == 0, true);
	expectedBuildTeams[0] = expectedBuildTeams[1] = NULL;
	humanMatchesHolder = NULL;
	failBuild = false;
}

TEST(writes_both_team_overrides) {
	uint8_t bytes[6] = {0};
	Serialised serialised = {.bytes = {.data = bytes, .length = sizeof bytes}};
	FILE *file = tmpfile();
	ASSERT_BOOL_EQ(file != NULL, true);
	if (!file) {
		return;
	}
	uint32_t teams[2] = {678, 677};
	writeHeader(file, &serialised, "test", teams, false);
	ASSERT_INT_EQ(fflush(file), 0);
	rewind(file);
	char text[512] = {0};
	ASSERT_BOOL_EQ(fread(text, 1, sizeof text - 1, file) > 0, true);
	ASSERT_BOOL_EQ(strstr(text, "\"home_team\": 678") != NULL, true);
	ASSERT_BOOL_EQ(strstr(text, "\"away_team\": 677") != NULL, true);
	ASSERT_INT_EQ(fclose(file), 0);
}

TEST(skips_tracing_unedited_builds) {
	SeedBuilder builder = {0};
	InitializeCriticalSection(&builder.lock);
	ByteBuffer out = {0};
	usedTrace = true;
	ASSERT_BOOL_EQ(seedBuilder_build(&builder, 12345, &out), true);
	ASSERT_BOOL_EQ(usedTrace, false);
	ASSERT_BOOL_EQ(out.length == sizeof(uint32_t), true);
	if (out.length == sizeof(uint32_t)) {
		ASSERT_BOOL_EQ(memory_loadU32(out.data) == 12345, true);
	}
	ASSERT_BOOL_EQ(seedBuilder_build(&builder, UINT32_MAX, &out), true);
	ASSERT_BOOL_EQ(usedTrace, false);
	if (out.length == sizeof(uint32_t)) {
		ASSERT_BOOL_EQ(memory_loadU32(out.data) == UINT32_MAX, true);
	}
	DeleteCriticalSection(&builder.lock);
	buffer_free(&out);
}

TEST(merges_resized_edits_with_the_same_layout) {
	uint8_t inputBytes[] = {'i', 'E', 'D', 'I', 'T', 'z'};
	uint8_t builtBytes[] = {'a', 'b', 'c', 'd'};
	TraceRecord inputRecords[] = {
		{.site = 1, .length = 1},
		{.site = 2, .offset = 1, .length = 4},
		{.site = 3, .offset = 5, .length = 1},
	};
	TraceRecord builtRecords[] = {
		{.site = 1, .length = 1},
		{.site = 2, .offset = 1, .length = 2},
		{.site = 3, .offset = 3, .length = 1},
	};
	bool edited[] = {false, true, false};
	SeedBuilder builder = {
		.input = {.bytes = {.data = inputBytes, .length = sizeof inputBytes},
			.records = inputRecords,
			.recordCount = 3},
		.edited = edited,
		.editCount = 1,
	};
	Serialised built = {.bytes = {.data = builtBytes, .length = sizeof builtBytes},
		.records = builtRecords,
		.recordCount = 3};
	ByteBuffer out = {0};
	ASSERT_BOOL_EQ(mergeEdits(&builder, &built, 7, &out), true);
	ASSERT_BOOL_EQ(out.length == 6, true);
	if (out.length == 6) {
		ASSERT_BOOL_EQ(memcmp(out.data, "aEDITd", 6) == 0, true);
	}
	buffer_free(&out);
}

TEST(rejects_unplaceable_edits) {
	uint8_t inputBytes[] = {10, 20, 30};
	uint8_t builtBytes[] = {11, 40, 50, 31};
	TraceRecord inputRecords[] = {
		{.site = 1, .length = 1},
		{.site = 2, .offset = 1, .length = 1},
		{.site = 3, .offset = 2, .length = 1},
	};
	TraceRecord builtRecords[] = {
		{.site = 1, .length = 1},
		{.site = 4, .offset = 1, .length = 1},
		{.site = 5, .offset = 2, .length = 1},
		{.site = 3, .offset = 3, .length = 1},
	};
	char *paths[] = {""};
	bool edited[] = {false, true, false};
	SeedBuilder builder = {
		.input = {.bytes = {.data = inputBytes, .length = sizeof inputBytes},
			.records = inputRecords,
			.recordCount = 3,
			.paths = paths,
			.pathCount = 1},
		.edited = edited,
		.editCount = 1,
	};
	Serialised built = {.bytes = {.data = builtBytes, .length = sizeof builtBytes},
		.records = builtRecords,
		.recordCount = 4,
		.paths = paths,
		.pathCount = 1};
	ByteBuffer out = {0};
	ASSERT_BOOL_EQ(mergeEdits(&builder, &built, 7, &out), false);
	edited[1] = false;
	edited[2] = true;
	ASSERT_BOOL_EQ(mergeEdits(&builder, &built, 7, &out), true);
	const uint8_t expected[] = {11, 40, 50, 30};
	ASSERT_BOOL_EQ(out.length == sizeof expected, true);
	if (out.length == sizeof expected) {
		ASSERT_BOOL_EQ(memcmp(out.data, expected, sizeof expected) == 0, true);
	}
	buffer_free(&out);
}

typedef struct TestPlayer {
	_Alignas(void *) uint8_t object[PLAYER_STATS + sizeof(void *)];
	uint8_t stats[256];
} TestPlayer;

static void setPointer(uint8_t *bytes, size_t offset, const void *value) {
	memcpy(bytes + offset, &value, sizeof value);
}

static void setPlayer(TestPlayer *player, uint32_t uniqueId, uint8_t goals) {
	*player = (TestPlayer){0};
	memcpy(player->object + PLAYER_UNIQUE_ID, &uniqueId, sizeof uniqueId);
	setPointer(player->object, PLAYER_STATS, player->stats);
	player->stats[STATS_PERSON] = 1;
	uint16_t rating = 700;
	memcpy(player->stats + STATS_RATING, &rating, sizeof rating);
	player->stats[playerValues[V_GOALS].offset] = goals;
	player->stats[playerValues[V_PASSES_COMPLETED].offset] = 30;
}

TEST(keeps_statistics_with_player_identity) {
	TestPlayer players[3];
	setPlayer(&players[0], 10, 1);
	setPlayer(&players[1], 20, 2);
	setPlayer(&players[2], 5, 5);
	_Alignas(void *) uint8_t home[1024] = {0};
	_Alignas(void *) uint8_t away[1024] = {0};
	_Alignas(void *) uint8_t result[160] = {0};
	_Alignas(void *) uint8_t match[MATCH_TEAM_AWAY + sizeof(void *)] = {0};
	home[TEAM_PLAYER_COUNT] = 2;
	setPointer(home, TEAM_PLAYERS, players[0].object);
	setPointer(home, TEAM_PLAYERS + sizeof(void *), players[1].object);
	uint16_t length = 90;
	memcpy(result + RESULT_MATCH_LENGTH, &length, sizeof length);
	setPointer(match, MATCH_RESULT, result);
	setPointer(match, MATCH_TEAM_HOME, home);
	setPointer(match, MATCH_TEAM_AWAY, away);
	MatchStatsTotals totals = {0};
	ASSERT_BOOL_EQ(matchStats_add(&totals, match), true);
	setPlayer(&players[0], 10, 4);
	setPlayer(&players[1], 20, 3);
	home[TEAM_PLAYER_COUNT] = 3;
	setPointer(home, TEAM_PLAYERS, players[1].object);
	setPointer(home, TEAM_PLAYERS + sizeof(void *), players[0].object);
	setPointer(home, TEAM_PLAYERS + 2 * sizeof(void *), players[2].object);
	ASSERT_BOOL_EQ(matchStats_add(&totals, match), true);
	ASSERT_BOOL_EQ(totals.matches == 2 && totals.teams[0].playerCount == 3, true);
	TeamTotals *team = &totals.teams[0];
	if (team->playerCount == 3) {
		ASSERT_BOOL_EQ(team->players[0].uniqueId == 5 && team->players[0].values[V_GOALS] == 5, true);
		ASSERT_BOOL_EQ(team->players[1].uniqueId == 10 && team->players[1].values[V_GOALS] == 5, true);
		ASSERT_BOOL_EQ(team->players[2].uniqueId == 20 && team->players[2].values[V_GOALS] == 5, true);
		ASSERT_BOOL_EQ(team->players[1].appearances == 2 && team->players[0].appearances == 1, true);
		ASSERT_BOOL_EQ(team->values[1] == 200, true);
	}
	for (uint32_t i = 0; i < 2 * MATCH_STATS_MAX_PLAYERS; ++i) {
		ASSERT_BOOL_EQ(findPlayer(team, 1000 + i) != NULL, true);
	}
	ASSERT_BOOL_EQ(team->playerCount == 3 + 2 * MATCH_STATS_MAX_PLAYERS, true);
	matchStats_free(&totals);
	ASSERT_BOOL_EQ(totals.teams[0].players == NULL && totals.matches == 0, true);
}

TEST(rejects_truncated_statistics_paths) {
	wchar_t output[HARNESS_PATH_LENGTH];
	ASSERT_BOOL_EQ(siblingPath(L"match.csv", L"-players.csv", output, HARNESS_PATH_LENGTH), true);
	ASSERT_BOOL_EQ(wcscmp(output, L"match-players.csv") == 0, true);
	wchar_t longPath[HARNESS_PATH_LENGTH];
	wmemset(longPath, L'a', HARNESS_PATH_LENGTH - 1);
	longPath[HARNESS_PATH_LENGTH - 1] = 0;
	ASSERT_BOOL_EQ(siblingPath(longPath, L"-players.csv", output, HARNESS_PATH_LENGTH), false);
	longPath[300] = 0;
	ASSERT_BOOL_EQ(siblingPath(longPath, L"-players.csv", output, HARNESS_PATH_LENGTH), true);
	ASSERT_BOOL_EQ(wcslen(output) == 312, true);
}

TEST(labels_instruction_runs_in_order) {
	TraceRecord records[] = {
		{.site = SITE_TACTIC_TEAM_INSTRUCTIONS},
		{.site = SITE_TACTIC_TEAM_INSTRUCTIONS},
		{.site = SITE_TACTIC_TEAM_INSTRUCTIONS},
		{.site = SITE_TACTIC_MENTALITY},
		{.site = SITE_TACTIC_TEAM_INSTRUCTIONS},
	};
	char *paths[] = {"1ac77d5>1b6f34b>1a8971a"};
	Serialised serialised = {.records = records, .recordCount = 5, .paths = paths, .pathCount = 1};
	PeopleIndex people = {0};
	FieldLabel *labels = labels_compute(&serialised, &people);
	ASSERT_BOOL_EQ(labels != NULL, true);
	if (!labels) {
		return;
	}
	ASSERT_INT_EQ((int)labels[0].flagWord, 0);
	ASSERT_INT_EQ((int)labels[1].flagWord, 1);
	ASSERT_INT_EQ((int)labels[2].flagWord, 2);
	ASSERT_INT_EQ((int)labels[4].flagWord, 0);
	ASSERT_BOOL_EQ(
		strcmp(labels[2].name, "home.manager.tactic.plan[0].team_instructions[2]") == 0, true
	);
	free(labels);
}

TEST(labels_staff_attributes_byte_by_byte) {
	uint8_t bytes[4 + STAFF_ATTRIBUTE_COUNT] = {0};
	for (size_t k = 0; k < STAFF_ATTRIBUTE_COUNT; ++k) {
		bytes[4 + k] = (uint8_t)(k + 1);
	}
	TraceRecord records[] = {
		{.site = SITE_BACKROOM_FIRST, .offset = 0, .length = 4},
		{.site = SITE_STAFF_ATTRIBUTES, .offset = 4, .length = STAFF_ATTRIBUTE_COUNT},
	};
	char *paths[] = {"1ac77d5>1b6f51f>1ae992a"};
	Serialised serialised = {
		.bytes = {.data = bytes, .length = sizeof bytes},
		.records = records,
		.recordCount = 2,
		.paths = paths,
		.pathCount = 1,
	};
	PeopleIndex people = {0};
	FieldLabel *labels = labels_compute(&serialised, &people);
	ASSERT_BOOL_EQ(labels != NULL, true);
	if (!labels) {
		return;
	}
	ASSERT_BOOL_EQ(labels[1].staffAttributes, true);
	ASSERT_BOOL_EQ(strcmp(labels[1].name, "home.backroom[0].attributes") == 0, true);
	wchar_t directory[MAX_PATH] = {0};
	wchar_t path[MAX_PATH] = {0};
	DWORD length = GetTempPathW(MAX_PATH, directory);
	ASSERT_BOOL_EQ(length > 0 && length < MAX_PATH, true);
	ASSERT_BOOL_EQ(length && length < MAX_PATH && GetTempFileNameW(directory, L"fma", 0, path), true);
	FILE *file = path[0] ? _wfopen(path, L"w+b") : NULL;
	ASSERT_BOOL_EQ(file != NULL, true);
	if (file) {
		ASSERT_INT_EQ((int)writeField(file, &serialised, labels, 1, SIZE_MAX, false), 1);
		rewind(file);
		char line[256];
		size_t fields = 0;
		while (fgets(line, sizeof line, file)) {
			if (fields == 0) {
				ASSERT_BOOL_EQ(
					strstr(
						line, "\"name\": \"home.backroom[0].attributes.tactical_attacking\", \"u8\": 1}"
					) != NULL,
					true
				);
			}
			if (fields == STAFF_ATTRIBUTE_COUNT - 1) {
				ASSERT_BOOL_EQ(
					strstr(line, ".attributes.coaching_set_pieces\", \"u8\": 52}") != NULL, true
				);
			}
			++fields;
		}
		ASSERT_INT_EQ((int)fields, STAFF_ATTRIBUTE_COUNT);
		(void)fclose(file);
	}
	if (path[0]) {
		(void)DeleteFileW(path);
	}
	free(labels);
}

TEST(labels_weather_and_venue_fields) {
	TraceRecord records[] = {
		{.site = 0x1A7BED3, .path = 0},                      // match header: kickoff weather follows
		{.site = 0x1A7BEE9, .path = 0},                      // MATCH_BASE+0x6b0
		{.site = 0x43CC669, .path = 0},                      // first weather change's temperature
		{.site = SITE_WEATHER_CHANGE_TIME, .path = 0},       //   and its time
		{.site = 0x43CC669, .path = 0},                      // second change's temperature
		{.site = 0x1A8EB7E, .path = 1},                      // venue: the pitch length names the block
		{.site = SITE_PITCH_WIDTH, .path = 1},               //
		{.site = 0x1A8EC6C, .path = 1},                      // second landmark coordinate
		{.site = 0x456AA30, .path = 1},                      // fn_456a9d0+0x158
		{.site = 0x45844E1, .path = 1},                      // fn_45844b0+0x7
		{.site = 0x43D0774, .caller = 0x43D0256, .path = 1}, // the unnamed first section array
		{.site = 0x43D0774, .caller = SITE_OCCUPANCY_CALLER, .path = 1},
		{.site = SITE_TEXT, .caller = SITE_STADIUM_NAME_CALLER, .path = 1},
		{.site = 0x11758B46, .path = 2}, // fixture_extra: attendance names the block
		{.site = SITE_FIXTURE_EXTRA_AWAY_FANS, .path = 2},
	};
	char *paths[] = {"1ac76fb", "1ac778c", "1ac77c2"};
	Serialised serialised = {
		.records = records,
		.recordCount = sizeof records / sizeof records[0],
		.paths = paths,
		.pathCount = 3,
	};
	PeopleIndex people = {0};
	FieldLabel *labels = labels_compute(&serialised, &people);
	ASSERT_BOOL_EQ(labels != NULL, true);
	if (!labels) {
		return;
	}
	static const char *const expected[] = {
		"match",
		"match.weather.wind",
		"match.weather.change[0].temperature",
		"match.weather.change[0].time",
		"match.weather.change[1].temperature",
		"venue",
		"venue.pitch.width",
		"venue.pitch.centre_spot.y",
		"venue.attendance",
		"venue.stand[0].flags",
		"",
		"venue.section[1].occupancy",
		"venue.stadium.name",
		"fixture_extra",
		"fixture_extra.away_fans",
	};
	for (size_t i = 0; i < sizeof expected / sizeof expected[0]; ++i) {
		ASSERT_BOOL_EQ(strcmp(labels[i].name, expected[i]) == 0, true);
	}
	ASSERT_INT_EQ((int)labels[9].flags, FLAGS_STAND);
	ASSERT_BOOL_EQ(strcmp(labels_flagName(&labels[9], 3), "tunnel") == 0, true);
	free(labels);
}

TEST(only_merges_valid_utf8_text) {
	static const char *const valid[] = {
		"text",
		"\xC2\xA0",
		"\xDF\xBF",
		"\xE0\xA0\x80",
		"\xED\x9F\xBF",
		"\xF0\x90\x80\x80",
		"\xF4\x8F\xBF\xBF",
	};
	static const char *const invalid[] = {
		"\x01",
		"\x7F",
		"\x80",
		"\xC0\xAF",
		"\xC1\xBF",
		"\xC2",
		"\xE0\x80\xAF",
		"\xED\xA0\x80",
		"\xF0\x80\x80\xAF",
		"\xF4\x90\x80\x80",
		"\xF5\x80\x80\x80",
		"\xFF",
	};
	for (size_t i = 0; i < sizeof valid / sizeof valid[0]; ++i) {
		ASSERT_BOOL_EQ(isText((const uint8_t *)valid[i], strlen(valid[i])), true);
	}
	for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		ASSERT_BOOL_EQ(isText((const uint8_t *)invalid[i], strlen(invalid[i])), false);
	}
}

void registerDllTests(void) {
	test_run(
		"DLL reports trace allocation failures and restores its hook",
		test_reports_trace_allocation_failures
	);
	test_run(
		"DLL only traces setup serialisations whose records are used",
		test_only_traces_setup_round_trips
	);
	test_run(
		"DLL drops only the replaced team's edits and reports failures",
		test_drops_only_replaced_team_edits
	);
	test_run(
		"DLL swaps either or both teams and restores game state",
		test_swaps_either_or_both_teams_and_restores_game_state
	);
	test_run("DLL writes both team overrides", test_writes_both_team_overrides);
	test_run("DLL interns call chains without repeated text formatting", test_interns_numeric_paths);
	test_run(
		"DLL rejects incomplete or invalid serialisation traces", test_rejects_incomplete_traces
	);
	test_run(
		"DLL skips tracing and alignment for unedited builds", test_skips_tracing_unedited_builds
	);
	test_run(
		"DLL merges resized edits without parsing unchanged layouts",
		test_merges_resized_edits_with_the_same_layout
	);
	test_run("DLL rejects edits that cannot be aligned", test_rejects_unplaceable_edits);
	test_run(
		"DLL statistics follow players across changed line-ups",
		test_keeps_statistics_with_player_identity
	);
	test_run("DLL rejects truncated detailed-report paths", test_rejects_truncated_statistics_paths);
	test_run("DLL numbers instruction runs consistently", test_labels_instruction_runs_in_order);
	test_run(
		"DLL exports staff attributes as one named u8 each", test_labels_staff_attributes_byte_by_byte
	);
	test_run("DLL names weather, venue and attendance fields", test_labels_weather_and_venue_fields);
	test_run("DLL only exports valid UTF-8 as JSON text", test_only_merges_valid_utf8_text);
	buffer_free(&engineOutput);
}
