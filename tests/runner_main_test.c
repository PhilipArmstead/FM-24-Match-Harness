// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdlib.h>
#include <windows.h>

static DWORD tempPath(DWORD size, wchar_t *out);

#define GetTempPathW tempPath
#define wmain runnerMain
#include "../src/runner/main.c" // NOLINT(bugprone-suspicious-include): test startup with stubs.
#undef wmain
#undef GetTempPathW

#define CHECK(expression)                                                                          \
	do {                                                                                             \
		if (!(expression)) {                                                                           \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                             \
			exit(EXIT_FAILURE); /* NOLINT(concurrency-mt-unsafe): single-threaded test. */               \
		}                                                                                              \
	} while (0)

static wchar_t directory[HARNESS_PATH_LENGTH + 1] = L"C:\\Temp\\";
static DWORD directoryLength = 8;
static int exportResult;
static unsigned exports;
static unsigned opens;
static bool expectTemporary = true;
static wchar_t exportedPath[HARNESS_PATH_LENGTH];

static DWORD tempPath(DWORD size, wchar_t *out) {
	CHECK(size == HARNESS_PATH_LENGTH);
	if (directoryLength && directoryLength < size) {
		wmemcpy(out, directory, directoryLength + 1);
	}
	return directoryLength;
}

int commands_getNextFixture(int argc, wchar_t **argv) {
	++exports;
	CHECK(argc == 3);
	CHECK(wcscmp(argv[0], L"fm24ctl.exe") == 0);
	CHECK(wcscmp(argv[1], L"get-next-fixture") == 0);
	CHECK(argv[3] == NULL);
	CHECK(wcslen(argv[2]) == directoryLength + wcslen(L"temp-fixture.json"));
	CHECK(wcsncmp(argv[2], directory, directoryLength) == 0);
	CHECK(wcscmp(argv[2] + directoryLength, L"temp-fixture.json") == 0);
	wcscpy(exportedPath, argv[2]);
	return exportResult;
}

int fixtureWindow_open(const wchar_t *path, bool temporary) {
	++opens;
	CHECK(wcscmp(path, exportedPath) == 0);
	CHECK(temporary == expectTemporary);
	return 9;
}

bool files_hasExtension(const wchar_t *path, const wchar_t *suffix) {
	size_t length = wcslen(path);
	size_t suffixLength = wcslen(suffix);
	return length >= suffixLength && wcscmp(path + length - suffixLength, suffix) == 0;
}

int commands_run(int argc, wchar_t **argv) {
	(void)argc;
	(void)argv;
	return 10;
}

int commands_encode(int argc, wchar_t **argv) {
	(void)argc;
	(void)argv;
	return 11;
}

int commands_probe(int argc, wchar_t **argv) {
	(void)argc;
	(void)argv;
	return 12;
}

int main(void) {
	wchar_t *arguments[] = {L"fm24ctl.exe", NULL, NULL};
	CHECK(runnerMain(1, arguments) == 9);
	CHECK(exports == 1 && opens == 1);
	CHECK(wcscmp(exportedPath, L"C:\\Temp\\temp-fixture.json") == 0);

	exportResult = 7;
	CHECK(runnerMain(1, arguments) == 7);
	CHECK(exports == 2 && opens == 1);

	directoryLength = 0;
	CHECK(runnerMain(1, arguments) == 1);
	directoryLength = HARNESS_PATH_LENGTH;
	CHECK(runnerMain(1, arguments) == 1);
	directoryLength = HARNESS_PATH_LENGTH - (DWORD)wcslen(L"temp-fixture.json");
	CHECK(runnerMain(1, arguments) == 1);
	CHECK(exports == 2 && opens == 1);

	--directoryLength;
	wmemset(directory, L'x', directoryLength);
	directory[directoryLength - 1] = L'\\';
	directory[directoryLength] = 0;
	exportResult = 0;
	CHECK(runnerMain(1, arguments) == 9);
	CHECK(exports == 3 && opens == 2);

	arguments[1] = L"run";
	CHECK(runnerMain(2, arguments) == 10);
	arguments[1] = L"encode";
	CHECK(runnerMain(2, arguments) == 11);
	arguments[1] = L"probe";
	CHECK(runnerMain(2, arguments) == 12);
	arguments[1] = exportedPath;
	expectTemporary = false;
	CHECK(runnerMain(2, arguments) == 9);
	CHECK(exports == 3 && opens == 3);
	arguments[1] = L"unknown";
	CHECK(runnerMain(2, arguments) == 2);
	return 0;
}
