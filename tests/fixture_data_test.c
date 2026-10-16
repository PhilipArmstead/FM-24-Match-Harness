// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture_data.h"
#include "json_reader.h"
#include "match_config.h"
#include "test.h"
#include "tests.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const char *baselinePath;

static bool append(ByteBuffer *out, const char *format, ...) {
	char text[2048];
	va_list arguments;
	va_start(arguments, format);
	int length = vsnprintf(text, sizeof text, format, arguments);
	va_end(arguments);
	return length >= 0 && (size_t)length < sizeof text && buffer_append(out, text, (size_t)length);
}

static void makeConfig(ByteBuffer *out, bool labelled, bool swapControl, const char *source) {
	ASSERT_BOOL_EQ(
		append(
			out,
			"{\"format\":\"fm24-match-config/1\",\"fields\":["
			"{\"name\":\"fixture\",\"u8\":24},{\"str\":\"Friendly\"},"
			"{\"name\":\"venue\",\"u32\":0},{\"str\":\"Villa Park\"},"
			"{\"name\":\"official[0]\",\"u8\":0},"
			"{\"name\":\"referee\",\"u8\":8},{\"str\":\"Aimee\"},{\"str\":\"Keir\"},"
			"{\"str\":\"Aimee Keir\"}"
		),
		true
	);
	static const unsigned positions[11] = {
		1,
		2097168,
		1048704,
		1048592,
		4,
		4096,
		2113536,
		2097280,
		8,
		1064960,
		2048,
	};
	static const unsigned long long roles[11] = {
		2101248,
		18874368,
		4194320,
		18874368,
		8388612,
		138412032,
		2151677952,
		4194320,
		8388612,
		8390656,
		142606336,
	};
	for (unsigned side = 0; side < 2; ++side) {
		const char *name = side ? "away" : "home";
		ASSERT_BOOL_EQ(
			append(
				out,
				",{\"name\":\"%s.team\",\"u8\":2},{\"str\":\"%s\"},"
				"{\"name\":\"%s.manager\",\"u32\":%u},"
				"{\"name\":\"%s.manager.tactic.plan[0]\",\"u8\":34},{\"str\":\"Custom 4-2-2-2\"},"
				"{\"name\":\"%s.manager.tactic.plan[0].mentality\",\"u8\":6}",
				name,
				side ? "Bristol City" : "Aston Villa",
				name,
				(side == (unsigned)swapControl) ? 135942U : 81920U,
				name,
				name
			),
			true
		);
		for (unsigned i = 0; i < 11; ++i) {
			ASSERT_BOOL_EQ(
				append(
					out,
					",{\"name\":\"%s.manager.tactic.plan[0].slot[%u].position\","
					"\"flags\":[\"deliberately wrong\"],\"u32\":%u},"
					"{\"name\":\"%s.manager.tactic.plan[0].slot[%u].role\",\"u64\":%llu}",
					name,
					i,
					positions[i],
					name,
					i,
					roles[i]
				),
				true
			);
		}
		ASSERT_BOOL_EQ(append(out, ",{\"name\":\"%s.player_count\",\"u8\":12}", name), true);
		for (unsigned i = 0; i < 12; ++i) {
			ASSERT_BOOL_EQ(
				append(
					out,
					",{\"name\":\"%s.player[%u]\",\"player\":\"Stale name\",\"u8\":255},"
					"{\"name\":\"%s.player[%u].ratings\",\"u8map\":{",
					name,
					i,
					name,
					i
				),
				true
			);
			for (unsigned rating = 0; rating < 69; ++rating) {
				ASSERT_BOOL_EQ(append(out, "%s\"r%u\":20", rating ? "," : "", rating), true);
			}
			ASSERT_BOOL_EQ(append(out, "}}"), true);
			static const unsigned values[14] = {
				10000,
				5600,
				5650,
				1,
				26,
				255,
				65396,
				0,
				7750,
				7850,
				7000,
				157,
				157,
				15,
			};
			static const char *const types[14] = {
				"u16",
				"u16",
				"u16",
				"u8",
				"u8",
				"u8",
				"u16",
				"u8",
				"u16",
				"u16",
				"u16",
				"u16",
				"u16",
				"u8",
			};
			for (unsigned field = 0; field < 14; ++field) {
				ASSERT_BOOL_EQ(append(out, ",{\"%s\":%u}", types[field], values[field]), true);
			}
			ASSERT_BOOL_EQ(
				append(
					out,
					",{\"str\":\"Emiliano\"},{\"str\":\"Mart\\u00ednez\"},"
					"{\"str\":\"Emiliano Mart\\u00ednez\"}"
				),
				true
			);
			if (labelled) {
				ASSERT_BOOL_EQ(
					append(
						out,
						",{\"name\":\"%s.player[%u].condition\",\"u16\":9876},"
						"{\"name\":\"%s.player[%u].sharpness[0]\",\"u16\":8125},"
						"{\"name\":\"%s.player[%u].morale\",\"u8\":12},"
						"{\"name\":\"%s.player[%u].fatigue\",\"u16\":65535}",
						name,
						i,
						name,
						i,
						name,
						i,
						name,
						i
					),
					true
				);
			}
		}
	}
	ASSERT_BOOL_EQ(append(out, "]"), true);
	if (source) {
		ASSERT_BOOL_EQ(append(out, ",\"source\":\"%s\"", source), true);
	}
	ASSERT_BOOL_EQ(append(out, "}"), true);
}

