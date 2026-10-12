// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files.h"

#include <stdio.h>
#include <wchar.h>


bool files_read(const wchar_t *path, ByteBuffer *out) {
	buffer_clear(out);
	FILE *file = _wfopen(path, L"rb");
	if (!file) {
		return false;
	}
	uint8_t chunk[1 << 16];
	size_t got = 0;
	bool ok = true;
	while (ok && (got = fread(chunk, 1, sizeof chunk, file)) > 0) {
		ok = buffer_append(out, chunk, got);
	}
	ok = ok && !ferror(file);
	return fclose(file) == 0 && ok;
}

bool files_write(const wchar_t *path, const void *bytes, size_t size) {
	FILE *file = _wfopen(path, L"wb");
	if (!file) {
		return false;
	}
	bool ok = fwrite(bytes, 1, size, file) == size;
	return fclose(file) == 0 && ok;
}

bool files_hasExtension(const wchar_t *path, const wchar_t *suffix) {
	size_t pathLength = wcslen(path);
	size_t suffixLength = wcslen(suffix);
	return pathLength >= suffixLength && _wcsicmp(path + pathLength - suffixLength, suffix) == 0;
}
