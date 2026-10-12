// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "inject.h"

#include "paths.h"
#include "protocol.h"

#include <stdio.h>
#include <string.h>

// clang-format off: tlhelp32.h needs windows.h first, which alphabetical include sorting breaks
#include <windows.h>
#include <tlhelp32.h>
// clang-format on


enum { LOAD_TIMEOUT_MS = 30000 };

static DWORD findGame(void) {
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}
	PROCESSENTRY32W entry = {.dwSize = sizeof entry};
	DWORD pid = 0;
	for (BOOL more = Process32FirstW(snapshot, &entry); more && !pid;
		more = Process32NextW(snapshot, &entry)) {
		if (_wcsicmp(entry.szExeFile, L"fm.exe") == 0) {
			pid = entry.th32ProcessID;
		}
	}
	CloseHandle(snapshot);
	return pid;
}

// Runs LoadLibraryW(dllPath) in the game; returns false (and prints why) on failure.
static bool loadRemotely(HANDLE process, const wchar_t *dllPath) {
	size_t bytes = (wcslen(dllPath) + 1) * sizeof(wchar_t);
	void *remote = VirtualAllocEx(process, NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!remote || !WriteProcessMemory(process, remote, dllPath, bytes, NULL)) {
		fprintf(stderr, "cannot write the DLL path into fm.exe (%lu)\n", GetLastError());
		return false;
	}
	FARPROC loadLibrary = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
	LPTHREAD_START_ROUTINE start = NULL;
	memcpy(&start, &loadLibrary, sizeof start); // FARPROC -> thread routine, without a cast warning
	HANDLE thread = CreateRemoteThread(process, NULL, 0, start, remote, 0, NULL);
	if (!thread) {
		fprintf(stderr, "CreateRemoteThread failed (%lu)\n", GetLastError());
		return false;
	}
	if (WaitForSingleObject(thread, LOAD_TIMEOUT_MS) == WAIT_TIMEOUT) {
		// A debugger probably suspended fm.exe on the DLL-load event. Keep the path allocated so
		// LoadLibraryW can still read it once the game resumes.
		fprintf(
			stderr,
			"LoadLibrary still pending after 30 s (debugger holding fm.exe?); resume the game, then "
			"check harness.log\n"
		);
		CloseHandle(thread);
		return false;
	}
	DWORD loaded = 0;
	GetExitCodeThread(thread, &loaded);
	CloseHandle(thread);
	VirtualFreeEx(process, remote, 0, MEM_RELEASE);
	if (!loaded) {
		fprintf(stderr, "LoadLibrary failed inside fm.exe\n");
	}
	return loaded != 0;
}

bool inject_harness(void) {
	DWORD pid = findGame();
	if (!pid) {
		fprintf(stderr, "fm.exe is not running\n");
		return false;
	}
	wchar_t directory[HARNESS_PATH_LENGTH];
	wchar_t dllPath[HARNESS_PATH_LENGTH];
	if (!paths_executableDirectory(directory, HARNESS_PATH_LENGTH) ||
			!paths_join(dllPath, HARNESS_PATH_LENGTH, directory, HARNESS_DLL_NAME) ||
			GetFileAttributesW(dllPath) == INVALID_FILE_ATTRIBUTES) {
		fprintf(stderr, "%ls not found next to fm24ctl.exe (build it first)\n", HARNESS_DLL_NAME);
		return false;
	}
	HANDLE process = OpenProcess(
		PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION,
		FALSE,
		pid
	);
	if (!process) {
		fprintf(stderr, "OpenProcess failed (%lu)\n", GetLastError());
		return false;
	}
	bool ok = loadRemotely(process, dllPath);
	CloseHandle(process);
	if (ok) {
		printf("injected into fm.exe (pid %lu); see harness.log for the build check\n", pid);
	}
	return ok;
}