static void checkConfig(bool labelled, bool swapControl) {
	ByteBuffer json = {0};
	makeConfig(
		&json, labelled, swapControl, "Aston Villa vs Bristol City, 2023 day 186 19:30, seed 42"
	);
	FixtureData *data = malloc(sizeof *data);
	ASSERT_BOOL_EQ(data != NULL, true);
	if (!data) {
		buffer_free(&json);
		return;
	}
	char error[256] = "";
	bool ok = fixtureData_read(buffer_string(&json), json.length, data, error, sizeof error);
	if (!ok) {
		fprintf(stderr, "%s\n", error);
	}
	ASSERT_BOOL_EQ(ok, true);
	ASSERT_INT_EQ(strcmp(data->teams[0].name, "Aston Villa"), 0);
	ASSERT_INT_EQ(strcmp(data->teams[1].name, "Bristol City"), 0);
	ASSERT_INT_EQ(strcmp(data->teams[0].formation, "4-2-2-2"), 0);
	ASSERT_INT_EQ(strcmp(data->teams[0].tactic, "Custom 4-2-2-2"), 0);
	ASSERT_BOOL_EQ(data->teams[0].lineupKnown, !swapControl);
	ASSERT_BOOL_EQ(data->teams[1].lineupKnown, swapControl);
	ASSERT_INT_EQ(strcmp(data->competition, "Friendly"), 0);
	ASSERT_INT_EQ(strcmp(data->matchType, "Friendly"), 0);
	ASSERT_INT_EQ(strcmp(data->venue, "Villa Park"), 0);
	ASSERT_INT_EQ(strcmp(data->officials[4], "Aimee Keir"), 0);
	ASSERT_INT_EQ(data->officials[0][0], 0);
	ASSERT_INT_EQ(strcmp(data->when, "2023-07-05 19:30"), 0);
	ASSERT_INT_EQ(strcmp(data->teams[0].players[0].name, "Emiliano Mart\xC3\xADnez"), 0);
	ASSERT_BOOL_EQ(data->teams[0].players[0].condition.known, true);
	ASSERT_BOOL_EQ(data->teams[0].players[0].condition.value == (labelled ? 9876U : 10000U), true);
	ASSERT_BOOL_EQ(data->teams[1].players[11].morale.value == (labelled ? 12U : 15U), true);
	ASSERT_BOOL_EQ(data->teams[0].players[0].sharpness[0].value == (labelled ? 8125U : 5600U), true);
	ASSERT_BOOL_EQ(data->teams[0].players[0].sharpness[1].value == 5650, true);
	ASSERT_BOOL_EQ(data->teams[0].players[0].fatigue.value == (labelled ? 65535U : 65396U), true);
	ASSERT_BOOL_EQ(data->teams[0].players[12].present, false);
	free(data);
	buffer_free(&json);
}

