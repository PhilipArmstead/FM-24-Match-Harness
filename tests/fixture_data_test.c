// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture_data.h"
#include "match_config.h"
#include "tests.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define CHECK(expression)                                                                          \
	do {                                                                                             \
		if (!(expression)) {                                                                           \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                             \
			exit(EXIT_FAILURE); /* NOLINT(concurrency-mt-unsafe): single-threaded test. */               \
		}                                                                                              \
	} while (0)

static bool append(ByteBuffer *out, const char *format, ...) {
	char text[2048];
	va_list arguments;
	va_start(arguments, format);
	int length = vsnprintf(text, sizeof text, format, arguments);
	va_end(arguments);
	return length >= 0 && (size_t)length < sizeof text && buffer_append(out, text, (size_t)length);
}

static bool makeConfig(ByteBuffer *out, bool labelled, bool swapControl, const char *source) {
	CHECK(append(
		out,
		"{\"format\":\"fm24-match-config/1\",\"fields\":["
		"{\"name\":\"fixture\",\"u8\":24},{\"str\":\"Friendly\"},"
		"{\"name\":\"venue\",\"u32\":0},{\"str\":\"Villa Park\"},"
		"{\"name\":\"official[0]\",\"u8\":0},"
		"{\"name\":\"referee\",\"u8\":8},{\"str\":\"Aimee\"},{\"str\":\"Keir\"},"
		"{\"str\":\"Aimee Keir\"}"
	));
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
		CHECK(append(
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
		));
		for (unsigned i = 0; i < 11; ++i) {
			CHECK(append(
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
			));
		}
		CHECK(append(out, ",{\"name\":\"%s.player_count\",\"u8\":12}", name));
		for (unsigned i = 0; i < 12; ++i) {
			CHECK(append(
				out,
				",{\"name\":\"%s.player[%u]\",\"player\":\"Stale name\",\"u8\":255},"
				"{\"name\":\"%s.player[%u].ratings\",\"u8map\":{",
				name,
				i,
				name,
				i
			));
			for (unsigned rating = 0; rating < 69; ++rating) {
				CHECK(append(out, "%s\"r%u\":20", rating ? "," : "", rating));
			}
			CHECK(append(out, "}}"));
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
				CHECK(append(out, ",{\"%s\":%u}", types[field], values[field]));
			}
			CHECK(append(
				out,
				",{\"str\":\"Emiliano\"},{\"str\":\"Mart\\u00ednez\"},"
				"{\"str\":\"Emiliano Mart\\u00ednez\"}"
			));
			if (labelled) {
				CHECK(append(
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
				));
			}
		}
	}
	CHECK(append(out, "]"));
	if (source) {
		CHECK(append(out, ",\"source\":\"%s\"", source));
	}
	CHECK(append(out, "}"));
	return true;
}

static bool checkConfig(bool labelled, bool swapControl) {
	ByteBuffer json = {0};
	CHECK(makeConfig(
		&json, labelled, swapControl, "Aston Villa vs Bristol City, 2023 day 186 19:30, seed 42"
	));
	FixtureData *data = malloc(sizeof *data);
	CHECK(data != NULL);
	char error[256] = "";
	bool ok = fixtureData_read(buffer_string(&json), json.length, data, error, sizeof error);
	if (!ok) {
		fprintf(stderr, "%s\n", error);
	}
	CHECK(ok);
	CHECK(strcmp(data->teams[0].name, "Aston Villa") == 0);
	CHECK(strcmp(data->teams[1].name, "Bristol City") == 0);
	CHECK(strcmp(data->teams[0].formation, "4-2-2-2") == 0);
	CHECK(strcmp(data->teams[0].tactic, "Custom 4-2-2-2") == 0);
	CHECK(data->teams[0].lineupKnown == !swapControl);
	CHECK(data->teams[1].lineupKnown == swapControl);
	CHECK(strcmp(data->competition, "Friendly") == 0);
	CHECK(strcmp(data->matchType, "Friendly") == 0);
	CHECK(strcmp(data->venue, "Villa Park") == 0);
	CHECK(strcmp(data->officials[4], "Aimee Keir") == 0);
	CHECK(!data->officials[0][0]);
	CHECK(strcmp(data->when, "2023-07-05 19:30") == 0);
	CHECK(strcmp(data->teams[0].players[0].name, "Emiliano Mart\xC3\xADnez") == 0);
	CHECK(data->teams[0].players[0].condition.known);
	CHECK(data->teams[0].players[0].condition.value == (labelled ? 9876U : 10000U));
	CHECK(data->teams[1].players[11].morale.value == (labelled ? 12U : 15U));
	CHECK(data->teams[0].players[0].sharpness[0].value == (labelled ? 8125U : 5600U));
	CHECK(data->teams[0].players[0].sharpness[1].value == 5650);
	CHECK(data->teams[0].players[0].fatigue.value == (labelled ? 65535U : 65396U));
	CHECK(!data->teams[0].players[12].present);
	free(data);
	buffer_free(&json);
	return true;
}

static bool checkDates(void) {
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
	CHECK(data != NULL);
	for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		ByteBuffer json = {0};
		CHECK(makeConfig(&json, false, false, cases[i].source));
		char error[256] = "";
		CHECK(fixtureData_read(buffer_string(&json), json.length, data, error, sizeof error));
		CHECK(strcmp(data->when, cases[i].when) == 0);
		buffer_free(&json);
	}
	free(data);
	return true;
}

