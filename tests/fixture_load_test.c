// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "client.h"
#include "files.h"
#include "paths.h"
#include "test.h"
#include "tests.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>


static bool inspectSubmit(const char *job, unsigned timeout, char *result, size_t resultSize);
static bool inspectSucceeded(const char *result);

#define fixtureLoad_open fixtureLoadTest_open
#define fixtureLoad_save fixtureLoadTest_save
#define client_submit inspectSubmit
#define client_succeeded inspectSucceeded
#include "../src/runner/fixture_load.c" // NOLINT(bugprone-suspicious-include): isolate the game connection.
#undef fixtureLoad_open
#undef fixtureLoad_save
#undef client_submit
#undef client_succeeded

static const char config[] = "{\"format\":\"fm24-match-config/1\",\"fields\":[{\"u8\":42}],"
														 "\"source\":\"preserve annotations\",\"away_team\":677}";
static unsigned inspectCalls;
static bool inspectFailure;

static bool inspectSubmit(const char *job, unsigned timeout, char *result, size_t resultSize) {
	ASSERT_BOOL_EQ(strncmp(job, "command=inspect\ninput=", 22) == 0, true);
	ASSERT_BOOL_EQ(timeout == CLIENT_SHORT_TIMEOUT_MS, true);
	++inspectCalls;
	if (inspectFailure) {
		(void)snprintf(result, resultSize, "FM24 unavailable");
		return false;
	}
	const char *output = strstr(job, "\noutput=");
	ASSERT_BOOL_EQ(output != NULL, true);
	if (!output) {
		return false;
	}
	output += strlen("\noutput=");
	size_t length = strcspn(output, "\n");
	wchar_t path[HARNESS_PATH_LENGTH];
	int count = MultiByteToWideChar(
		CP_UTF8, MB_ERR_INVALID_CHARS, output, (int)length, path, HARNESS_PATH_LENGTH - 1
	);
	ASSERT_BOOL_EQ(count > 0, true);
	if (count <= 0) {
		return false;
	}
	path[count] = 0;
	ASSERT_BOOL_EQ(files_write(path, config, sizeof config - 1), true);
	(void)snprintf(result, resultSize, "ok: decoded");
	return true;
}

static bool inspectSucceeded(const char *result) {
	return strcmp(result, "ok: decoded") == 0;
}

static void checkFile(const wchar_t *path, const void *expected, size_t size) {
	ByteBuffer contents = {0};
	ASSERT_BOOL_EQ(files_read(path, &contents), true);
	ASSERT_BOOL_EQ(contents.length == size, true);
	if (contents.length == size) {
		ASSERT_INT_EQ(memcmp(contents.data, expected, size), 0);
	}
	buffer_free(&contents);
}

