// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Writes a serialised match to a file: raw bytes for .bin, a labelled JSON config for .json.
#pragma once

#include "trace.h"

#include <stdbool.h>
#include <wchar.h>


// True if `path` wants a JSON config (and therefore a traced serialisation).
bool output_wantsJson(const wchar_t *path);

// `awayTeam`: the unique ID of a team that replaced the fixture's away team, or 0; `full`: also
// write the research keys. Both apply to a JSON config only (see config_writer.h).
bool output_write(
	const wchar_t *path,
	const Serialised *serialised,
	const char *source,
	uint32_t awayTeam,
	bool full
);
