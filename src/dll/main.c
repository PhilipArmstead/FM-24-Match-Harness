// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// fm24_harness: runs Football Manager 24's own match engine headless, inside fm.exe.
//
// Injected by the runner (fm24ctl). On load it checks the game build, then a worker thread serves
// jobs on the harness pipe, one runner at a time (see protocol.h and job.h). Jobs must only be
// submitted while the game is idle: the engine calls are not otherwise synchronised with the game's
// own threads.

#include "game.h"
#include "job.h"
#include "log.h"
#include "protocol.h"

#include <string.h>
#include <wchar.h>
#include <windows.h>


static wchar_t directory[HARNESS_PATH_LENGTH]; // where the DLL and its log live

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved);

static void runJob(char *text);
static void serve(HANDLE pipe);
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

static void runJob(char *text) {
	Job job;
	if (!job_parse(text, &job)) {
		job_writeResult("error: cannot read the job; see harness.log");
		return;
	}
	switch (job.command) {
		case JOB_NONE: break; // job_parse rejects it
		case JOB_RUN: jobs_run(&job); break;
		case JOB_BUILD_NEXT: jobs_buildNext(&job); break;
		case JOB_INSPECT: jobs_inspect(&job); break;
		case JOB_REPLACE_AWAY:
		case JOB_REPLACE_HOME: jobs_replaceTeam(&job); break;
		case JOB_LIST_TEAMS: jobs_listTeams(&job); break;
		case JOB_LIST_PLAYERS: jobs_listPlayers(&job); break;
		case JOB_GET_PLAYER: jobs_getPlayer(&job); break;
		case JOB_DEVELOP: jobs_develop(&job); break;
		case JOB_SEED_DEVELOPMENT: jobs_seedDevelopment(&job); break;
		case JOB_GET_DEVELOPMENT: jobs_getDevelopment(&job); break;
		case JOB_WATCH_WRITE: jobs_watchWrite(&job); break;
	}
	job_free(&job);
}

// Reads one job message from a connected runner, runs it and replies with its result line.
static void serve(HANDLE pipe) {
	static char text[HARNESS_JOB_LENGTH + 1];
	DWORD length = 0;
	if (!ReadFile(pipe, text, HARNESS_JOB_LENGTH, &length, NULL)) {
		DWORD error = GetLastError();
		log_message("cannot read the job (Windows error %lu)", error);
		if (error != ERROR_MORE_DATA) {
			return;
		}
		job_writeResult("error: the job is too long");
	} else if (memchr(text, 0, length)) {
		log_message("the job contains a NUL byte");
		job_writeResult("error: cannot read the job; see harness.log");
	} else {
		text[length] = 0;
		runJob(text);
	}
	const char *result = job_takeResult();
	if (!result[0]) {
		result = "error: the job reported no result; see harness.log";
	}
	DWORD written = 0;
	DWORD size = (DWORD)strlen(result);
	if (!WriteFile(pipe, result, size, &written, NULL) || written != size) {
		log_message("cannot send the job result (Windows error %lu): %s", GetLastError(), result);
	}
}

static DWORD WINAPI worker(LPVOID parameter) {
	setDirectory(parameter);
	log_open(directory);
	if (!game_init()) {
		return 1;
	}
	// One instance: runners queue for it. FILE_FLAG_FIRST_PIPE_INSTANCE keeps a second copy of this
	// version (loaded from another folder) from serving the same pipe.
	HANDLE pipe = CreateNamedPipeW(
		HARNESS_PIPE_NAME,
		PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
		1,
		HARNESS_RESULT_LENGTH,
		HARNESS_JOB_LENGTH,
		0,
		NULL
	);
	if (pipe == INVALID_HANDLE_VALUE) {
		log_message(
			"cannot create the job pipe (Windows error %lu); is this version already loaded from "
			"another folder?",
			GetLastError()
		);
		return 1;
	}
	log_message("ready; waiting for jobs");
	while (ConnectNamedPipe(pipe, NULL) || GetLastError() == ERROR_PIPE_CONNECTED) {
		serve(pipe);
		(void)FlushFileBuffers(pipe); // until the runner has read the result (or has gone)
		if (!DisconnectNamedPipe(pipe)) {
			break;
		}
	}
	log_message("job loop stopped (Windows error %lu)", GetLastError());
	CloseHandle(pipe);
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