TEST(saves_complete_fixtures_without_partial_overwrites) {
	wchar_t temporary[HARNESS_PATH_LENGTH];
	wchar_t directory[HARNESS_PATH_LENGTH];
	DWORD length = GetTempPathW(HARNESS_PATH_LENGTH, temporary);
	ASSERT_BOOL_EQ(length > 0 && length < HARNESS_PATH_LENGTH, true);
	if (!length || length >= HARNESS_PATH_LENGTH) {
		return;
	}
	UINT unique = GetTempFileNameW(temporary, L"fms", 0, directory);
	ASSERT_BOOL_EQ(unique != 0, true);
	if (!unique) {
		return;
	}
	BOOL removed = DeleteFileW(directory);
	ASSERT_BOOL_EQ(removed, true);
	if (!removed) {
		return;
	}
	BOOL created = CreateDirectoryW(directory, NULL);
	ASSERT_BOOL_EQ(created, true);
	if (!created) {
		return;
	}
	wchar_t json[HARNESS_PATH_LENGTH];
	wchar_t binary[HARNESS_PATH_LENGTH];
	wchar_t jsonCopy[HARNESS_PATH_LENGTH];
	wchar_t binaryCopy[HARNESS_PATH_LENGTH];
	ASSERT_BOOL_EQ(paths_join(json, HARNESS_PATH_LENGTH, directory, L"input.json"), true);
	ASSERT_BOOL_EQ(paths_join(binary, HARNESS_PATH_LENGTH, directory, L"input.bin"), true);
	ASSERT_BOOL_EQ(paths_join(jsonCopy, HARNESS_PATH_LENGTH, directory, L"output.json"), true);
	ASSERT_BOOL_EQ(paths_join(binaryCopy, HARNESS_PATH_LENGTH, directory, L"output.bin"), true);
	ASSERT_BOOL_EQ(files_write(json, config, sizeof config - 1), true);
	char error[512] = "";
	const unsigned char encoded[] = {42};
	ASSERT_BOOL_EQ(fixtureLoadTest_save(json, binary, error, sizeof error), true);
	checkFile(binary, encoded, sizeof encoded);
	ASSERT_BOOL_EQ(inspectCalls == 0, true);

	ASSERT_BOOL_EQ(fixtureLoadTest_save(json, jsonCopy, error, sizeof error), true);
	checkFile(jsonCopy, config, sizeof config - 1);
	ASSERT_BOOL_EQ(fixtureLoadTest_save(binary, binaryCopy, error, sizeof error), true);
	checkFile(binaryCopy, encoded, sizeof encoded);
	ASSERT_BOOL_EQ(inspectCalls == 0, true);
	ASSERT_BOOL_EQ(fixtureLoadTest_save(binary, jsonCopy, error, sizeof error), true);
	checkFile(jsonCopy, config, sizeof config - 1);
	ASSERT_BOOL_EQ(inspectCalls == 1, true);

	// Saving over the source must finish reading it before replacing it.
	ASSERT_BOOL_EQ(fixtureLoadTest_save(json, json, error, sizeof error), true);
	checkFile(json, config, sizeof config - 1);
	ASSERT_BOOL_EQ(fixtureLoadTest_save(binary, binary, error, sizeof error), true);
	checkFile(binary, encoded, sizeof encoded);

	ASSERT_BOOL_EQ(files_write(binaryCopy, "existing", 8), true);
	ASSERT_BOOL_EQ(files_write(json, "{bad json", 9), true);
	ASSERT_BOOL_EQ(fixtureLoadTest_save(json, binaryCopy, error, sizeof error), false);
	ASSERT_BOOL_EQ(error[0] != 0, true);
	checkFile(binaryCopy, "existing", 8);

	ASSERT_BOOL_EQ(files_write(jsonCopy, "existing", 8), true);
	inspectFailure = true;
	ASSERT_BOOL_EQ(fixtureLoadTest_save(binary, jsonCopy, error, sizeof error), false);
	ASSERT_BOOL_EQ(strstr(error, "FM24") != NULL, true);
	checkFile(jsonCopy, "existing", 8);
	inspectFailure = false;

	ASSERT_BOOL_EQ(files_write(json, config, sizeof config - 1), true);
	HANDLE locked = CreateFileW(
		jsonCopy, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
	);
	ASSERT_BOOL_EQ(locked != INVALID_HANDLE_VALUE, true);
	ASSERT_BOOL_EQ(fixtureLoadTest_save(json, jsonCopy, error, sizeof error), false);
	ASSERT_BOOL_EQ(error[0] != 0, true);
	ASSERT_BOOL_EQ(CloseHandle(locked), true);
	checkFile(jsonCopy, "existing", 8);
	ASSERT_BOOL_EQ(fixtureLoadTest_save(json, L"invalid.txt", error, sizeof error), false);
	ASSERT_BOOL_EQ(fixtureLoadTest_save(L"missing.json", jsonCopy, error, sizeof error), false);
	checkFile(jsonCopy, "existing", 8);
	wchar_t missing[HARNESS_PATH_LENGTH];
	ASSERT_BOOL_EQ(
		paths_join(missing, HARNESS_PATH_LENGTH, directory, L"missing\\output.json"), true
	);
	ASSERT_BOOL_EQ(fixtureLoadTest_save(json, missing, error, sizeof error), false);
	ASSERT_BOOL_EQ(error[0] != 0, true);

	wchar_t pattern[HARNESS_PATH_LENGTH];
	ASSERT_BOOL_EQ(paths_join(pattern, HARNESS_PATH_LENGTH, directory, L"fms*.tmp"), true);
	WIN32_FIND_DATAW found;
	HANDLE scan = FindFirstFileW(pattern, &found);
	ASSERT_BOOL_EQ(scan == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_NOT_FOUND, true);
	if (scan != INVALID_HANDLE_VALUE) {
		FindClose(scan);
	}
	ASSERT_BOOL_EQ(DeleteFileW(json), true);
	ASSERT_BOOL_EQ(DeleteFileW(binary), true);
	ASSERT_BOOL_EQ(DeleteFileW(jsonCopy), true);
	ASSERT_BOOL_EQ(DeleteFileW(binaryCopy), true);
	ASSERT_BOOL_EQ(RemoveDirectoryW(directory), true);
}

void registerFixtureLoadTests(void) {
	test_run(
		"fixture saving converts formats and preserves destinations on failure",
		test_saves_complete_fixtures_without_partial_overwrites
	);
}
