// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture_export.h"

#include <string.h>


static void teamFilename(const char *name, wchar_t *out) {
	for (; *name; ++name) {
		unsigned char c = (unsigned char)*name;
		if (c < ' ' || c > '~' || strchr("<>:\"/\\|?*", c)) {
			continue;
		}
		*out++ = (wchar_t)(c == ' ' ? '-' : c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
	}
	*out = 0;
}

bool fixtureExport_filename(
	const FixtureData *data,
	uint64_t timestamp,
	wchar_t *out,
	size_t size
) {
	wchar_t home[FIXTURE_TEXT];
	wchar_t away[FIXTURE_TEXT];
	teamFilename(data->teams[0].name, home);
	teamFilename(data->teams[1].name, away);
	int length =
		swprintf(out, size, L"%ls-v-%ls-%llu.json", home, away, (unsigned long long)timestamp);
	return length > 0 && (size_t)length < size && length < FIXTURE_EXPORT_FILENAME_LENGTH;
}
