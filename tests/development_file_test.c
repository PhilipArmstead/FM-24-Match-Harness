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

void registerDevelopmentFileTests(void) {
	test_run("DLL reads monthly match points in order", test_parses_match_points_by_month);
}
