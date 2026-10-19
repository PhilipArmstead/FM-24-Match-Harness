// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "player_inject.h"
#include "test.h"
#include "tests.h"

#include <string.h>


static const char config[] =
	"{\"format\": \"fm24-match-config/1\", \"home_team\": 678, \"fields\": [\n"
	"  {\"name\": \"home.player[0]\", \"player\": \"Old Keeper\", \"u8\": 255},\n"
	"  {\"name\": \"home.player[0].ratings\", \"u8map\": {\"a\": 1, \"b\": 2}},\n"
	"  {\"name\": \"home.player[0].condition\", \"u16\": 9000},\n"
	"  {\"name\": \"home.player[0].ability\", \"u8\": 140},\n"
	"  {\"name\": \"home.player[0].person_uid\", \"u32\": 11},\n"
	"  {\"u8\": 7},\n"
	"  {\"u32\": 11},\n"
	"  {\"name\": \"home.player[1]\", \"player\": \"Old Striker\", \"u8\": 255},\n"
	"  {\"name\": \"home.player[1].ratings\", \"u8map\": {\"a\": 3, \"b\": 4}},\n"
	"  {\"name\": \"home.player[1].condition\", \"u16\": 8000},\n"
	"  {\"name\": \"home.player[1].person_uid\", \"u32\": 22},\n"
	"  {\"u32\": 23},\n"
	"  {\"u32\": 22},\n"
	"  {\"name\": \"home.player[10]\", \"player\": \"Lookalike\", \"u8\": 255},\n"
	"  {\"u32\": 22}\n"
	"]}\n";

static const char player[] =
	"{\"format\": \"fm24-player/1\", \"fields\": [\n"
	"  {\"name\": \"person_uid\", \"player\": \"New \\\"Star\\\"\", \"u32\": 33},\n"
	"  {\"name\": \"ratings\", \"u8map\": {\"a\": 19, \"b\": 20}},\n"
	"  {\"name\": \"condition\", \"u16\": 10000},\n"
	"  {\"name\": \"caps\", \"u32\": 5}\n"
	"]}\n";

static bool inject(
	const char *playerJson,
	const char *block,
	bool allowDuplicate,
	ByteBuffer *out,
	PlayerInjection *result,
	char *error,
	size_t errorSize
) {
	return playerInject_apply(
		config,
		sizeof config - 1,
		playerJson,
		strlen(playerJson),
		block,
		allowDuplicate,
		out,
		result,
		error,
		errorSize
	);
}

TEST(replaces_only_the_named_values_of_the_block) {
	static const char expected[] =
		"{\"format\": \"fm24-match-config/1\", \"home_team\": 678, \"fields\": [\n"
		"  {\"name\": \"home.player[0]\", \"player\": \"Old Keeper\", \"u8\": 255},\n"
		"  {\"name\": \"home.player[0].ratings\", \"u8map\": {\"a\": 1, \"b\": 2}},\n"
		"  {\"name\": \"home.player[0].condition\", \"u16\": 9000},\n"
		"  {\"name\": \"home.player[0].ability\", \"u8\": 140},\n"
		"  {\"name\": \"home.player[0].person_uid\", \"u32\": 11},\n"
		"  {\"u8\": 7},\n"
		"  {\"u32\": 11},\n"
		"  {\"name\": \"home.player[1]\", \"player\": \"New \\\"Star\\\"\", \"u8\": 255},\n"
		"  {\"name\": \"home.player[1].ratings\", \"u8map\": {\"a\": 19, \"b\": 20}},\n"
		"  {\"name\": \"home.player[1].condition\", \"u16\": 10000},\n"
		"  {\"name\": \"home.player[1].person_uid\", \"u32\": 33},\n"
		"  {\"u32\": 23},\n"
		"  {\"u32\": 33},\n"
		"  {\"name\": \"home.player[10]\", \"player\": \"Lookalike\", \"u8\": 255},\n"
		"  {\"u32\": 22}\n"
		"]}\n";
	ByteBuffer out = {0};
	PlayerInjection result;
	char error[256] = "";
	ASSERT_BOOL_EQ(inject(player, "home.player[1]", false, &out, &result, error, sizeof error), true);
	ASSERT_BOOL_EQ(out.length == sizeof expected - 1, true);
	ASSERT_INT_EQ(strcmp(buffer_string(&out), expected), 0);
	ASSERT_BOOL_EQ(result.replaced == 3, true);
	ASSERT_BOOL_EQ(result.uidCopies == 1, true);
	ASSERT_BOOL_EQ(result.missing == 1, true);
	ASSERT_INT_EQ(strcmp(result.missingNames, "caps"), 0);
	buffer_free(&out);
}

