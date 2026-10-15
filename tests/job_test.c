// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/dll/job.c" // NOLINT(bugprone-suspicious-include): test internal job parsing.
#include "test.h"
#include "tests.h"


void log_message(const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
}

TEST(parses_progress_window) {
	Job job = {0};
	ASSERT_BOOL_EQ(job.progressWindow == 0, true);
	ASSERT_BOOL_EQ(parseLine(&job, "progress_window", "0"), true);
	ASSERT_BOOL_EQ(job.progressWindow == 0, true);
	ASSERT_BOOL_EQ(parseLine(&job, "progress_window", "123456"), true);
	ASSERT_BOOL_EQ(job.progressWindow == 123456, true);
	char maximum[32];
	(void)snprintf(maximum, sizeof maximum, "%llu", (unsigned long long)UINTPTR_MAX);
	ASSERT_BOOL_EQ(parseLine(&job, "progress_window", maximum), true);
	ASSERT_BOOL_EQ(job.progressWindow == UINTPTR_MAX, true);
	char invalid[][32] = {
		"", "-1", " -1", "+1", " 1", "1 ", "1x", "1\ncommand=run", "18446744073709551616"};
	for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		ASSERT_BOOL_EQ(parseLine(&job, "progress_window", invalid[i]), false);
		ASSERT_BOOL_EQ(job.progressWindow == UINTPTR_MAX, true);
	}
	job_free(&job);
	ASSERT_BOOL_EQ(job.progressWindow == 0, true);
}

void registerJobTests(void) {
	test_run(
		"job parses progress window handles and rejects invalid values", test_parses_progress_window
	);
}
