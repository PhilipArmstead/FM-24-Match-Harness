// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.h"
#include "tests.h"

#include <windows.h>


static DWORD tempPath(DWORD size, wchar_t *out);

#define GetTempPathW tempPath
#define wmain runnerMain
#define fixtureWindow_open runnerTest_open
#define files_hasExtension runnerTest_hasExtension
#include "../src/runner/main.c" // NOLINT(bugprone-suspicious-include): test startup with stubs.


#undef wmain
#undef GetTempPathW

static wchar_t directory[HARNESS_PATH_LENGTH + 1] = L"C:\\Temp\\";
static DWORD directoryLength = 8;
static int exportResult;
static unsigned exports;
static unsigned opens;
static bool expectTemporary = true;
static wchar_t exportedPath[HARNESS_PATH_LENGTH];

static DWORD tempPath(DWORD size, wchar_t *out) {
	ASSERT_BOOL_EQ(size == HARNESS_PATH_LENGTH, true);
	if (directoryLength && directoryLength < size) {
		wmemcpy(out, directory, directoryLength + 1);
	}
	return directoryLength;
}

int commands_getNextFixture(int argc, wchar_t **argv) {
	++exports;
	ASSERT_INT_EQ(argc, 3);
	ASSERT_INT_EQ(wcscmp(argv[0], L"fm24ctl.exe"), 0);
	ASSERT_INT_EQ(wcscmp(argv[1], L"get-next-fixture"), 0);
	ASSERT_BOOL_EQ(argv[3] == NULL, true);
	ASSERT_BOOL_EQ(wcslen(argv[2]) == directoryLength + wcslen(L"temp-fixture.json"), true);
	ASSERT_INT_EQ(wcsncmp(argv[2], directory, directoryLength), 0);
	ASSERT_INT_EQ(wcscmp(argv[2] + directoryLength, L"temp-fixture.json"), 0);
	wcscpy(exportedPath, argv[2]);
	return exportResult;
}

int fixtureWindow_open(const wchar_t *path, bool temporary) {
	++opens;
	ASSERT_INT_EQ(wcscmp(path, exportedPath), 0);
	ASSERT_BOOL_EQ(temporary, expectTemporary);
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

int commands_replaceTeam(int argc, wchar_t **argv) {
	(void)argc;
	(void)argv;
	return 0;
}

int commands_listTeams(int argc, wchar_t **argv) {
	(void)argc;
	(void)argv;
	return 13;
}

int commands_getPlayer(int argc, wchar_t **argv) {
	(void)argc;
	(void)argv;
	return 14;
}

int commands_injectPlayer(int argc, wchar_t **argv) {
	(void)argc;
	(void)argv;
	return 15;
}

int commands_probe(int argc, wchar_t **argv) {
	(void)argc;
	(void)argv;
	return 12;
}

TEST(exports_next_fixture_and_dispatches_commands) {
	wchar_t *arguments[] = {L"fm24ctl.exe", NULL, NULL};
	ASSERT_INT_EQ(runnerMain(1, arguments), 9);
	ASSERT_BOOL_EQ(exports == 1 && opens == 1, true);
	ASSERT_INT_EQ(wcscmp(exportedPath, L"C:\\Temp\\temp-fixture.json"), 0);

	exportResult = 7;
	ASSERT_INT_EQ(runnerMain(1, arguments), 7);
	ASSERT_BOOL_EQ(exports == 2 && opens == 1, true);

	directoryLength = 0;
	ASSERT_INT_EQ(runnerMain(1, arguments), 1);
	directoryLength = HARNESS_PATH_LENGTH;
	ASSERT_INT_EQ(runnerMain(1, arguments), 1);
	directoryLength = HARNESS_PATH_LENGTH - (DWORD)wcslen(L"temp-fixture.json");
	ASSERT_INT_EQ(runnerMain(1, arguments), 1);
	ASSERT_BOOL_EQ(exports == 2 && opens == 1, true);

	--directoryLength;
	wmemset(directory, L'x', directoryLength);
	directory[directoryLength - 1] = L'\\';
	directory[directoryLength] = 0;
	exportResult = 0;
	ASSERT_INT_EQ(runnerMain(1, arguments), 9);
	ASSERT_BOOL_EQ(exports == 3 && opens == 2, true);

	arguments[1] = L"run";
	ASSERT_INT_EQ(runnerMain(2, arguments), 10);
	arguments[1] = L"encode";
	ASSERT_INT_EQ(runnerMain(2, arguments), 11);
	arguments[1] = L"probe";
	ASSERT_INT_EQ(runnerMain(2, arguments), 12);
	arguments[1] = L"replace-home";
	ASSERT_INT_EQ(runnerMain(2, arguments), 0);
	arguments[1] = L"replace-away";
	ASSERT_INT_EQ(runnerMain(2, arguments), 0);
	arguments[1] = L"list-teams";
	ASSERT_INT_EQ(runnerMain(2, arguments), 13);
	arguments[1] = L"get-player";
	ASSERT_INT_EQ(runnerMain(2, arguments), 14);
	arguments[1] = L"inject-player";
	ASSERT_INT_EQ(runnerMain(2, arguments), 15);
	arguments[1] = exportedPath;
	expectTemporary = false;
	ASSERT_INT_EQ(runnerMain(2, arguments), 9);
	ASSERT_BOOL_EQ(exports == 3 && opens == 3, true);
	arguments[1] = L"unknown";
	ASSERT_INT_EQ(runnerMain(2, arguments), 2);
}

void registerRunnerMainTests(void) {
	test_run(
		"runner exports the next fixture and dispatches commands",
		test_exports_next_fixture_and_dispatches_commands
	);
}
