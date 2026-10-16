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
	fputc('\n', stderr);
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

TEST(replaces_repeated_seed_lists) {
	Job job = {0};
	char first[] = "1-3,4294967295";
	char second[] = "8-80";
	char keep[] = "keep";
	ASSERT_BOOL_EQ(parseLine(&job, "seeds", first), true);
	ASSERT_BOOL_EQ(job.seedCount == 4, true);
	ASSERT_BOOL_EQ(job.seeds[3] == UINT32_MAX, true);
	ASSERT_BOOL_EQ(parseLine(&job, "seeds", second), true);
	ASSERT_BOOL_EQ(job.seedCount == 73, true);
	if (job.seedCount == 73) {
		ASSERT_BOOL_EQ(job.seeds[0] == 8 && job.seeds[72] == 80, true);
	}
	ASSERT_BOOL_EQ(parseLine(&job, "seeds", keep), true);
	ASSERT_BOOL_EQ(job.keepSeed, true);
	ASSERT_BOOL_EQ(job.seeds == NULL && job.seedCount == 0, true);
	job_free(&job);
}

TEST(rejects_invalid_seeds) {
	char invalid[][32] = {
		"", ",1", "1,", "1,,2", "-1", "+1", " 1", "1 ", "2-1", "1--2", "4294967296", "0-10000001"};
	for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		Job job = {0};
		ASSERT_BOOL_EQ(parseLine(&job, "seeds", invalid[i]), false);
		job_free(&job);
	}
	Job job = {0};
	ASSERT_BOOL_EQ(parseLine(&job, "seed", "4294967295"), true);
	ASSERT_BOOL_EQ(job.hasSeed && job.seed == UINT32_MAX, true);
	char invalidNumbers[][32] = {"", "-1", "+1", " 1", "1x", "4294967296", "18446744073709551616"};
	for (size_t i = 0; i < sizeof invalidNumbers / sizeof invalidNumbers[0]; ++i) {
		ASSERT_BOOL_EQ(parseLine(&job, "seed", invalidNumbers[i]), false);
		ASSERT_BOOL_EQ(job.hasSeed && job.seed == UINT32_MAX, true);
	}
	ASSERT_BOOL_EQ(parseLine(&job, "seed", "random"), true);
	ASSERT_BOOL_EQ(job.hasSeed || job.seed != 0, false);
}

void registerJobTests(void) {
	test_run(
		"job parses progress window handles and rejects invalid values", test_parses_progress_window
	);
	test_run(
		"job replaces repeated seed lists without retaining old seeds",
		test_replaces_repeated_seed_lists
	);
	test_run("job rejects malformed and overflowing seeds", test_rejects_invalid_seeds);
}
