// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "development_file.h"

#include "config_writer.h"
#include "development_format.h"
#include "game.h"
#include "labels.h"
#include "memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef enum Base {
	ON_PLAYER,          // offset from the player record
	ON_PERSON,          // offset from the person inside it (PLAYER_PERSON)
	ON_HAPPINESS,       // offset in the training happiness relationship record
	ON_TEAM_TRAINING,   // offset in the training team's TEAM_TRAINING_INFO
	ON_CLUB_FACILITIES, // offset in the training team's club's facilities
	ON_FOCUS,           // offset in the player's INDIVIDUAL_TRAINING_INFO, created when applied
} Base;

// The table's names, kinds, limits and notes are shared with the viewer (development_format.h);
// `locations` adds where each field lives.
typedef DevelopmentField Field;

typedef struct Location {
	Base base;
	size_t offset;
} Location;

static const Field *const fields = developmentFields;

enum { FIELD_COUNT = DEVELOPMENT_FIELD_COUNT };

// Everything the development routine is known to read that is plain data (no pointers).
static const Location locations[FIELD_COUNT] = {
	[DEVELOPMENT_CURRENT_ABILITY] = {ON_PLAYER, PLAYER_CURRENT_ABILITY},
	[DEVELOPMENT_POTENTIAL_ABILITY] = {ON_PLAYER, PLAYER_POTENTIAL_ABILITY},
	[DEVELOPMENT_RATINGS_FIELD] = {ON_PLAYER, PLAYER_RATINGS},
	[DEVELOPMENT_PROGRESS_STRENGTH] = {ON_PLAYER, PLAYER_TRAINING_PROGRESS + 0},
	[DEVELOPMENT_PROGRESS_QUICKNESS] = {ON_PLAYER, PLAYER_TRAINING_PROGRESS + 1},
	[DEVELOPMENT_PROGRESS_GK_SHOT_STOPPING] = {ON_PLAYER, PLAYER_TRAINING_PROGRESS + 2},
	[DEVELOPMENT_PROGRESS_GK_HANDLING] = {ON_PLAYER, PLAYER_TRAINING_PROGRESS + 3},
	[DEVELOPMENT_PROGRESS_DEFENDING] = {ON_PLAYER, PLAYER_TRAINING_PROGRESS + 4},
	[DEVELOPMENT_PROGRESS_TACKLING] = {ON_PLAYER, PLAYER_TRAINING_PROGRESS + 5},
	[DEVELOPMENT_PROGRESS_TACTICS] = {ON_PLAYER, PLAYER_TRAINING_PROGRESS + 6},
	[DEVELOPMENT_PROGRESS_BALL_CONTROL] = {ON_PLAYER, PLAYER_TRAINING_PROGRESS + 7},
	[DEVELOPMENT_PROGRESS_ATTACKING] = {ON_PLAYER, PLAYER_TRAINING_PROGRESS + 8},
	[DEVELOPMENT_PROGRESS_SHOOTING] = {ON_PLAYER, PLAYER_TRAINING_PROGRESS + 9},
	[DEVELOPMENT_PROGRESS_SET_PIECES] = {ON_PLAYER, PLAYER_TRAINING_PROGRESS + 10},
	[DEVELOPMENT_TRAINING_INTENSITY] = {ON_PLAYER, PLAYER_TRAINING_INTENSITY},
	[DEVELOPMENT_TRAINING_FOCUS] = {ON_FOCUS, FOCUS_ID},
	[DEVELOPMENT_CONDITION] = {ON_PLAYER, PLAYER_CONDITION},
	[DEVELOPMENT_FATIGUE] = {ON_PLAYER, PLAYER_FATIGUE},
	[DEVELOPMENT_SHARPNESS] = {ON_PLAYER, PLAYER_SHARPNESS},
	[DEVELOPMENT_MORALE] = {ON_PLAYER, PLAYER_MORALE},
	[DEVELOPMENT_HEIGHT] = {ON_PLAYER, PLAYER_HEIGHT},
	[DEVELOPMENT_WEIGHT] = {ON_PLAYER, PLAYER_WEIGHT},
	[DEVELOPMENT_ADAPTABILITY] = {ON_PERSON, PERSON_PERSONALITY + 0},
	[DEVELOPMENT_AMBITION] = {ON_PERSON, PERSON_PERSONALITY + 1},
	[DEVELOPMENT_LOYALTY] = {ON_PERSON, PERSON_PERSONALITY + 2},
	[DEVELOPMENT_PRESSURE] = {ON_PERSON, PERSON_PERSONALITY + 3},
	[DEVELOPMENT_PROFESSIONALISM] = {ON_PERSON, PERSON_PERSONALITY + 4},
	[DEVELOPMENT_SPORTSMANSHIP] = {ON_PERSON, PERSON_PERSONALITY + 5},
	[DEVELOPMENT_TEMPERAMENT] = {ON_PERSON, PERSON_PERSONALITY + 6},
	[DEVELOPMENT_CONTROVERSY] = {ON_PERSON, PERSON_PERSONALITY + 7},
	[DEVELOPMENT_BIRTH_DAY] = {ON_PERSON, PERSON_DATE_OF_BIRTH},
	[DEVELOPMENT_BIRTH_YEAR] = {ON_PERSON, PERSON_DATE_OF_BIRTH + 2},
	[DEVELOPMENT_TRAINING_HAPPINESS] = {ON_HAPPINESS, 0x0C},
	[DEVELOPMENT_HAPPINESS_CHANGE] = {ON_HAPPINESS, 0x08},
	[DEVELOPMENT_LAST_GAINS] = {ON_PLAYER, PLAYER_DEVELOPMENT_GAINS},
	[DEVELOPMENT_LAST_OTHER_GAINS] = {ON_PLAYER, PLAYER_DEVELOPMENT_GAINS + 2},
	[DEVELOPMENT_WEAK_FOOT_PROGRESS] = {ON_PLAYER, PLAYER_WEAK_FOOT_PROGRESS},
	[DEVELOPMENT_PERIOD_TOTAL] = {ON_PLAYER, PLAYER_DEVELOPMENT_PERIOD},
	[DEVELOPMENT_PERIOD_LAST_TOTAL] = {ON_PLAYER, PLAYER_DEVELOPMENT_PERIOD + 2},
	[DEVELOPMENT_PERIOD_LAST_COUNT] = {ON_PLAYER, PLAYER_DEVELOPMENT_PERIOD + 12},
	[DEVELOPMENT_PERIOD_COUNT] = {ON_PLAYER, PLAYER_DEVELOPMENT_PERIOD + 13},
	[DEVELOPMENT_IMPORTANT_MATCHES_PROGRESS] = {ON_PLAYER, PLAYER_RATING_PROGRESS},
	[DEVELOPMENT_DETERMINATION_PROGRESS] = {ON_PLAYER, PLAYER_RATING_PROGRESS + 2},
	[DEVELOPMENT_PRESSURE_PROGRESS] = {ON_PLAYER, PLAYER_PERSONALITY_PROGRESS},
	[DEVELOPMENT_PROFESSIONALISM_PROGRESS] = {ON_PLAYER, PLAYER_PERSONALITY_PROGRESS + 2},
	[DEVELOPMENT_PLAYED_LEVEL] = {ON_PLAYER, PLAYER_PLAYED_LEVEL},
	[DEVELOPMENT_PLAYED_APPEARANCES] = {ON_PLAYER, PLAYER_PLAYED_LEVEL + 1},
	[DEVELOPMENT_FLAGS] = {ON_PLAYER, PLAYER_DEVELOPMENT_FLAGS},
	[DEVELOPMENT_TRAINING_RATING] = {ON_PLAYER, PLAYER_TRAINING_RATING},
	[DEVELOPMENT_ABILITY_CARRY] = {ON_PLAYER, PLAYER_ABILITY_CARRY},
	[DEVELOPMENT_COACHING] = {ON_TEAM_TRAINING, 0},
	[DEVELOPMENT_COACH_WORKLOAD] = {ON_TEAM_TRAINING, TRAINING_CATEGORY_COUNT},
	[DEVELOPMENT_TRAINING_FACILITIES] = {ON_CLUB_FACILITIES, FACILITIES_TRAINING},
};