TEST(allows_a_duplicate_with_its_own_unique_id_when_asked) {
	ByteBuffer out = {0};
	PlayerInjection result;
	char error[256] = "";
	ASSERT_BOOL_EQ(inject(player, "home.player[0]", true, &out, &result, error, sizeof error), true);
	ASSERT_BOOL_EQ(strstr(buffer_string(&out), "\"home.player[0].person_uid\", \"u32\": 33") != NULL, true);
	ASSERT_BOOL_EQ(strstr(buffer_string(&out), "\"a\": 19, \"b\": 20") != NULL, true);
	ASSERT_BOOL_EQ(strstr(buffer_string(&out), "{\"u32\": 11}") == NULL, true);
	ASSERT_BOOL_EQ(result.replaced == 3, true);
	ASSERT_BOOL_EQ(result.uidCopies == 1, true);
	ASSERT_BOOL_EQ(result.missing == 1, true);
	buffer_free(&out);
}

TEST(rejects_a_player_already_in_the_match) {
	static const char twin[] = "{\"format\": \"fm24-player/1\", \"fields\": [\n"
														 "  {\"name\": \"person_uid\", \"u32\": 22}\n"
														 "]}\n";
	ByteBuffer out = {0};
	PlayerInjection result;
	char error[256] = "";
	ASSERT_BOOL_EQ(inject(twin, "home.player[0]", false, &out, &result, error, sizeof error), false);
	ASSERT_BOOL_EQ(strstr(error, "home.player[1].person_uid (Old Striker)") != NULL, true);
	ASSERT_BOOL_EQ(out.length == 0, true);
	ASSERT_BOOL_EQ(inject(twin, "home.player[0]", true, &out, &result, error, sizeof error), true);
	buffer_free(&out);
}

TEST(rejects_missing_blocks_and_mismatched_values) {
	static const char wrongType[] = "{\"format\": \"fm24-player/1\", \"fields\": [\n"
																	"  {\"name\": \"person_uid\", \"u32\": 33},\n"
																	"  {\"name\": \"condition\", \"u32\": 10000}\n"
																	"]}\n";
	static const char noUid[] = "{\"format\": \"fm24-player/1\", \"fields\": [\n"
															"  {\"name\": \"condition\", \"u16\": 10000}\n"
															"]}\n";
	ByteBuffer out = {0};
	PlayerInjection result;
	char error[256] = "";
	ASSERT_BOOL_EQ(inject(player, "away.player[0]", false, &out, &result, error, sizeof error), false);
	ASSERT_INT_EQ(strcmp(error, "the fixture has no away.player[0]"), 0);
	ASSERT_BOOL_EQ(inject(player, "home.player[2]", false, &out, &result, error, sizeof error), false);
	ASSERT_BOOL_EQ(
		inject(wrongType, "home.player[0]", false, &out, &result, error, sizeof error), false
	);
	ASSERT_BOOL_EQ(strstr(error, "home.player[0].condition") != NULL, true);
	ASSERT_BOOL_EQ(inject(noUid, "home.player[0]", false, &out, &result, error, sizeof error), false);
	ASSERT_BOOL_EQ(strstr(error, "person_uid") != NULL, true);
	ASSERT_BOOL_EQ(inject(config, "home.player[0]", false, &out, &result, error, sizeof error), false);
	ASSERT_BOOL_EQ(strstr(error, "fm24-player/1") != NULL, true);
	buffer_free(&out);
}

void registerPlayerInjectTests(void) {
	test_run(
		"player injection replaces only the named values of the block",
		test_replaces_only_the_named_values_of_the_block
	);
	test_run("player injection allows a duplicate with its own unique ID when asked", test_allows_a_duplicate_with_its_own_unique_id_when_asked);
	test_run(
		"player injection rejects a player already in the match",
		test_rejects_a_player_already_in_the_match
	);
	test_run(
		"player injection rejects missing blocks and mismatched values",
		test_rejects_missing_blocks_and_mismatched_values
	);
}
