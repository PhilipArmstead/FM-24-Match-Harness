// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "test.h"

#include "tests.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>


static unsigned int testsRun;
static unsigned int testsFailed;
static atomic_bool currentTestFailed;

void test_run(const char *name, const TestFunction fn) {
	currentTestFailed = false;
	++testsRun;
	fn();

	const bool failed = currentTestFailed;
	if (failed) {
		++testsFailed;
	}
	printf("[%s] %s\n", failed ? "FAIL" : "PASS", name);
}

int test_finish(void) {
	printf("%u tests, %u failed\n", testsRun, testsFailed);
	return testsRun == 0 || testsFailed != 0;
}

void test_assertIntEq(
	const int actual,
	const int expected,
	const char *expression,
	const char *file,
	const int line
) {
	if (expected == actual) {
		return;
	}

	currentTestFailed = true;
	fprintf(stderr, "%s:%d: %s expected %d, got %d\n", file, line, expression, expected, actual);
}

void test_assertBoolEq(
	const bool actual,
	const bool expected,
	const char *expression,
	const char *file,
	const int line
) {
	if (expected == actual) {
		return;
	}

	currentTestFailed = true;
	fprintf(
		stderr,
		"%s:%d: %s expected %s, got %s\n",
		file,
		line,
		expression,
		expected ? "true" : "false",
		actual ? "true" : "false"
	);
}

int main(const int argc, const char *argv[]) {
	if (argc > 3 || (argc == 3 && strcmp(argv[1], "fixture_data") != 0)) {
		fprintf(stderr, "usage: %s [suite [baseline.json]]\n", argv[0]);
		return 1;
	}
	static const struct {
		const char *name;
		void (*registerTests)(void);
	} tests[] = {
#if defined(_WIN32) || defined(FM24_DLL_TESTS)
		{"job", registerJobTests},
		{"dll", registerDllTests},
#endif
#if defined(_WIN32) || defined(FM24_RUNNER_TESTS)
		{"runner", registerRunnerTests},
#endif
#ifdef _WIN32
		{"runner_main", registerRunnerMainTests},
		{"fixture_window", registerFixtureWindowTests},
#endif
		{"fixture_data", registerFixtureDataTests},
		{"fixture_export", registerFixtureExportTests},
	};
	for (size_t i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
		if (argc > 1 && strcmp(argv[1], tests[i].name) != 0) {
			continue;
		}
		tests[i].registerTests();
		if (argc == 3) {
			registerFixtureBaselineTests(argv[2]);
		}
		if (argc > 1) {
			return test_finish();
		}
	}
	if (argc > 1) {
		fprintf(stderr, "unknown test suite: %s\n", argv[1]);
		return 1;
	}
	return test_finish();
}