TEST(reads_export_dates) {
	static const struct {
		const char *source;
		const char *when;
	} cases[] = {
		{"2024 day 133 08:00", "2024-05-12 08:00"},
		{"2024 day 60 00:00", "2024-02-29 00:00"},
		{"2023 day 60 06:00", "2023-03-01 06:00"},
		{"2024 day 256 23:45", "2024-09-12 23:45"},
		{"2024 day 366 19:30, seed 42", "2024-12-31 19:30"},
		{"2000 day 60 08:00", "2000-02-29 08:00"},
		{"2100 day 60 08:00", "2100-03-01 08:00"},
		{"2023 day 366 08:00", "Invalid export date/time"},
		{"2024 day 0 08:00", "Invalid export date/time"},
		{"0000 day 1 08:00", "Invalid export date/time"},
		{"2024 day 133 24:00", "Invalid export date/time"},
		{"2024 day 133 08:60", "Invalid export date/time"},
		{"2024 day 133 08:00 trailing", "Invalid export date/time"},
		{"2024 day 133 no time", "Invalid export date/time"},
		{"12345 day 133 08:00", "Invalid export date/time"},
		{"2024 day 1 0:0", "2024-01-01 00:00"},
		{"2024 day 999 08:00", "Invalid export date/time"},
		{"2024 day 1000 08:00", "Invalid export date/time"},
		{"2024 day -1 08:00", "Invalid export date/time"},
		{"2024 day +1 08:00", "Invalid export date/time"},
		{"2024 day 1 +1:00", "Invalid export date/time"},
		{"2024 day 1 08:-0", "Invalid export date/time"},
		{"2024 day 1 008:00", "Invalid export date/time"},
		{"2024 day 1 08:000", "Invalid export date/time"},
		{"2024 day 1 08:", "Invalid export date/time"},
		{"2024 day 1 ", "Invalid export date/time"},
		{"2024 day ", "Invalid export date/time"},
		{"Decoded binary fixture (date annotation unavailable)", ""},
		{NULL, ""},
	};
	FixtureData *data = malloc(sizeof *data);
	ASSERT_BOOL_EQ(data != NULL, true);
	if (!data) {
		return;
	}
	for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		ByteBuffer json = {0};
		makeConfig(&json, false, false, cases[i].source);
		char error[256] = "";
		ASSERT_BOOL_EQ(
			fixtureData_read(buffer_string(&json), json.length, data, error, sizeof error), true
		);
		ASSERT_INT_EQ(strcmp(data->when, cases[i].when), 0);
		buffer_free(&json);
	}
	free(data);
}

TEST(decodes_tactic_masks) {
	char text[32];
	fixtureData_position(1048704, text, sizeof text);
	ASSERT_INT_EQ(strcmp(text, "DMCL"), 0);
	fixtureData_position(2097168, text, sizeof text);
	ASSERT_INT_EQ(strcmp(text, "DCR"), 0);
	fixtureData_position(0, text, sizeof text);
	ASSERT_INT_EQ(strcmp(text, "Unknown"), 0);
	fixtureData_position(3145744, text, sizeof text);
	ASSERT_INT_EQ(strcmp(text, "Unknown"), 0);
	ASSERT_INT_EQ(strcmp(fixtureData_role(2151677952ULL), "pressing_forward"), 0);
	ASSERT_INT_EQ(strcmp(fixtureData_duty(2151677952ULL), "Support"), 0);
	ASSERT_INT_EQ(strcmp(fixtureData_role((1ULL << 50) | (1ULL << 23)), "segundo_volante"), 0);
	ASSERT_INT_EQ(strcmp(fixtureData_duty((1ULL << 2) | (1ULL << 34)), "Automatic"), 0);
	ASSERT_INT_EQ(strcmp(fixtureData_role(3), "Unknown"), 0);
	ASSERT_INT_EQ(strcmp(fixtureData_duty((1ULL << 21) | (1ULL << 22)), "Unknown"), 0);
}

