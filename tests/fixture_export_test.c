// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture_export.h"
#include "tests.h"

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

int fixture_export_test(int argc, char **argv) {
	(void)argc;
	(void)argv;
	FixtureData *data = calloc(1, sizeof *data);
	CHECK(data != NULL);
	wchar_t filename[FIXTURE_EXPORT_FILENAME_LENGTH];
	(void)snprintf(data->teams[0].name, FIXTURE_TEXT, "Aston Villa");
	(void)snprintf(data->teams[1].name, FIXTURE_TEXT, "Bristol City");
	CHECK(fixtureExport_filename(data, 1792074287, filename, FIXTURE_EXPORT_FILENAME_LENGTH));
	CHECK(wcscmp(filename, L"aston-villa-v-bristol-city-1792074287.json") == 0);

	(void)snprintf(data->teams[0].name, FIXTURE_TEXT, "M\xc3\xbcnchen\t\r\n\x01\x7f/\\:*?\"<>|");
	(void)snprintf(data->teams[1].name, FIXTURE_TEXT, "FC  CITY-1.2&'()");
	CHECK(fixtureExport_filename(data, 42, filename, FIXTURE_EXPORT_FILENAME_LENGTH));
	CHECK(wcscmp(filename, L"mnchen-v-fc--city-1.2&'()-42.json") == 0);
	CHECK(!fixtureExport_filename(data, 42, filename, 8));

	data->teams[0].name[0] = 0;
	data->teams[1].name[0] = 0;
	CHECK(fixtureExport_filename(data, 0, filename, FIXTURE_EXPORT_FILENAME_LENGTH));
	CHECK(wcscmp(filename, L"-v--0.json") == 0);

	memset(data->teams[0].name, 'A', FIXTURE_TEXT - 1);
	CHECK(!fixtureExport_filename(data, 42, filename, FIXTURE_EXPORT_FILENAME_LENGTH));
	size_t homeLength = 255 - wcslen(L"-v--42.json");
	data->teams[0].name[homeLength] = 0;
	CHECK(fixtureExport_filename(data, 42, filename, FIXTURE_EXPORT_FILENAME_LENGTH));
	CHECK(wcslen(filename) == 255);
	data->teams[0].name[homeLength] = 'A';
	data->teams[0].name[homeLength + 1] = 0;
	wchar_t longer[512];
	CHECK(!fixtureExport_filename(data, 42, longer, 512));
	free(data);
	return 0;
}