_Static_assert(
	(int)FIELD_COUNT <= (int)DEVELOPMENT_FIELD_LIMIT,
	"DevelopmentSnapshot.present is too small"
);
_Static_assert(
	(int)PLAYER_RATING_COUNT == (int)DEVELOPMENT_RATING_COUNT &&
		(int)TRAINING_CATEGORY_COUNT == (int)DEVELOPMENT_CATEGORY_COUNT &&
		(int)SCHEDULE_SESSIONS == (int)DEVELOPMENT_SCHEDULE_SESSIONS,
	"the game's counts and the file's differ"
);

static Base baseOf(const Field *field) {
	return locations[field - fields].base;
}

static const char SCHEDULE_PREFIX[] = "schedule.week";
static const char MATCH_PREFIX[] = "match_points.month";
static const char INJURY_PREFIX[] = "injury.window";
static const char FOCUS_PREFIX[] = "training_focus.month";
static const char PLAY_PREFIX[] = "matches.month";
// A month's matches, and the minutes in each (120 with extra time).
enum { PLAY_COUNT_LIMIT = 31, PLAY_MINUTES_LIMIT = 120 };
enum { FOCUS_LAST = 26, FOCUS_NONE = 255 };

// The player's training happiness record, or NULL if it has none.
static uint8_t *happinessRecord(uint8_t *player) {
	uint8_t *box = NULL;
	uint8_t *begin = NULL;
	uint8_t *end = NULL;
	if (!memory_readPointer(player + PLAYER_PERSON, PERSON_RELATIONSHIPS, &box) || !box ||
			!memory_readPointer(box, 0, &begin) || !memory_readPointer(box, 8, &end) || end < begin ||
			end - begin > 0x10000) {
		return NULL;
	}
	for (uint8_t *record = begin; record + RELATIONSHIP_SIZE <= end; record += RELATIONSHIP_SIZE) {
		uint16_t type = 0;
		if (memory_readU16(record, RELATIONSHIP_TYPE, &type) &&
				type == RELATIONSHIP_TRAINING_HAPPINESS) {
			return record;
		}
	}
	return NULL;
}

