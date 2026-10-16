// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Writes a traced serialisation as a JSON match config ("fm24-match-config/1"):
//
//   bytes     the serialised length (full only)
//   paths[]   distinct serialiser call chains, see trace.h (full only)
//   fields[]  one entry per write, in order:
//             {["site": "<rva>/<caller rva>", "p": <paths index>,] ["name": block label,]
//              ["member": game object member,] ["player": name,] ["flags": [...],] <value>
//              [, "role": "seed"]}
//
// "away_team" (written when `awayTeam` is not 0) is the unique ID of a team that replaced the
// fixture's away team (job_replace.c); `run` rebuilds the input against it.
//
// Only "format", "away_team" and each field's <value> are read back. The research keys (bytes,
// paths, site, p, member) are written only with `full`.
//
// <value> is one of u8/u16/u32/u64 (unsigned integers), str (a u32 byte length followed by
// that many UTF-8 bytes: the two writes merged into one editable string), hex (raw bytes) or u8map
// (a player's ratings: 15 positions 1-20, then 54 attributes 1-100 = in-game value x 5).
// A staff member's 52-byte attributes write is split into 52 named u8 fields.
#pragma once

#include "database.h"
#include "trace.h"

#include <stdbool.h>
#include <wchar.h>


bool configWriter_write(
	const wchar_t *path,
	const Serialised *serialised,
	const char *source,
	const PeopleIndex *people,
	uint32_t awayTeam,
	bool full
);
