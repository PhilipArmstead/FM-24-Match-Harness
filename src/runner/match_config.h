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
// Other keys ("site", "p", "role", "name", "player", "member", "flags", ...) are annotations and
// are skipped.
#pragma once

#include "buffer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum ConfigValueKind {
	CONFIG_U8 = 1,
	CONFIG_U16 = 2,
	CONFIG_U32 = 4,
	CONFIG_U64 = 8,
	CONFIG_STRING,
	CONFIG_HEX,
	CONFIG_RATINGS,
} ConfigValueKind;

typedef struct ConfigField {
	const char *name;
	const char *player;
	ConfigValueKind kind;
	const uint8_t *bytes; // encoded value, including the length prefix for strings
	size_t length;
} ConfigField;

typedef struct ConfigVisitor {
	void *context;
	void (*field)(void *context, const ConfigField *field);
	void (*source)(void *context, const char *source);
} ConfigVisitor;

// Fills `out` with the encoded bytes. On failure returns false with a message in `error`.
bool matchConfig_encode(
	const char *json,
	size_t length,
	ByteBuffer *out,
	char *error,
	size_t errorSize
);

// Validates values with the same encoder, visiting each field once. Borrowed strings and bytes
// remain valid only during the callback. Annotation strings are read only for this operation.
bool matchConfig_visit(
	const char *json,
	size_t length,
	const ConfigVisitor *visitor,
	char *error,
	size_t errorSize
);

// A config's "away_team": the unique ID of the team that replaced the fixture's away team
// (`fm24ctl replace-away`), or 0 if there is none. False if the JSON cannot be read.
bool matchConfig_awayTeam(const char *json, size_t length, uint32_t *awayTeam);