// The team whose training the player does (the weekly training's own lookup), or NULL.
static uint8_t *trainingTeam(uint8_t *player) {
	uint8_t *manager = NULL;
	return memory_readPointer(game_address(RVA_TRAINING_MANAGER), 0, &manager) && manager
					 ? ((TrainingTeamFn)game_function(RVA_TRAINING_TEAM))(manager, player)
					 : NULL;
}

static uint8_t *locate(uint8_t *player, const Field *field) {
	if (!player) {
		return NULL;
	}
	size_t offset = locations[field - fields].offset;
	switch (baseOf(field)) {
		case ON_PLAYER: return player + offset;
		case ON_PERSON: return player + PLAYER_PERSON + offset;
		case ON_HAPPINESS: {
			uint8_t *record = happinessRecord(player);
			return record ? record + offset : NULL;
		}
		case ON_TEAM_TRAINING: {
			uint8_t *team = trainingTeam(player);
			uint8_t *data = NULL;
			uint8_t *info = NULL;
			return team && memory_readPointer(team, TEAM_IN_GAME_DATA, &data) && data &&
								 memory_readPointer(data, TEAM_TRAINING_INFO, &info) && info
							 ? info + offset
							 : NULL;
		}
		case ON_CLUB_FACILITIES: {
			// shortcut: the club's own level; sub_142215070 can use an affiliate's instead, and the
			// monthly routine asks the player's own club (vfunc +672), which differs on loan.
			uint8_t *team = trainingTeam(player);
			uint8_t *club = NULL;
			uint8_t *facilities = NULL;
			return team && memory_readPointer(team, TEAM_CLUB, &club) && club &&
								 memory_readPointer(club, CLUB_FACILITIES, &facilities) && facilities
							 ? facilities + offset
							 : NULL;
		}
		case ON_FOCUS: {
			uint8_t *record = NULL;
			return memory_readPointer(player, PLAYER_FOCUS_RECORD, &record) && record ? record + offset
																																								: NULL;
		}
	}
	return NULL;
}

