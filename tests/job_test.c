// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tests.h"

#include "../src/dll/job.c" // NOLINT(bugprone-suspicious-include): test internal job parsing.


#define CHECK(expression)                                                                          \
	do {                                                                                             \
		if (!(expression)) {                                                                           \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                             \
			exit(EXIT_FAILURE); /* NOLINT(concurrency-mt-unsafe): single-threaded test. */               \
		}                                                                                              \
	} while (0)

void log_message(const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
}

int job_test(int argc, char **argv) {
	(void)argc;
	(void)argv;
	Job job = {0};
	CHECK(job.progressWindow == 0);
	CHECK(parseLine(&job, "progress_window", "0"));
	CHECK(job.progressWindow == 0);
	CHECK(parseLine(&job, "progress_window", "123456"));
	CHECK(job.progressWindow == 123456);
	char maximum[32];
	(void)snprintf(maximum, sizeof maximum, "%llu", (unsigned long long)UINTPTR_MAX);
	CHECK(parseLine(&job, "progress_window", maximum));
	CHECK(job.progressWindow == UINTPTR_MAX);
	char invalid[][32] = {
		"", "-1", " -1", "+1", " 1", "1 ", "1x", "1\ncommand=run", "18446744073709551616"};
	for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		CHECK(!parseLine(&job, "progress_window", invalid[i]));
		CHECK(job.progressWindow == UINTPTR_MAX);
	}
	job_free(&job);
	CHECK(job.progressWindow == 0);
	return 0;
}