TEST(rejects_invalid_configs) {
	static const char *const invalid[] = {
		"{}",
		"{\"format\":\"other\",\"fields\":[]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[{\"u8\":256}]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[{\"u64\":18446744073709551616}]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[{\"u8\":1,\"u16\":1}]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[]} trailing",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[{\"str\":\"\\uD800\"}]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[{\"str\":\"\\uDC00\"}]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[{\"str\":\"raw\nnewline\"}]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[{\"u8\":+1}]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[{\"u8\":01}]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[],\"annotation\":1e}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[],\"annotation\":1.}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[],\"annotation\":1e+}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[],\"annotation\":.5}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[],\"annotation\":-}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[],\"annotation\":00}",
		"{\"format\":0,\"fields\":[]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[],\"source\":}",
		"{\"format\":\"fm24-match-config/1\\u0000suffix\",\"fields\":[]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[{\"u8\\u0000junk\":1}]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[{\"hex\":\"+1\"}]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[{\"hex\":\"-0\"}]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[{\"hex\":\" f\"}]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[{\"hex\":\"\\u0000f\"}]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[{\"hex\":\"abc\"}]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[],\"fields\":[]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[],\"away_team\":4294967296}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[],\"away_team\":-1}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[],\"away_team\":1.0}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[],\"away_team\":1",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[],\"home_team\":4294967296}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[],\"home_team\":-1}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[],\"home_team\":1.0}",
	};
	FixtureData *data = malloc(sizeof *data);
	ASSERT_BOOL_EQ(data != NULL, true);
	if (!data) {
		return;
	}
	for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		char error[256] = "";
		ByteBuffer bytes = {0};
		ASSERT_BOOL_EQ(
			matchConfig_encode(invalid[i], strlen(invalid[i]), &bytes, NULL, error, sizeof error), false
		);
		ASSERT_BOOL_EQ(error[0], true);
		buffer_free(&bytes);
		error[0] = 0;
		ASSERT_BOOL_EQ(
			fixtureData_read(invalid[i], strlen(invalid[i]), data, error, sizeof error), false
		);
		ASSERT_BOOL_EQ(error[0], true);
	}
	free(data);
}

typedef struct Visited {
	ByteBuffer bytes;
	bool ok;
} Visited;

static void collect(void *context, const ConfigField *field) {
	Visited *visited = context;
	visited->ok = visited->ok && buffer_append(&visited->bytes, field->bytes, field->length);
}

TEST(encodes_and_visits_config_fields) {
	static const char json[] =
		"{\"format\":\"fm24-match-config/1\",\"fields\":[{\"hex\":\"\"},"
		"{\"u8\":255},{\"u16\":4660},{\"u32\":2309737967},{\"u64\":18446744073709551615},"
		"{\"hex\":\"00ff\"},{\"u8map\":{\"a\":1,\"b\":2}},{\"str\":\"Z\\u00e9\"}],"
		"\"annotation\":[-1,1.25,1e+3,0.5,-0,0e0,-2.5E-3,null,true,false]}";
	static const uint8_t expected[] = {
		255,
		0x34,
		0x12,
		0xef,
		0xcd,
		0xab,
		0x89,
		255,
		255,
		255,
		255,
		255,
		255,
		255,
		255,
		0,
		255,
		1,
		2,
		3,
		0,
		0,
		0,
		'Z',
		0xc3,
		0xa9,
	};
	ByteBuffer bytes = {0};
	char error[256] = "";
	ASSERT_BOOL_EQ(
		matchConfig_encode(json, sizeof json - 1, &bytes, NULL, error, sizeof error), true
	);
	ASSERT_BOOL_EQ(bytes.length == sizeof expected, true);
	if (bytes.length == sizeof expected) {
		ASSERT_INT_EQ(memcmp(bytes.data, expected, sizeof expected), 0);
	}
	Visited visited = {.ok = true};
	ConfigVisitor visitor = {.context = &visited, .field = collect};
	ASSERT_BOOL_EQ(matchConfig_visit(json, sizeof json - 1, &visitor, error, sizeof error), true);
	ASSERT_BOOL_EQ(visited.ok, true);
	ASSERT_BOOL_EQ(visited.bytes.length == bytes.length, true);
	if (bytes.length && visited.bytes.length == bytes.length) {
		ASSERT_INT_EQ(memcmp(visited.bytes.data, bytes.data, bytes.length), 0);
	}
	buffer_free(&bytes);
	buffer_free(&visited.bytes);
}

TEST(validates_json_strings_and_skipped_values) {
	static const char *const invalid[] = {
		"\"\\u+041\"",
		"\"\\u 041\"",
		"\"\\u-000\"",
		"\"\\u000g\"",
		"\"\\uD800\\u0041\"",
		"\"\\q\"",
		"\"truncated\\",
		"\"\x80\"",
		"\"\xC0\x80\"",
		"\"\xC1\xBF\"",
		"\"\xE0\x80\x80\"",
		"\"\xED\xA0\x80\"",
		"\"\xF0\x80\x80\x80\"",
		"\"\xF4\x90\x80\x80\"",
		"\"\xF5\x80\x80\x80\"",
		"\"\xE2\x82\"",
		"\"\xC2!\"",
	};
	for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		JsonReader reader;
		ByteBuffer text = {0};
		jsonReader_init(&reader, invalid[i], strlen(invalid[i]));
		ASSERT_BOOL_EQ(jsonReader_readString(&reader, &text), false);
		ASSERT_BOOL_EQ(reader.error[0] != 0, true);
		jsonReader_init(&reader, invalid[i], strlen(invalid[i]));
		ASSERT_BOOL_EQ(jsonReader_skipValue(&reader), false);
		ASSERT_BOOL_EQ(reader.error[0] != 0, true);
		buffer_free(&text);
	}
	static const char json[] = "\"plain\\n\\u0000\\u00e9\\uD83D\\uDE00\xF4\x8F\xBF\xBF\"";
	static const uint8_t expected[] = {
		'p',
		'l',
		'a',
		'i',
		'n',
		'\n',
		0,
		0xc3,
		0xa9,
		0xf0,
		0x9f,
		0x98,
		0x80,
		0xf4,
		0x8f,
		0xbf,
		0xbf,
	};
	JsonReader reader;
	ByteBuffer text = {0};
	jsonReader_init(&reader, json, sizeof json - 1);
	ASSERT_BOOL_EQ(jsonReader_readString(&reader, &text), true);
	ASSERT_BOOL_EQ(text.length == sizeof expected, true);
	if (text.length == sizeof expected) {
		ASSERT_INT_EQ(memcmp(text.data, expected, sizeof expected), 0);
	}
	buffer_free(&text);
	static const char annotation[] =
		"{\"ignored\":[18446744073709551616,"
		"12345678901234567890123456789012345678901234567890123456789012345678901234567890,"
		"-123456789012345678901234567890.5e+1234567890,{\"\\u0000\":\"skip\\uD83D\\uDE00\"}]}";
	jsonReader_init(&reader, annotation, sizeof annotation - 1);
	ASSERT_BOOL_EQ(jsonReader_skipValue(&reader), true);
	ASSERT_BOOL_EQ(reader.position == reader.end, true);
	for (size_t i = 0; i < sizeof json - 2; ++i) {
		jsonReader_init(&reader, json, i);
		ASSERT_BOOL_EQ(jsonReader_readString(&reader, NULL), false);
	}
}

