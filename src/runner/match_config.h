// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Encodes an "fm24-match-config/1" JSON config (written by the DLL) back into the serialised
// GAME_MATCH bytes the game's deserialiser expects.
//
// Each entry of "fields" carries exactly one value, emitted in order:
//   u8 / u16 / u32 / u64  little-endian unsigned integer
//   str                   u32 byte length, then the UTF-8 bytes
//   hex                   raw bytes
//   u8map                 object of u8 values, emitted in key order (a player's ratings)
// Other keys ("site", "p", "role", "name", "player", ...) are annotations; "site" is kept in the
// write records.
#pragma once

#include "buffer.h"

#include <stdbool.h>
#include <stddef.h>


// One serialiser write as the config records it: its site ("<rva>/<caller rva>", or "*" for the
// byte part of a string, whose site the config does not record) and byte length.
typedef struct ConfigRecord {
	char site[32];
	size_t length;
} ConfigRecord;

typedef struct ConfigRecords {
	ConfigRecord *items;
	size_t count;
	size_t capacity;
} ConfigRecords;

// Fills `out` with the encoded bytes and, if `records` is non-NULL, the write records (they tile
// `out`). On failure returns false with a message in `error`.
bool matchConfig_encode(
	const char *json,
	size_t length,
	ByteBuffer *out,
	ConfigRecords *records,
	char *error,
	size_t errorSize
);

void matchConfig_freeRecords(ConfigRecords *records);
