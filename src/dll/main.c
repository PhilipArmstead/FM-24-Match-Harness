// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// fm24_harness: runs Football Manager 24's own match engine headless, inside fm.exe.
//
// Injected by the runner (fm24ctl). On load it checks the game build, then a worker thread waits
// for jobs (see protocol.h and job.h). Jobs must only be submitted while the game is idle: the
// engine calls are not otherwise synchronised with the game's own threads.

#include "game.h"
#include "job.h"
#include "log.h"
#include "protocol.h"

#include <wchar.h>
#include <windows.h>


static wchar_t directory[HARNESS_PATH_LENGTH]; // where the DLL, job file, result and log live

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved);

static void runJob(void);
static DWORD WINAPI worker(LPVOID parameter);
static void setDirectory(HINSTANCE module);

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
	(void)reserved;
	if (reason != DLL_PROCESS_ATTACH) {
		return TRUE;
	}
	DisableThreadLibraryCalls(module);
	HANDLE thread = CreateThread(NULL, 0, worker, module, 0, NULL);
	if (!thread) {
		OutputDebugStringW(L"fm24 harness: cannot start the job thread");
		return FALSE;
	}
	CloseHandle(thread);
	return TRUE;
}

static void runJob(void) {
	Job job;
	if (!job_read(directory, &job)) {
		job_writeResult(directory, "error: cannot read the job; see harness.log");
		return;
	}
	switch (job.command) {
		case JOB_NONE: break; // job_read rejects it
		case JOB_RUN: jobs_run(&job, directory); break;
		case JOB_BUILD_NEXT: jobs_buildNext(&job, directory); break;
		case JOB_INSPECT: jobs_inspect(&job, directory); break;
		case JOB_REPLACE_AWAY: jobs_replaceAway(&job, directory); break;
	}
	job_free(&job);
}

static DWORD WINAPI worker(LPVOID parameter) {
	setDirectory(parameter);
	log_open(directory);
	if (!game_init()) {
		return 1;
	}
	HANDLE go = CreateEventW(NULL, FALSE, FALSE, HARNESS_EVENT_GO);
	HANDLE done = CreateEventW(NULL, FALSE, FALSE, HARNESS_EVENT_DONE);
	if (!go || !done) {
		log_message("cannot create events");
		if (go) {
			CloseHandle(go);
		}
		if (done) {
			CloseHandle(done);
		}
		return 1;
	}
	log_message("ready; waiting for jobs");
	while (WaitForSingleObject(go, INFINITE) == WAIT_OBJECT_0) {
		runJob();
		if (!SetEvent(done)) {
			log_message("cannot signal job completion (Windows error %lu)", GetLastError());
			break;
		}
	}
	log_message("job event loop stopped (Windows error %lu)", GetLastError());
	CloseHandle(done);
	CloseHandle(go);
	return 1;
}

static void setDirectory(HINSTANCE module) {
	DWORD length = GetModuleFileNameW(module, directory, HARNESS_PATH_LENGTH);
	if (length == 0 || length >= HARNESS_PATH_LENGTH) {
		directory[0] = 0;
		return;
	}
	wchar_t *slash = wcsrchr(directory, L'\\');
	if (slash) {
		*slash = 0;
	}
}
