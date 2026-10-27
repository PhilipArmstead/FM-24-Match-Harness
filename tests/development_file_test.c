// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/dll/development_file.h"
#include "test.h"
#include "tests.h"

#include <stdlib.h>


TEST(parses_match_points_by_month) {
	DevelopmentValues *values = malloc(sizeof *values);
	char error[160] = "";
	ASSERT_BOOL_EQ(values != NULL, true);
	ASSERT_BOOL_EQ(
		developmentFile_parse(
			"current_ability:3500,match_points.month1:8700,match_points.month2:0f01",
			values,
			error,
			sizeof error
		),
		true
	);
	ASSERT_INT_EQ((int)values->count, 1);
	ASSERT_INT_EQ((int)values->matchMonths, 2);
	ASSERT_INT_EQ(values->matchPoints[0], 135);
	ASSERT_INT_EQ(values->matchPoints[1], 271);
	static const char *const invalid[] = {
		"match_points.month2:8700",                         // month 1 missing
		"match_points.month1:8700,match_points.month1:8700", // repeated
		"match_points.month1:87",                           // not a u16
		"match_points.month1:87zz",
		"match_points.month601:8700",
	};
	for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		ASSERT_BOOL_EQ(developmentFile_parse(invalid[i], values, error, sizeof error), false);
	}
	free(values);
}

TEST(parses_injury_windows) {
	DevelopmentValues *values = malloc(sizeof *values);
	char error[160] = "";
	ASSERT_BOOL_EQ(values != NULL, true);
	ASSERT_BOOL_EQ(
		developmentFile_parse(
			"injury.window1.days:1500,injury.window1.start:3c00,injury.window2.start:0000,"
			"injury.window2.days:0700",
			values,
			error,
			sizeof error
		),
		true
	);
	ASSERT_INT_EQ((int)values->injuryWindows, 2);
	ASSERT_INT_EQ(values->injuryStart[0], 60);
	ASSERT_INT_EQ(values->injuryDays[0], 21);
	ASSERT_INT_EQ(values->injuryStart[1], 0);
	ASSERT_INT_EQ(values->injuryDays[1], 7);
	static const char *const invalid[] = {
		"injury.window1.start:3c00",                                  // no days
		"injury.window1.start:3c00,injury.window1.days:0000",         // 0 days
		"injury.window2.start:3c00,injury.window2.days:1500",         // window 1 missing
		"injury.window1.start:3c00,injury.window1.start:3c00",        // repeated
		"injury.window1.end:3c00",                                    // unknown part
		"injury.window1.days:15",                                     // not a u16
		"injury.window.days:1500",                                    // no number
	};
	for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		ASSERT_BOOL_EQ(developmentFile_parse(invalid[i], values, error, sizeof error), false);
	}
	free(values);
}

void registerDevelopmentFileTests(void) {
	test_run("DLL reads monthly match points in order", test_parses_match_points_by_month);
	test_run("DLL reads planned injury windows", test_parses_injury_windows);
}