static bool writeField(FILE *file, uint8_t *player, const Field *field, const char *heading);
static void writeAttributeChanges(FILE *file, uint8_t *player);

bool developmentFile_write(
	const wchar_t *path,
	uint8_t *player,
	uint32_t uniqueId,
	const char *name,
	const uint16_t *schedule
) {
	FILE *file = _wfopen(path, L"wb");
	if (!file) {
		return false;
	}
	fprintf(file, "{\n%s", developmentFormat_header);
	uint16_t day = 0;
	uint16_t year = 0;
	const uint8_t *date = game_address(RVA_CURRENT_DATE);
	if (memory_readU16(date, 0, &day) && memory_readU16(date, 2, &year)) {
		fprintf(file, "  // Exported on day %u of %u (game date).\n", day & 0x1FF, year);
	}
	writeAttributeChanges(file, player);
	fprintf(file, "  \"format\": \"" DEVELOPMENT_FORMAT "\",\n  \"source\": ");
	configWriter_writeString(file, name);
	// The scheduler develops a person on day (row ID % 28) + 1 of each month (sub_144F6FC80).
	uint32_t row = 0;
	bool hasRow = memory_readU32(player + PLAYER_PERSON, PERSON_ROW_ID, &row);
	fprintf(file, ",\n  \"fields\": [\n    // the player (don't change)");
	if (hasRow) {
		fprintf(file, "; develops on day %u of each month", row % 28 + 1);
	}
	fprintf(file, "\n    {\"name\": \"person_uid\", \"player\": ");
	configWriter_writeString(file, name);
	fprintf(file, ", \"u32\": %u}", uniqueId);
	for (size_t i = 0; i < FIELD_COUNT; ++i) {
		if (!fields[i].internal) {
			writeField(file, player, &fields[i], NULL);
		}
	}
	if (schedule) {
		developmentFormat_writeSchedule(file, schedule);
	}
	const char *heading = developmentFormat_dragons;
	for (size_t i = 0; i < FIELD_COUNT; ++i) {
		if (fields[i].internal && writeField(file, player, &fields[i], heading)) {
			heading = NULL;
		}
	}
	fprintf(file, "\n  ]\n}\n");
	bool ok = !ferror(file);
	return fclose(file) == 0 && ok;
}

// The attribute changes the game keeps for the player (e.g. losses from an injury), as a comment:
// not in the file's values, and not yet understood (knowledge-base section 15).
static void writeAttributeChanges(FILE *file, uint8_t *player) {
	uint8_t *list = NULL;
	uint8_t *begin = NULL;
	uint8_t *end = NULL;
	if (!memory_readPointer(player, PLAYER_ATTRIBUTE_CHANGES, &list) || !list ||
			!memory_readPointer(list, 0, &begin) || !memory_readPointer(list, 8, &end) || end <= begin ||
			end - begin > 512) {
		return;
	}
	fprintf(file, "  // Attribute changes the game keeps (not applied by develop):");
	for (uint8_t *entry = begin; entry + 2 <= end; entry += 2) {
		uint16_t value = 0;
		if (memory_readU16(entry, 0, &value)) {
			fprintf(file, " %s %+d", labels_ratingName(value & 0xFF), (int8_t)(value >> 8));
		}
	}
	fprintf(file, "\n");
}

