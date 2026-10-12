// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "paths.h"

#include <stdio.h>
#include <windows.h>


bool paths_executableDirectory(wchar_t *out, size_t size) {
	DWORD length = GetModuleFileNameW(NULL, out, (DWORD)size);
	if (length == 0 || length >= size) {
		return false;
	}
	wchar_t *slash = wcsrchr(out, L'\\');
	if (slash) {
		*slash = 0;
	}
	return true;
}

bool paths_join(wchar_t *out, size_t size, const wchar_t *directory, const wchar_t *name) {
	int written = swprintf(out, size, L"%ls\\%ls", directory, name);
	return written > 0 && (size_t)written < size;
}

bool paths_absolute(const wchar_t *path, wchar_t *out, size_t size) {
	DWORD length = GetFullPathNameW(path, (DWORD)size, out, NULL);
	return length > 0 && length < size;
}

bool paths_toUtf8(const wchar_t *text, char *out, size_t size) {
	return WideCharToMultiByte(CP_UTF8, 0, text, -1, out, (int)size, NULL, NULL) > 0;
}
