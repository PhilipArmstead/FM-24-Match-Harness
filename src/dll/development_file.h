// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// A player's development inputs as an editable "fm24-development/1" file (the field syntax of
// match_config.h): what `fm24ctl get-development` writes and `fm24ctl develop <file>` applies
// before simulating. One table (src/common/development_format.h, shared with the viewer) names
// every field and its limits, and development_file.c says where each lives, so export and apply
// always agree (knowledge-base/FM24 Training — Attribute Growth.md, section 14).
#pragma once

#include "game.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>


enum {
	DEVELOPMENT_FIELD_LIMIT = 64,
	DEVELOPMENT_SNAPSHOT_SIZE = 256,
	DEVELOPMENT_WEEK_LIMIT = 52,
	DEVELOPMENT_MONTH_LIMIT = 600, // develop's most months
	DEVELOPMENT_INJURY_LIMIT = 16,
};

// Values to apply, as `fm24ctl develop` sends them: "name:hex,name:hex" (each a field's encoded
// bytes, in the file's order).
typedef struct DevelopmentValues {
	size_t count;
	size_t field[DEVELOPMENT_FIELD_LIMIT];    // index into the table
	uint8_t bytes[DEVELOPMENT_SNAPSHOT_SIZE]; // the values, back to back
	size_t offset[DEVELOPMENT_FIELD_LIMIT];   // each value's start in `bytes`
	// "schedule.week1".."schedule.weekN": session IDs day by day, repeated for every week trained.
	size_t scheduleWeeks;
	uint16_t schedule[DEVELOPMENT_WEEK_LIMIT][SCHEDULE_SESSIONS];
	// "match_points.month1".."match_points.monthN": the match points each simulated month earns,
	// the last repeated for later months.
	size_t matchMonths;
	uint16_t matchPoints[DEVELOPMENT_MONTH_LIMIT];
	// "injury.window1.start"/".days" ...: planned injuries, from `start` days after the run's first
	// date, lasting `days`.
	size_t injuryWindows;
	uint16_t injuryStart[DEVELOPMENT_INJURY_LIMIT];
	uint16_t injuryDays[DEVELOPMENT_INJURY_LIMIT];
	uint8_t injuryParts[DEVELOPMENT_INJURY_LIMIT]; // bit 0 start given, bit 1 days given
	// "training_focus.month1".."training_focus.monthN": each simulated month's additional focus
	// (255 none), the last repeated for later months.
	size_t focusMonths;
	uint8_t focus[DEVELOPMENT_MONTH_LIMIT];
	// "matches.month1.count" / ".minutes" ...: each simulated month's matches and the minutes in
	// each, which move sharpness; the last month repeats.
	size_t playMonths;
	uint8_t playCount[DEVELOPMENT_MONTH_LIMIT];
	uint8_t playMinutes[DEVELOPMENT_MONTH_LIMIT];
	uint8_t playParts[DEVELOPMENT_MONTH_LIMIT]; // bit 0 count given, bit 1 minutes given
} DevelopmentValues;

// Every table field's current bytes, to put back later.
typedef struct DevelopmentSnapshot {
	bool present[DEVELOPMENT_FIELD_LIMIT]; // the field was there (e.g. a training happiness record)
	uint8_t bytes[DEVELOPMENT_SNAPSHOT_SIZE];
} DevelopmentSnapshot;

// Writes the player's fields (`name` annotates person_uid) and `schedule` as schedule.week1 unless
// it is NULL. Fails if the file cannot be written.
bool developmentFile_write(
	const wchar_t *path,
	uint8_t *player,
	uint32_t uniqueId,
	const char *name,
	const uint16_t *schedule
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