// One field, as developmentFormat_writeField writes it. False if the player has no such field (no
// training happiness record, or no team coaching record).
static bool writeField(FILE *file, uint8_t *player, const Field *field, const char *heading) {
	const uint8_t *at = locate(player, field);
	if (!at) {
		return false;
	}
	developmentFormat_writeField(file, field, at, heading);
	return true;
}

static const Field *findField(const char *name, size_t length) {
	for (size_t i = 0; i < FIELD_COUNT; ++i) {
		if (strlen(fields[i].name) == length && memcmp(fields[i].name, name, length) == 0) {
			return &fields[i];
		}
	}
	return NULL;
}

// One week's "<N>:hex" (after "schedule.week"): SCHEDULE_SESSIONS u16, weeks numbered from 1 in
// order. Any session ID is accepted: the game skips IDs it has no session for.
static bool parseWeek(
	const char *number,
	const char *colon,
	const char *end,
	DevelopmentValues *out,
	char *error,
	size_t errorSize
) {
	char *stop = NULL;
	unsigned long week = strtoul(number, &stop, 10);
	if (stop != colon || week != out->scheduleWeeks + 1 || week > DEVELOPMENT_WEEK_LIMIT) {
		(void)snprintf(
			error,
			errorSize,
			"schedule weeks must be %s1 to %s%d, in order",
			SCHEDULE_PREFIX,
			SCHEDULE_PREFIX,
			DEVELOPMENT_WEEK_LIMIT
		);
		return false;
	}
	const char *hex = colon + 1;
	if ((size_t)(end - hex) != (size_t)4 * SCHEDULE_SESSIONS) {
		(void)snprintf(
			error, errorSize, "%s%lu should be %d session IDs", SCHEDULE_PREFIX, week, SCHEDULE_SESSIONS
		);
		return false;
	}
	for (size_t i = 0; i < SCHEDULE_SESSIONS; ++i) {
		char digits[5] = {hex[4 * i + 2], hex[4 * i + 3], hex[4 * i], hex[4 * i + 1], 0}; // LE
		out->schedule[week - 1][i] = (uint16_t)strtoul(digits, &stop, 16);
		if (*stop) {
			(void)snprintf(error, errorSize, "%s%lu has a bad hex digit", SCHEDULE_PREFIX, week);
			return false;
		}
	}
	out->scheduleWeeks = week;
	return true;
}

// A u16 as the runner encodes it: 4 hex digits, little-endian.
static bool readU16(const char *hex, const char *end, uint16_t *out) {
	if (end - hex != 4) {
		return false;
	}
	char digits[5] = {hex[2], hex[3], hex[0], hex[1], 0};
	char *stop = NULL;
	*out = (uint16_t)strtoul(digits, &stop, 16);
	return !*stop;
}

// One month's "<N>:hex" (after "match_points.month"): a u16 points total, months numbered from 1
// in order.
// The N of "<prefix>N:" (`number` follows the prefix): months numbered from 1 in order, `count`
// read so far. 0, with the reason in `error`, otherwise.
static size_t monthNumber(
	const char *prefix,
	const char *number,
	const char *colon,
	size_t count,
	char *error,
	size_t errorSize
) {
	char *stop = NULL;
	unsigned long month = strtoul(number, &stop, 10);
	if (stop != colon || month != count + 1 || month > DEVELOPMENT_MONTH_LIMIT) {
		(void)snprintf(
			error, errorSize, "%s1 to %s%d must be in order", prefix, prefix, DEVELOPMENT_MONTH_LIMIT
		);
		return 0;
	}
	return month;
}

static bool parseMonth(
	const char *number,
	const char *colon,
	const char *end,
	DevelopmentValues *out,
	char *error,
	size_t errorSize
) {
	size_t month = monthNumber(MATCH_PREFIX, number, colon, out->matchMonths, error, errorSize);
	if (!month) {
		return false;
	}
	if (!readU16(colon + 1, end, &out->matchPoints[month - 1])) {
		(void)snprintf(error, errorSize, "%s%zu should be a u16", MATCH_PREFIX, month);
		return false;
	}
	out->matchMonths = month;
	return true;
}

