// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture_export.h"
#include "slug.h"
#include "test.h"
#include "tests.h"

#include <stdio.h>
#include <string.h>


TEST(builds_export_filenames) {
	FixtureData data = {0};
	wchar_t filename[FIXTURE_EXPORT_FILENAME_LENGTH] = {0};
	(void)snprintf(data.teams[0].name, FIXTURE_TEXT, "Aston Villa");
	(void)snprintf(data.teams[1].name, FIXTURE_TEXT, "Bristol City");
	ASSERT_BOOL_EQ(
		fixtureExport_filename(&data, 1792074287, filename, FIXTURE_EXPORT_FILENAME_LENGTH), true
	);
	ASSERT_INT_EQ(wcscmp(filename, L"aston-villa-v-bristol-city-1792074287.json"), 0);
}

TEST(sanitises_export_filenames) {
	FixtureData data = {0};
	wchar_t filename[FIXTURE_EXPORT_FILENAME_LENGTH] = {0};
	(void)snprintf(data.teams[0].name, FIXTURE_TEXT, "M\xc3\xbcnchen\t\r\n\x01\x7f/\\:*?\"<>|");
	(void)snprintf(data.teams[1].name, FIXTURE_TEXT, "FC  CITY-1.2&'()");
	ASSERT_BOOL_EQ(fixtureExport_filename(&data, 42, filename, FIXTURE_EXPORT_FILENAME_LENGTH), true);
	ASSERT_INT_EQ(wcscmp(filename, L"mnchen-v-fc--city-1.2&'()-42.json"), 0);
	ASSERT_BOOL_EQ(fixtureExport_filename(&data, 42, filename, 8), false);
}

TEST(handles_empty_team_names) {
	FixtureData data = {0};
	wchar_t filename[FIXTURE_EXPORT_FILENAME_LENGTH] = {0};
	ASSERT_BOOL_EQ(fixtureExport_filename(&data, 0, filename, FIXTURE_EXPORT_FILENAME_LENGTH), true);
	ASSERT_INT_EQ(wcscmp(filename, L"-v--0.json"), 0);
}

TEST(enforces_export_filename_length) {
	FixtureData data = {0};
	wchar_t filename[FIXTURE_EXPORT_FILENAME_LENGTH] = {0};
	memset(data.teams[0].name, 'A', FIXTURE_TEXT - 1);
	ASSERT_BOOL_EQ(
		fixtureExport_filename(&data, 42, filename, FIXTURE_EXPORT_FILENAME_LENGTH), false
	);
	size_t homeLength = 255 - wcslen(L"-v--42.json");
	data.teams[0].name[homeLength] = 0;
	ASSERT_BOOL_EQ(fixtureExport_filename(&data, 42, filename, FIXTURE_EXPORT_FILENAME_LENGTH), true);
	ASSERT_BOOL_EQ(wcslen(filename) == 255, true);
	data.teams[0].name[homeLength] = 'A';
	data.teams[0].name[homeLength + 1] = 0;
	wchar_t longer[512];
	ASSERT_BOOL_EQ(fixtureExport_filename(&data, 42, longer, 512), false);
}

TEST(makes_file_names_from_people_names) {
	static const struct {
		const char *name;
		const char *slug;
	} cases[] = {
		{"Harvey Rhoades", "harvey-rhoades"},
		{"\xC3\x81lex Moreno", "alex-moreno"},                 // Álex
		{"Tommi O'Reilly", "tommi-oreilly"},
		{"Tommi O\xE2\x80\x99Reilly", "tommi-oreilly"},        // ’
		{"  Diego   Carlos  ", "diego-carlos"},
		{"\xC3\x98" "degaard", "odegaard"},                     // Ø
		{"M\xC3\xBCller-Wohlfahrt", "muller-wohlfahrt"},        // ü
		{"\xC5\x81ukasz Fabia\xC5\x84ski", "lukasz-fabianski"}, // Ł, ń
		{"Strau\xC3\x9F", "strauss"},                           // ß
		{"Jr. 2nd", "jr-nd"},
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		char slug[64] = "";
		ASSERT_BOOL_EQ(slug_fromName(cases[i].name, slug, sizeof slug), true);
		ASSERT_INT_EQ(strcmp(slug, cases[i].slug), 0);
	}
	char slug[8] = "";
	ASSERT_BOOL_EQ(slug_fromName("\xE6\x9D\x8E", slug, sizeof slug), false); // 李
	ASSERT_BOOL_EQ(slug_fromName("", slug, sizeof slug), false);
	ASSERT_BOOL_EQ(slug_fromName("Harvey Rhoades", slug, sizeof slug), false); // too long
	ASSERT_BOOL_EQ(slug_fromName("\xC3", slug, sizeof slug), false);           // truncated UTF-8
}

void registerFixtureExportTests(void) {
	test_run("slug makes file names from people's names", test_makes_file_names_from_people_names);
	test_run("fixture export builds team-based filenames", test_builds_export_filenames);
	test_run("fixture export sanitises team names", test_sanitises_export_filenames);
	test_run("fixture export handles empty team names", test_handles_empty_team_names);
	test_run(
		"fixture export enforces the filename length limit", test_enforces_export_filename_length
	);
}
