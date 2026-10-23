// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// A player's development inputs as an editable "fm24-development/1" file (the field syntax of
// match_config.h): what `fm24ctl get-development` writes and `fm24ctl develop <file>` applies
// before simulating. One table names every field, where it lives and its limits, so export and
// apply always agree (knowledge-base/FM24 Training — Attribute Growth.md, section 14).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>


enum { DEVELOPMENT_FIELD_LIMIT = 64, DEVELOPMENT_SNAPSHOT_SIZE = 256 };

// Values to apply, as `fm24ctl develop` sends them: "name:hex,name:hex" (each a field's encoded
// bytes, in the file's order).
typedef struct DevelopmentValues {
	size_t count;
	size_t field[DEVELOPMENT_FIELD_LIMIT];    // index into the table
	uint8_t bytes[DEVELOPMENT_SNAPSHOT_SIZE]; // the values, back to back
	size_t offset[DEVELOPMENT_FIELD_LIMIT];   // each value's start in `bytes`
} DevelopmentValues;

// Every table field's current bytes, to put back later.
typedef struct DevelopmentSnapshot {
	bool hasHappiness; // the player had a training happiness record
	uint8_t bytes[DEVELOPMENT_SNAPSHOT_SIZE];
} DevelopmentSnapshot;

// Writes the player's fields (`name` annotates person_uid). Fails if the file cannot be written.
bool developmentFile_write(
	const wchar_t *path,
	uint8_t *player,
	uint32_t uniqueId,
	const char *name
);

// Parses and checks `text` (names, sizes and limits). False with a reason in `error`.
bool developmentFile_parse(const char *text, DevelopmentValues *out, char *error, size_t errorSize);

// Whether `values` can be applied to this player (a training happiness value needs its record).
bool developmentFile_fits(
	uint8_t *player,
	const DevelopmentValues *values,
	char *error,
	size_t errorSize
);

void developmentFile_save(uint8_t *player, DevelopmentSnapshot *out);
void developmentFile_restore(uint8_t *player, const DevelopmentSnapshot *snapshot);
void developmentFile_apply(uint8_t *player, const DevelopmentValues *values);
