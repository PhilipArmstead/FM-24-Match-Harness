// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "client.h"

#include "files.h"
#include "paths.h"
#include "protocol.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>


static void readResult(const wchar_t *path, char *result, size_t size) {
	(void)snprintf(result, size, "no result; see harness.log");
	FILE *file = _wfopen(path, L"rb");
	if (!file) {
		return;
	}
	if (!fgets(result, (int)size, file)) {
		result[0] = 0;
	}
	(void)fclose(file);
}

bool client_submit(const char *job, unsigned timeoutMs, char *result, size_t resultSize) {
	HANDLE go = OpenEventW(EVENT_MODIFY_STATE, FALSE, HARNESS_EVENT_GO);
	HANDLE done = OpenEventW(SYNCHRONIZE, FALSE, HARNESS_EVENT_DONE);
	wchar_t directory[HARNESS_PATH_LENGTH];
	wchar_t jobPath[HARNESS_PATH_LENGTH];
	wchar_t resultPath[HARNESS_PATH_LENGTH];
	bool ok = go && done;
	if (!ok) {
		fprintf(stderr, "harness not running in fm.exe (inject first; check harness.log)\n");
	}
	ok = ok && paths_executableDirectory(directory, HARNESS_PATH_LENGTH) &&
			 paths_join(jobPath, HARNESS_PATH_LENGTH, directory, HARNESS_JOB_FILE) &&
			 paths_join(resultPath, HARNESS_PATH_LENGTH, directory, HARNESS_RESULT_FILE);
	if (ok) {
		DeleteFileW(resultPath);
		ok = files_write(jobPath, job, strlen(job));
		if (!ok) {
			fprintf(stderr, "cannot write the job file\n");
		}
	}
	if (ok) {
		SetEvent(go);
		ok = WaitForSingleObject(done, timeoutMs) == WAIT_OBJECT_0;
		if (!ok) {
			fprintf(stderr, "timed out\n");
		}
	}
	if (ok && result) {
		readResult(resultPath, result, resultSize);
	}
	if (go) {
		CloseHandle(go);
	}
	if (done) {
		CloseHandle(done);
	}
	return ok;
}

bool client_succeeded(const char *result) {
	return strncmp(result, "ok", 2) == 0;
}