static bool checkMasks(void) {
	char text[32];
	fixtureData_position(1048704, text, sizeof text);
	CHECK(strcmp(text, "DMCL") == 0);
	fixtureData_position(2097168, text, sizeof text);
	CHECK(strcmp(text, "DCR") == 0);
	fixtureData_position(0, text, sizeof text);
	CHECK(strcmp(text, "Unknown") == 0);
	fixtureData_position(3145744, text, sizeof text);
	CHECK(strcmp(text, "Unknown") == 0);
	CHECK(strcmp(fixtureData_role(2151677952ULL), "pressing_forward") == 0);
	CHECK(strcmp(fixtureData_duty(2151677952ULL), "Support") == 0);
	CHECK(strcmp(fixtureData_role((1ULL << 50) | (1ULL << 23)), "segundo_volante") == 0);
	CHECK(strcmp(fixtureData_duty((1ULL << 2) | (1ULL << 34)), "Automatic") == 0);
	CHECK(strcmp(fixtureData_role(3), "Unknown") == 0);
	CHECK(strcmp(fixtureData_duty((1ULL << 21) | (1ULL << 22)), "Unknown") == 0);
	return true;
}

static bool checkInvalid(void) {
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
	};
	FixtureData *data = malloc(sizeof *data);
	CHECK(data != NULL);
	for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		char error[256] = "";
		ByteBuffer bytes = {0};
		CHECK(!matchConfig_encode(invalid[i], strlen(invalid[i]), &bytes, error, sizeof error));
		CHECK(error[0]);
		buffer_free(&bytes);
		error[0] = 0;
		CHECK(!fixtureData_read(invalid[i], strlen(invalid[i]), data, error, sizeof error));
		CHECK(error[0]);
	}
	free(data);
	return true;
}

typedef struct Visited {
	ByteBuffer bytes;
	bool ok;
} Visited;

static void collect(void *context, const ConfigField *field) {
	Visited *visited = context;
	visited->ok = visited->ok && buffer_append(&visited->bytes, field->bytes, field->length);
}

static bool checkEncoder(void) {
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
	CHECK(matchConfig_encode(json, sizeof json - 1, &bytes, error, sizeof error));
	CHECK(bytes.length == sizeof expected);
	CHECK(memcmp(bytes.data, expected, sizeof expected) == 0);
	Visited visited = {.ok = true};
	ConfigVisitor visitor = {.context = &visited, .field = collect};
	CHECK(matchConfig_visit(json, sizeof json - 1, &visitor, error, sizeof error));
	CHECK(visited.ok);
	CHECK(visited.bytes.length == bytes.length);
	CHECK(memcmp(visited.bytes.data, bytes.data, bytes.length) == 0);
	buffer_free(&bytes);
	buffer_free(&visited.bytes);
	return true;
}

static bool checkBaseline(const char *path) {
	FILE *file = fopen(path, "rb");
	CHECK(file != NULL);
	ByteBuffer json = {0};
	char chunk[16384];
	while (!feof(file) && !ferror(file)) {
		size_t length = fread(chunk, 1, sizeof chunk, file);
		CHECK(buffer_append(&json, chunk, length));
	}
	CHECK(!ferror(file));
	CHECK(fclose(file) == 0);
	FixtureData *data = malloc(sizeof *data);
	CHECK(data != NULL);
	char error[256] = "";
	bool ok = fixtureData_read(buffer_string(&json), json.length, data, error, sizeof error);
	if (!ok) {
		fprintf(stderr, "%s\n", error);
	}
	CHECK(ok);
	CHECK(strcmp(data->teams[0].name, "Aston Villa") == 0);
	CHECK(strcmp(data->teams[1].name, "Bristol City") == 0);
	CHECK(data->teams[0].playerCount.value == 26);
	CHECK(data->teams[1].playerCount.value == 21);
	CHECK(data->teams[0].lineupKnown && !data->teams[1].lineupKnown);
	CHECK(strcmp(data->teams[0].formation, "4-2-2-2") == 0);
	CHECK(strcmp(data->teams[1].formation, "4-2-3-1") == 0);
	CHECK(strcmp(data->teams[1].tactic, "4-2-3-1 DM AM Wide") == 0);
	CHECK(strcmp(data->teams[0].players[9].name, "Moussa Diaby") == 0);
	CHECK(strcmp(data->teams[0].players[25].name, "Travis Patterson") == 0);
	CHECK(data->teams[0].players[0].morale.value == 15);
	CHECK(data->teams[0].players[0].condition.value == 10000);
	CHECK(data->teams[0].players[0].sharpness[0].value == 5600);
	CHECK(data->teams[0].players[0].fatigue.value == 65396);
	CHECK(strcmp(data->officials[4], "Aimee Keir") == 0);
	CHECK(strcmp(data->when, "2023-07-05 19:30") == 0);
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
	return true;
}

int fixture_data_test(int argc, char **argv) {
	if (!checkConfig(false, false) || !checkConfig(true, false) || !checkConfig(true, true) ||
			!checkDates() || !checkMasks() || !checkInvalid() || !checkEncoder() ||
			(argc > 1 && !checkBaseline(argv[1]))) {
		return 1;
	}
	puts("fixture data checks passed");
	return 0;
}