// One month's "<N>:hex" (after "training_focus.month"): a u8 focus ID, 0-26 (the IDs the game
// accepts, sub_1439242B0) or 255 for none.
static bool parseFocusMonth(
	const char *number,
	const char *colon,
	const char *end,
	DevelopmentValues *out,
	char *error,
	size_t errorSize
) {
	size_t month = monthNumber(FOCUS_PREFIX, number, colon, out->focusMonths, error, errorSize);
	if (!month) {
		return false;
	}
	char digits[3] = {0};
	char *stop = NULL;
	unsigned long focus = 0;
	if (end - colon == 3) {
		memcpy(digits, colon + 1, 2);
		focus = strtoul(digits, &stop, 16);
	}
	if (!stop || *stop || (focus > FOCUS_LAST && focus != FOCUS_NONE)) {
		(void)snprintf(
			error,
			errorSize,
			"%s%zu must be 0 to %d, or %d for none",
			FOCUS_PREFIX,
			month,
			FOCUS_LAST,
			FOCUS_NONE
		);
		return false;
	}
	out->focus[month - 1] = (uint8_t)focus;
	out->focusMonths = month;
	return true;
}

// One month's "<N>.count:hex" or "<N>.minutes:hex" (after "matches.month"), each a u16: matches
// played and minutes in each, months numbered from 1 in order. developmentFile_parse checks that
// each month has both.
static bool parsePlayMonth(
	const char *number,
	const char *colon,
	const char *end,
	DevelopmentValues *out,
	char *error,
	size_t errorSize
) {
	char *stop = NULL;
	unsigned long month = strtoul(number, &stop, 10);
	size_t length = (size_t)(colon - stop);
	int part = length == 6 && memcmp(stop, ".count", 6) == 0     ? 1
						 : length == 8 && memcmp(stop, ".minutes", 8) == 0 ? 2
																															 : 0;
	if (!part || month == 0 || month > out->playMonths + 1 || month > DEVELOPMENT_MONTH_LIMIT ||
			(out->playParts[month - 1] & part)) {
		(void)snprintf(
			error,
			errorSize,
			"matches must be %s1.count and .minutes to %s%d, in order, each once",
			PLAY_PREFIX,
			PLAY_PREFIX,
			DEVELOPMENT_MONTH_LIMIT
		);
		return false;
	}
	uint16_t value = 0;
	int limit = part == 1 ? PLAY_COUNT_LIMIT : PLAY_MINUTES_LIMIT;
	if (!readU16(colon + 1, end, &value) || value > limit) {
		(void)snprintf(
			error, errorSize, "%s%lu%.*s must be 0 to %d", PLAY_PREFIX, month, (int)length, stop, limit
		);
		return false;
	}
	(part == 1 ? out->playCount : out->playMinutes)[month - 1] = (uint8_t)value;
	out->playParts[month - 1] |= (uint8_t)part;
	if (month > out->playMonths) {
		out->playMonths = month;
	}
	return true;
}

