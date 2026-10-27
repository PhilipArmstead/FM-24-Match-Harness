// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/dll/development_file.h"
#include "../src/dll/sharpness.h"
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

TEST(parses_focus_by_month) {
	DevelopmentValues *values = malloc(sizeof *values);
	char error[160] = "";
	ASSERT_BOOL_EQ(values != NULL, true);
	ASSERT_BOOL_EQ(
		developmentFile_parse(
			"training_focus:09,training_focus.month1:ff,training_focus.month2:0f,"
			"training_focus.month3:1a",
			values,
			error,
			sizeof error
		),
		true
	);
	ASSERT_INT_EQ((int)values->count, 1); // training_focus itself is a table field
	ASSERT_INT_EQ((int)values->focusMonths, 3);
	ASSERT_INT_EQ(values->focus[0], 255);
	ASSERT_INT_EQ(values->focus[1], 15);
	ASSERT_INT_EQ(values->focus[2], 26);
	static const char *const invalid[] = {
		"training_focus.month2:0f",                       // month 1 missing
		"training_focus.month1:1b",                       // 27: no such focus
		"training_focus.month1:fe",                       // 254 likewise
		"training_focus.month1:0f0f",                     // not a u8
		"training_focus.month1:0g",                       // bad digit
		"training_focus.month1:0f,training_focus.month1:0f", // repeated
	};
	for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		ASSERT_BOOL_EQ(developmentFile_parse(invalid[i], values, error, sizeof error), false);
	}
	free(values);
}

TEST(parses_matches_by_month) {
	DevelopmentValues *values = malloc(sizeof *values);
	char error[160] = "";
	ASSERT_BOOL_EQ(values != NULL, true);
	ASSERT_BOOL_EQ(
		developmentFile_parse(
			"matches.month1.minutes:5a00,matches.month1.count:0400,matches.month2.count:0000,"
			"matches.month2.minutes:0000",
			values,
			error,
			sizeof error
		),
		true
	);
	ASSERT_INT_EQ((int)values->playMonths, 2);
	ASSERT_INT_EQ(values->playCount[0], 4);
	ASSERT_INT_EQ(values->playMinutes[0], 90);
	ASSERT_INT_EQ(values->playCount[1], 0);
	static const char *const invalid[] = {
		"matches.month1.count:0400",                                // no minutes
		"matches.month2.count:0400,matches.month2.minutes:5a00",    // month 1 missing
		"matches.month1.count:2000,matches.month1.minutes:5a00",    // 32 matches
		"matches.month1.count:0400,matches.month1.minutes:7900",    // 121 minutes
		"matches.month1.goals:0400",                                // unknown part
	};
	for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		ASSERT_BOOL_EQ(developmentFile_parse(invalid[i], values, error, sizeof error), false);
	}
	free(values);
}

// Measured with the game's match engine (knowledge-base section 21): start, minutes, x, end.
TEST(moves_sharpness_as_the_engine_does) {
	static const unsigned cases[][4] = {
		{6100, 90, 5, 7000},   // below 7500 throughout: 10 a minute
		{6900, 90, 2, 7713},   // across 7500 (x below 6 counts as 5)
		{8450, 90, 5, 9025},   // across 9000
		{9000, 90, 5, 9180},   // above 9000: 2 a minute
		{9900, 90, 5, 10000},  // capped
		{8900, 90, 6, 9242},   // x 6
		{7300, 90, 20, 9160},  // x above 15 counts as 15; across both thresholds
		{4100, 45, 6, 4640},   // half a match
		{6000, 0, 15, 6000},   // didn't play
	};
	for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
		ASSERT_INT_EQ(sharpness_afterMatch((uint16_t)cases[i][0], cases[i][1], cases[i][2]), (int)cases[i][3]);
	}
}

TEST(draws_the_factor_as_seen) {
	uint64_t random = 7;
	unsigned top = 0;
	for (int i = 0; i < 9000; ++i) {
		unsigned x = sharpness_drawFactor(&random);
		ASSERT_BOOL_EQ(x >= 5 && x <= 15, true);
		top += x == 15;
	}
	ASSERT_BOOL_EQ(top > 4400 && top < 5000, true); // 47 in 90 of the values seen
	uint64_t again = 7;
	uint64_t first = 7;
	ASSERT_INT_EQ((int)sharpness_drawFactor(&again), (int)sharpness_drawFactor(&first));
}

void registerDevelopmentFileTests(void) {
	test_run("DLL reads planned matches month by month", test_parses_matches_by_month);
	test_run("DLL moves sharpness as the match engine does", test_moves_sharpness_as_the_engine_does);
	test_run("DLL draws the sharpness factor as the game sets it", test_draws_the_factor_as_seen);
	test_run("DLL reads the training focus month by month", test_parses_focus_by_month);
	test_run("DLL reads monthly match points in order", test_parses_match_points_by_month);
	test_run("DLL reads planned injury windows", test_parses_injury_windows);
}
