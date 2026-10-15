// SPDX-License-Identifier: GPL-3.0-or-later
#include "tests.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#endif


int main(int argc, char **argv) {
	static const struct {
		const char *name;
		int (*run)(int argc, char **argv);
	} tests[] = {
#ifdef _WIN32
		{"job", job_test},
		{"runner_main", runner_main_test},
		{"fixture_window", fixture_window_test},
#endif
		{"fixture_data", fixture_data_test},
		{"fixture_export", fixture_export_test},
	};
	unsigned failures = 0;
	for (size_t i = 0; i < sizeof tests / sizeof tests[0]; ++i) {
		if (argc > 1 && strcmp(argv[1], tests[i].name) != 0) {
			continue;
		}
		if (argc > 1) {
			return tests[i].run(argc - 1, argv + 1);
		}
		printf("Running %s\n", tests[i].name);
		fflush(stdout);
#ifdef _WIN32
		const char *arguments[] = {"fm24_tests", tests[i].name, NULL};
		intptr_t result = _spawnvp(_P_WAIT, argv[0], arguments);
		if (result == -1) {
			perror("cannot start test");
		}
#else
		int result = tests[i].run(argc, argv);
#endif
		printf("%s: %s\n", tests[i].name, result ? "FAILED" : "passed");
		failures += result != 0;
	}
	if (argc > 1) {
		fprintf(stderr, "unknown test: %s\n", argv[1]);
		return 1;
	}
	return failures ? 1 : 0;
}