// One window's "<N>.start:hex" or "<N>.days:hex" (after "injury.window"), each a u16, windows
// numbered from 1 in order. developmentFile_parse checks that each has both.
static bool parseInjury(
	const char *number,
	const char *colon,
	const char *end,
	DevelopmentValues *out,
	char *error,
	size_t errorSize
) {
	char *stop = NULL;
	unsigned long window = strtoul(number, &stop, 10);
	size_t length = (size_t)(colon - stop);
	int part = length == 6 && memcmp(stop, ".start", 6) == 0  ? 1
						 : length == 5 && memcmp(stop, ".days", 5) == 0 ? 2
																														: 0;
	if (!part || window == 0 || window > out->injuryWindows + 1 ||
			window > DEVELOPMENT_INJURY_LIMIT || (out->injuryParts[window - 1] & part)) {
		(void)snprintf(
			error,
			errorSize,
			"injuries must be %s1.start and .days to %s%d, in order, each once",
			INJURY_PREFIX,
			INJURY_PREFIX,
			DEVELOPMENT_INJURY_LIMIT
		);
		return false;
	}
	if (!readU16(
				colon + 1, end, part == 1 ? &out->injuryStart[window - 1] : &out->injuryDays[window - 1]
			)) {
		(void)snprintf(
			error, errorSize, "%s%lu%.*s should be a u16", INJURY_PREFIX, window, (int)length, stop
		);
		return false;
	}
	out->injuryParts[window - 1] |= (uint8_t)part;
	if (window > out->injuryWindows) {
		out->injuryWindows = window;
	}
	return true;
}

// One "name:hex" entry of `text` (ending at `end`) into `out`.
static bool parseEntry(
	const char *text,
	const char *end,
	DevelopmentValues *out,
	size_t *used,
	char *error,
	size_t errorSize
) {
	const char *colon = memchr(text, ':', (size_t)(end - text));
	// Entries numbered by week, month or window, outside the field table.
	static const struct {
		const char *prefix;
		bool (*parse)(const char *, const char *, const char *, DevelopmentValues *, char *, size_t);
	} numbered[] = {
		{SCHEDULE_PREFIX, parseWeek},
		{MATCH_PREFIX, parseMonth},
		{INJURY_PREFIX, parseInjury},
		{FOCUS_PREFIX, parseFocusMonth},
		{PLAY_PREFIX, parsePlayMonth},
	};
	for (size_t i = 0; colon && i < sizeof numbered / sizeof numbered[0]; ++i) {
		size_t prefix = strlen(numbered[i].prefix);
		if (strncmp(text, numbered[i].prefix, prefix) == 0) {
			return numbered[i].parse(text + prefix, colon, end, out, error, errorSize);
		}
	}
	const Field *field = colon ? findField(text, (size_t)(colon - text)) : NULL;
	if (!field) {
		(void)snprintf(error, errorSize, "unknown field \"%.*s\"", (int)(end - text), text);
		return false;
	}
	size_t index = (size_t)(field - fields);
	for (size_t i = 0; i < out->count; ++i) {
		if (out->field[i] == index) {
			(void)snprintf(error, errorSize, "%s appears twice", field->name);
			return false;
		}
	}
	size_t size = developmentFormat_size(field);
	const char *hex = colon + 1;
	if ((size_t)(end - hex) != 2 * size || out->count == DEVELOPMENT_FIELD_LIMIT ||
			*used + size > DEVELOPMENT_SNAPSHOT_SIZE) {
		(void)snprintf(error, errorSize, "%s should be %zu bytes", field->name, size);
		return false;
	}
	uint8_t *bytes = out->bytes + *used;
	for (size_t i = 0; i < size; ++i) {
		char pair[3] = {hex[2 * i], hex[2 * i + 1], 0};
		char *stop = NULL;
		bytes[i] = (uint8_t)strtoul(pair, &stop, 16);
		if (*stop) {
			(void)snprintf(error, errorSize, "%s has a bad hex digit", field->name);
			return false;
		}
	}
	size_t values =
		field->kind == DEVELOPMENT_RATINGS || field->kind == DEVELOPMENT_CATEGORIES ? size : 1;
	for (size_t i = 0; i < values; ++i) {
		int value = developmentFormat_value(field, bytes + i);
		if (value < field->minimum || value > field->maximum ||
				(field->mask && (memory_loadU16(bytes) & ~field->mask))) {
			(void)snprintf(
				error, errorSize, "%s must be %d to %d", field->name, field->minimum, field->maximum
			);
			return false;
		}
	}
	out->field[out->count] = index;
	out->offset[out->count] = *used;
	++out->count;
	*used += size;
	return true;
}