TEST(reads_replacement_metadata_during_encoding) {
	static const char *const configs[] = {
		"{\"format\":\"fm24-match-config/1\",\"away_team\":677,\"fields\":[{\"u8\":42}]}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[{\"u8\":42}],\"away_team\":4294967295}",
		"{\"format\":\"fm24-match-config/1\",\"fields\":[{\"u8\":42}]}",
		"{\"format\":\"fm24-match-config/1\",\"home_team\":678,\"fields\":[{\"u8\":42}]}",
		"{\"format\":\"fm24-match-config/"
		"1\",\"home_team\":678,\"fields\":[{\"u8\":42}],\"away_team\":677}",
	};
	static const uint32_t expected[][2] = {{0, 677}, {0, UINT32_MAX}, {0, 0}, {678, 0}, {678, 677}};
	for (size_t i = 0; i < sizeof configs / sizeof configs[0]; ++i) {
		ByteBuffer bytes = {0};
		char error[256] = "";
		uint32_t teams[2] = {123, 456};
		ASSERT_BOOL_EQ(
			matchConfig_encode(configs[i], strlen(configs[i]), &bytes, teams, error, sizeof error), true
		);
		ASSERT_BOOL_EQ(teams[0] == expected[i][0] && teams[1] == expected[i][1], true);
		ASSERT_BOOL_EQ(bytes.length == 1 && bytes.data[0] == 42, true);
		buffer_free(&bytes);
	}
	ByteBuffer bytes = {0};
	char error[256] = "";
	uint32_t teams[2] = {123, 456};
	const char invalid[] = "{\"format\":\"fm24-match-config/1\",\"home_team\":678,\"away_team\":-1}";
	ASSERT_BOOL_EQ(
		matchConfig_encode(invalid, sizeof invalid - 1, &bytes, teams, error, sizeof error), false
	);
	ASSERT_BOOL_EQ(teams[0] == 0 && teams[1] == 0 && error[0] != 0, true);
	buffer_free(&bytes);
}

