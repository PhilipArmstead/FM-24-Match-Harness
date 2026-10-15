// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "output.h"

#include "config_writer.h"
#include "database.h"
#include "files.h"


bool output_wantsJson(const wchar_t *path) {
	return files_hasExtension(path, L".json");
}

bool output_write(
	const wchar_t *path,
	const Serialised *serialised,
	const char *source,
	uint32_t awayTeam,
	bool full
) {
	if (!output_wantsJson(path)) {
		return files_write(path, serialised->bytes.data, serialised->bytes.length);
	}
	PeopleIndex people;
	(void)database_loadPeople(&people); // without it the config just lacks player names
	bool ok = configWriter_write(path, serialised, source, &people, awayTeam, full);
	database_freePeople(&people);
	return ok;
}