bool developmentFile_parse(
	const char *text,
	DevelopmentValues *out,
	char *error,
	size_t errorSize
) {
	*out = (DevelopmentValues){0};
	size_t used = 0;
	while (*text) {
		const char *end = strchr(text, ',');
		end = end ? end : text + strlen(text);
		if (!parseEntry(text, end, out, &used, error, errorSize)) {
			return false;
		}
		text = *end ? end + 1 : end;
	}
	for (size_t i = 0; i < out->injuryWindows; ++i) {
		if (out->injuryParts[i] != 3 || out->injuryDays[i] == 0) {
			(void)snprintf(
				error, errorSize, "%s%zu needs .start and .days (1 or more)", INJURY_PREFIX, i + 1
			);
			return false;
		}
	}
	for (size_t i = 0; i < out->playMonths; ++i) {
		if (out->playParts[i] != 3) {
			(void)snprintf(error, errorSize, "%s%zu needs .count and .minutes", PLAY_PREFIX, i + 1);
			return false;
		}
	}
	return true;
}

bool developmentFile_fits(
	uint8_t *player,
	const DevelopmentValues *values,
	char *error,
	size_t errorSize
) {
	for (size_t i = 0; i < values->count; ++i) {
		const Field *field = &fields[values->field[i]];
		if (baseOf(field) != ON_FOCUS && !locate(player, field)) { // apply creates a focus record
			(void)snprintf(error, errorSize, "the player has no %s record", field->name);
			return false;
		}
	}
	return true;
}

void developmentFile_save(uint8_t *player, DevelopmentSnapshot *out) {
	size_t used = 0;
	for (size_t i = 0; i < FIELD_COUNT; ++i) {
		const uint8_t *at = locate(player, &fields[i]);
		out->present[i] = at != NULL;
		if (at) {
			memcpy(out->bytes + used, at, developmentFormat_size(&fields[i]));
		}
		used += developmentFormat_size(&fields[i]);
	}
}

void developmentFile_restore(uint8_t *player, const DevelopmentSnapshot *snapshot) {
	size_t used = 0;
	for (size_t i = 0; i < FIELD_COUNT; ++i) {
		uint8_t *at = locate(player, &fields[i]);
		if (at && snapshot->present[i]) {
			memcpy(at, snapshot->bytes + used, developmentFormat_size(&fields[i]));
		} else if (at && baseOf(&fields[i]) == ON_FOCUS) {
			// shortcut: drops the focus record apply created, leaking its 10 pool bytes; free it
			// through the pool if records ever pile up.
			memcpy(player + PLAYER_FOCUS_RECORD, &(void *){NULL}, sizeof(void *));
		}
		used += developmentFormat_size(&fields[i]);
	}
}

void developmentFile_apply(uint8_t *player, const DevelopmentValues *values) {
	for (size_t i = 0; i < values->count; ++i) {
		const Field *field = &fields[values->field[i]];
		if (baseOf(field) == ON_FOCUS) {
			((FocusRecordFn)game_function(RVA_FOCUS_RECORD))((uint8_t **)(player + PLAYER_FOCUS_RECORD));
		}
		uint8_t *at = locate(player, field);
		const uint8_t *value = values->bytes + values->offset[i];
		if (!at) {
			continue; // checked by developmentFile_fits
		}
		if (field->mask) {
			uint16_t word =
				(uint16_t)((memory_loadU16(at) & ~field->mask) | (memory_loadU16(value) & field->mask));
			memcpy(at, &word, sizeof word);
		} else {
			memcpy(at, value, developmentFormat_size(field));
		}
	}
	// develop writes each month's focus into the record, so it must exist; restore removes it again
	// if it was made here, as for training_focus.
	if (values->focusMonths) {
		((FocusRecordFn)game_function(RVA_FOCUS_RECORD))((uint8_t **)(player + PLAYER_FOCUS_RECORD));
	}
}
