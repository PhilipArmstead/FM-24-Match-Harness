// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "embedded.h"

#include "files.h"
#include "paths.h"
#include "protocol.h"
#include "resources.h"

#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>


static const wchar_t directoryName[] = L"fm24-harness";

static bool sameContents(const wchar_t *path, const void *bytes, size_t size);

bool embedded_dllPath(wchar_t *out, size_t size) {
	wchar_t *localAppData = NULL;
	if (FAILED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &localAppData))) {
		CoTaskMemFree(localAppData);
		fprintf(stderr, "cannot find the local application data folder\n");
		return false;
	}
	wchar_t directory[HARNESS_PATH_LENGTH];
	bool ok = paths_join(directory, HARNESS_PATH_LENGTH, localAppData, directoryName);
	CoTaskMemFree(localAppData);
	if (!ok || !paths_join(out, size, directory, HARNESS_DLL_NAME)) {
		fprintf(stderr, "the harness DLL path is too long\n");
		return false;
	}
	if (!CreateDirectoryW(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
		fprintf(stderr, "cannot create %ls (%lu)\n", directory, GetLastError());
		return false;
	}
	return true;
}

bool embedded_extract(const wchar_t *path) {
	HRSRC resource = FindResourceW(NULL, MAKEINTRESOURCEW(IDR_HARNESS_DLL), RT_RCDATA);
	HGLOBAL loaded = resource ? LoadResource(NULL, resource) : NULL;
	const void *bytes = loaded ? LockResource(loaded) : NULL;
	DWORD size = resource ? SizeofResource(NULL, resource) : 0;
	if (!bytes || !size) {
		fprintf(stderr, "this fm24ctl.exe has no built-in harness DLL (%lu)\n", GetLastError());
		return false;
	}
	return embedded_write(path, bytes, size);
}

bool embedded_write(const wchar_t *path, const void *bytes, size_t size) {
	if (sameContents(path, bytes, size)) {
		return true;
	}
	wchar_t temporary[HARNESS_PATH_LENGTH];
	int written =
		swprintf(temporary, HARNESS_PATH_LENGTH, L"%ls.%lu.tmp", path, GetCurrentProcessId());
	if (written <= 0 || written >= HARNESS_PATH_LENGTH) {
		fprintf(stderr, "the harness DLL path is too long\n");
		return false;
	}
	if (!files_write(temporary, bytes, size)) {
		fprintf(stderr, "cannot write %ls\n", temporary);
		(void)DeleteFileW(temporary);
		return false;
	}
	if (MoveFileExW(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		return true;
	}
	DWORD error = GetLastError();
	(void)DeleteFileW(temporary);
	if (error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION) {
		// Windows does not let a loaded DLL be replaced, so fm.exe holds another build of it.
		fprintf(
			stderr,
			"cannot replace %ls: fm.exe has a different build of protocol %ls loaded; bump "
			"HARNESS_PROTOCOL or restart the game\n",
			path,
			HARNESS_PROTOCOL
		);
	} else {
		fprintf(stderr, "cannot replace %ls (%lu)\n", path, error);
	}
	return false;
}

// True if the file at `path` holds exactly `bytes`.
static bool sameContents(const wchar_t *path, const void *bytes, size_t size) {
	ByteBuffer existing = {0};
	bool same = files_read(path, &existing) && existing.length == size &&
							memcmp(existing.data, bytes, size) == 0;
	buffer_free(&existing);
	return same;
}