TEST(reads_baseline_fixture) {
	FILE *file = fopen(baselinePath, "rb");
	ASSERT_BOOL_EQ(file != NULL, true);
	if (!file) {
		return;
	}
	ByteBuffer json = {0};
	char chunk[16384];
	while (!feof(file) && !ferror(file)) {
		size_t length = fread(chunk, 1, sizeof chunk, file);
		ASSERT_BOOL_EQ(buffer_append(&json, chunk, length), true);
	}
	ASSERT_INT_EQ(ferror(file), 0);
	ASSERT_INT_EQ(fclose(file), 0);
	FixtureData *data = malloc(sizeof *data);
	ASSERT_BOOL_EQ(data != NULL, true);
	if (!data) {
		buffer_free(&json);
		return;
	}
	char error[256] = "";
	bool ok = fixtureData_read(buffer_string(&json), json.length, data, error, sizeof error);
	if (!ok) {
		fprintf(stderr, "%s\n", error);
	}
	ASSERT_BOOL_EQ(ok, true);
	ASSERT_INT_EQ(strcmp(data->teams[0].name, "Aston Villa"), 0);
	ASSERT_INT_EQ(strcmp(data->teams[1].name, "Bristol City"), 0);
	ASSERT_BOOL_EQ(data->teams[0].playerCount.value == 26, true);
	ASSERT_BOOL_EQ(data->teams[1].playerCount.value == 21, true);
	ASSERT_BOOL_EQ(data->teams[0].lineupKnown && !data->teams[1].lineupKnown, true);
	ASSERT_INT_EQ(strcmp(data->teams[0].formation, "4-2-2-2"), 0);
	ASSERT_INT_EQ(strcmp(data->teams[1].formation, "4-2-3-1"), 0);
	ASSERT_INT_EQ(strcmp(data->teams[1].tactic, "4-2-3-1 DM AM Wide"), 0);
	ASSERT_INT_EQ(strcmp(data->teams[0].players[9].name, "Moussa Diaby"), 0);
	ASSERT_INT_EQ(strcmp(data->teams[0].players[25].name, "Travis Patterson"), 0);
	ASSERT_BOOL_EQ(data->teams[0].players[0].morale.value == 15, true);
	ASSERT_BOOL_EQ(data->teams[0].players[0].condition.value == 10000, true);
	ASSERT_BOOL_EQ(data->teams[0].players[0].sharpness[0].value == 5600, true);
	ASSERT_BOOL_EQ(data->teams[0].players[0].fatigue.value == 65396, true);
	ASSERT_INT_EQ(strcmp(data->officials[4], "Aimee Keir"), 0);
	ASSERT_INT_EQ(strcmp(data->when, "2023-07-05 19:30"), 0);
	printf(
		"baseline: %s [%s] vs %s [%s], %llu players\n",
		data->teams[0].name,
		data->teams[0].formation,
		data->teams[1].name,
		data->teams[1].formation,
		(unsigned long long)(data->teams[0].playerCount.value + data->teams[1].playerCount.value)
	);
	free(data);
	buffer_free(&json);
}

TEST(reads_unlabelled_config) {
	checkConfig(false, false);
}

TEST(reads_labelled_config) {
	checkConfig(true, false);
}

TEST(reads_swapped_control) {
	checkConfig(true, true);
}

void registerFixtureDataTests(void) {
	test_run("fixture data reads unlabelled configs", test_reads_unlabelled_config);
	test_run("fixture data reads labelled configs", test_reads_labelled_config);
	test_run("fixture data identifies the controlled side", test_reads_swapped_control);
	test_run("fixture data reads export dates", test_reads_export_dates);
	test_run("fixture data decodes tactic masks", test_decodes_tactic_masks);
	test_run("fixture data rejects invalid configs", test_rejects_invalid_configs);
	test_run("fixture data encodes and visits config fields", test_encodes_and_visits_config_fields);
	test_run(
		"JSON validates decoded and skipped strings", test_validates_json_strings_and_skipped_values
	);
	test_run(
		"config encoding reads replacement metadata once",
		test_reads_replacement_metadata_during_encoding
	);
}

void registerFixtureBaselineTests(const char *path) {
	baselinePath = path;
	test_run("fixture data reads the baseline fixture", test_reads_baseline_fixture);
}
