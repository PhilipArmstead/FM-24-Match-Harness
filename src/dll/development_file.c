// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "development_file.h"

#include "config_writer.h"
#include "game.h"
#include "labels.h"
#include "memory.h"
#include "session_names.h"

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

// KIND_RATINGS and KIND_CATEGORIES are u8 maps: 69 ratings, or one value per training category.
typedef enum Kind {
	KIND_U8,
	KIND_U16,
	KIND_S8,
	KIND_S16,
	KIND_RATINGS,
	KIND_F32,
	KIND_CATEGORIES
} Kind;

typedef struct Field {
	const char *name;
	Kind kind;
	Base base;
	size_t offset;
	int minimum; // per value; each byte for KIND_RATINGS
	int maximum;
	uint16_t mask;    // the bits of a u16 the field owns, 0 for all of it
	bool internal;    // state the game computes: written last, under "here be dragons"
	const char *note; // written as a comment above the field, or NULL
} Field;

// Everything the development routine is known to read that is plain data (no pointers).
static const Field fields[] = {
	{"current_ability", KIND_U16, ON_PLAYER, PLAYER_CURRENT_ABILITY, 1, 200, 0, false, "1-200"},
	{"potential_ability", KIND_U16, ON_PLAYER, PLAYER_POTENTIAL_ABILITY, 1, 200, 0, false, "1-200"},
	{"ratings",
		KIND_RATINGS,
		ON_PLAYER,
		PLAYER_RATINGS,
		1,
		100,
		0,
		false,
		"by name: positions 1-20, attributes 1-100"},
	{"training_progress.strength",
		KIND_S8,
		ON_PLAYER,
		PLAYER_TRAINING_PROGRESS + 0,
		-100,
		50,
		0,
		true,
		"per category, -100 (untrained) to 50; development turns it into attribute growth"},
	{"training_progress.quickness",
		KIND_S8,
		ON_PLAYER,
		PLAYER_TRAINING_PROGRESS + 1,
		-100,
		50,
		0,
		true,
		NULL},
	{"training_progress.gk_shot_stopping",
		KIND_S8,
		ON_PLAYER,
		PLAYER_TRAINING_PROGRESS + 2,
		-100,
		50,
		0,
		true,
		NULL},
	{"training_progress.gk_handling",
		KIND_S8,
		ON_PLAYER,
		PLAYER_TRAINING_PROGRESS + 3,
		-100,
		50,
		0,
		true,
		NULL},
	{"training_progress.defending",
		KIND_S8,
		ON_PLAYER,
		PLAYER_TRAINING_PROGRESS + 4,
		-100,
		50,
		0,
		true,
		NULL},
	{"training_progress.tackling",
		KIND_S8,
		ON_PLAYER,
		PLAYER_TRAINING_PROGRESS + 5,
		-100,
		50,
		0,
		true,
		NULL},
	{"training_progress.tactics",
		KIND_S8,
		ON_PLAYER,
		PLAYER_TRAINING_PROGRESS + 6,
		-100,
		50,
		0,
		true,
		NULL},
	{"training_progress.ball_control",
		KIND_S8,
		ON_PLAYER,
		PLAYER_TRAINING_PROGRESS + 7,
		-100,
		50,
		0,
		true,
		NULL},
	{"training_progress.attacking",
		KIND_S8,
		ON_PLAYER,
		PLAYER_TRAINING_PROGRESS + 8,
		-100,
		50,
		0,
		true,
		NULL},
	{"training_progress.shooting",
		KIND_S8,
		ON_PLAYER,
		PLAYER_TRAINING_PROGRESS + 9,
		-100,
		50,
		0,
		true,
		NULL},
	{"training_progress.set_pieces",
		KIND_S8,
		ON_PLAYER,
		PLAYER_TRAINING_PROGRESS + 10,
		-100,
		50,
		0,
		true,
		NULL},
	// 0 or 1 normal, 2 double, 254 half, 255 the team's workload (knowledge-base section 6)
	{"training_intensity",
		KIND_U8,
		ON_PLAYER,
		PLAYER_TRAINING_INTENSITY,
		0,
		255,
		0,
		false,
		"0 or 1 normal, 2 double, 254 half, 255 the team's workload"},
	// The monthly routine boosts the focus's attributes for IDs 5 and up; 0-4 are the injury
	// recovery focuses (knowledge-base section 15).
	{"training_focus",
		KIND_U8,
		ON_FOCUS,
		FOCUS_ID,
		0,
		255,
		0,
		false,
		"additional focus: 255 none, 9 quickness, 12 endurance, 15 shooting ... (IDs in docs/CLI.md)"},
	{"condition", KIND_U16, ON_PLAYER, PLAYER_CONDITION, 0, 10000, 0, true, "0-10000"},
	{"fatigue", KIND_S16, ON_PLAYER, PLAYER_FATIGUE, -32768, 32767, 0, true, "0 is fresh"},
	{"sharpness", KIND_U16, ON_PLAYER, PLAYER_SHARPNESS, 0, 10000, 0, true, "0-10000"},
	{"morale", KIND_U8, ON_PLAYER, PLAYER_MORALE, 0, 255, 0, true, "0-255"},
	{"height", KIND_U16, ON_PLAYER, PLAYER_HEIGHT, 100, 230, 0, false, "cm"},
	{"weight", KIND_U8, ON_PLAYER, PLAYER_WEIGHT, 30, 150, 0, false, "kg"},
	{"personality.adaptability",
		KIND_U8,
		ON_PERSON,
		PERSON_PERSONALITY + 0,
		1,
		20,
		0,
		false,
		"1-20 each"},
	{"personality.ambition", KIND_U8, ON_PERSON, PERSON_PERSONALITY + 1, 1, 20, 0, false, NULL},
	{"personality.loyalty", KIND_U8, ON_PERSON, PERSON_PERSONALITY + 2, 1, 20, 0, false, NULL},
	{"personality.pressure", KIND_U8, ON_PERSON, PERSON_PERSONALITY + 3, 1, 20, 0, false, NULL},
	{"personality.professionalism",
		KIND_U8,
		ON_PERSON,
		PERSON_PERSONALITY + 4,
		1,
		20,
		0,
		false,
		NULL},
	{"personality.sportsmanship", KIND_U8, ON_PERSON, PERSON_PERSONALITY + 5, 1, 20, 0, false, NULL},
	{"personality.temperament", KIND_U8, ON_PERSON, PERSON_PERSONALITY + 6, 1, 20, 0, false, NULL},
	{"personality.controversy", KIND_U8, ON_PERSON, PERSON_PERSONALITY + 7, 1, 20, 0, false, NULL},
	// The day of the year counts from 1; the u16's other bits are kept.
	{"date_of_birth.day",
		KIND_U16,
		ON_PERSON,
		PERSON_DATE_OF_BIRTH,
		1,
		366,
		0x1FF,
		false,
		"day of the year, from 1"},
	{"date_of_birth.year", KIND_U16, ON_PERSON, PERSON_DATE_OF_BIRTH + 2, 1900, 2100, 0, false, NULL},
	{"training_happiness", KIND_S8, ON_HAPPINESS, 0x0C, -100, 100, 0, true, "-100 to 100"},
	// The last change to training_happiness (new - old, as s16), written with it (sub_143923980).
	{"training_happiness.unknown_08",
		KIND_U16,
		ON_HAPPINESS,
		0x08,
		0,
		65535,
		0,
		true,
		"the last change to training_happiness, as s16 bits"},
	// Development state the routine reads and updates (knowledge-base section 16).
	{"last_development.gains",
		KIND_S16,
		ON_PLAYER,
		PLAYER_DEVELOPMENT_GAINS,
		-32768,
		32767,
		0,
		true,
		"rating points from the last development"},
	{"last_development.other_gains",
		KIND_S16,
		ON_PLAYER,
		PLAYER_DEVELOPMENT_GAINS + 2,
		-32768,
		32767,
		0,
		true,
		NULL},
	{"weak_foot_progress",
		KIND_U16,
		ON_PLAYER,
		PLAYER_WEAK_FOOT_PROGRESS,
		0,
		65535,
		0,
		true,
		"the weaker foot gains 1 above 10 x its rating + 500"},
	{"development_period.total",
		KIND_U16,
		ON_PLAYER,
		PLAYER_DEVELOPMENT_PERIOD,
		0,
		65535,
		0,
		true,
		"match points and matches since the last development, moved to last_* on that day; plan "
		"later months with match_points.monthN (docs/CLI.md)"},
	{"development_period.last_total",
		KIND_S16,
		ON_PLAYER,
		PLAYER_DEVELOPMENT_PERIOD + 2,
		-32768,
		32767,
		0,
		true,
		NULL},
	{"development_period.last_count",
		KIND_U8,
		ON_PLAYER,
		PLAYER_DEVELOPMENT_PERIOD + 12,
		0,
		255,
		0,
		true,
		NULL},
	{"development_period.count",
		KIND_U8,
		ON_PLAYER,
		PLAYER_DEVELOPMENT_PERIOD + 13,
		0,
		255,
		0,
		true,
		NULL},
	{"rating_progress.important_matches",
		KIND_U16,
		ON_PLAYER,
		PLAYER_RATING_PROGRESS,
		0,
		65535,
		0,
		true,
		"the rating gains 1 at a threshold"},
	{"rating_progress.determination",
		KIND_U16,
		ON_PLAYER,
		PLAYER_RATING_PROGRESS + 2,
		0,
		65535,
		0,
		true,
		NULL},
	{"personality_progress.pressure",
		KIND_U16,
		ON_PLAYER,
		PLAYER_PERSONALITY_PROGRESS,
		0,
		65535,
		0,
		true,
		"the trait gains 1 at a threshold"},
	{"personality_progress.professionalism",
		KIND_U16,
		ON_PLAYER,
		PLAYER_PERSONALITY_PROGRESS + 2,
		0,
		65535,
		0,
		true,
		NULL},
	{"played_level",
		KIND_U8,
		ON_PLAYER,
		PLAYER_PLAYED_LEVEL,
		0,
		255,
		0,
		true,
		"best league reputation / 10 with 11+ league appearances in a season"},
	{"played_level.appearances",
		KIND_U8,
		ON_PLAYER,
		PLAYER_PLAYED_LEVEL + 1,
		0,
		120,
		0,
		true,
		"appearances there, 0-120"},
	{"development_flags",
		KIND_U16,
		ON_PLAYER,
		PLAYER_DEVELOPMENT_FLAGS,
		0,
		65535,
		0,
		true,
		"0x40 late developer, 0x200 late growth due, 0x100 late growth had"},
	{"training_rating", KIND_U8, ON_PLAYER, PLAYER_TRAINING_RATING, 0, 20, 0, true, "0-20"},
	// shortcut: the float travels as its 4 bytes ("hex"); add a float kind if people edit it.
	{"ability_carry",
		KIND_F32,
		ON_PLAYER,
		PLAYER_ABILITY_CARRY,
		0,
		0,
		0,
		true,
		"the 4 bytes of a float: CA growth carried to the next development"},
	// The team's, not the player's: the weekly training reads them (knowledge-base section 18).
	{"coaching",
		KIND_CATEGORIES,
		ON_TEAM_TRAINING,
		0,
		0,
		100,
		0,
		false,
		"the team's, 0-100 per category: best coach + training facilities - 10"},
	{"coach_workload",
		KIND_CATEGORIES,
		ON_TEAM_TRAINING,
		TRAINING_CATEGORY_COUNT,
		0,
		100,
		0,
		false,
		"the team's, 0-100 per category: progress builds (100 - workload)% as fast"},
	{"training_facilities",
		KIND_U8,
		ON_CLUB_FACILITIES,
		FACILITIES_TRAINING,
		1,
		20,
		0,
		false,
		"the club's, 1-20; change coaching by the same amount"},
};

enum { FIELD_COUNT = sizeof fields / sizeof fields[0] };
_Static_assert(
	(int)FIELD_COUNT <= (int)DEVELOPMENT_FIELD_LIMIT,
	"DevelopmentSnapshot.present is too small"
);

static const char SCHEDULE_PREFIX[] = "schedule.week";
static const char MATCH_PREFIX[] = "match_points.month";
static const char INJURY_PREFIX[] = "injury.window";
static const char FOCUS_PREFIX[] = "training_focus.month";
static const char PLAY_PREFIX[] = "matches.month";
// A month's matches, and the minutes in each (120 with extra time).
enum { PLAY_COUNT_LIMIT = 31, PLAY_MINUTES_LIMIT = 120 };
enum { FOCUS_LAST = 26, FOCUS_NONE = 255 };

static size_t sizeOf(const Field *field) {
	switch (field->kind) {
		case KIND_U8:
		case KIND_S8: return 1;
		case KIND_U16:
		case KIND_S16: return 2;
		case KIND_F32: return 4;
		case KIND_RATINGS: return PLAYER_RATING_COUNT;
		case KIND_CATEGORIES: return TRAINING_CATEGORY_COUNT;
	}
	return 0;
}

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
	switch (field->base) {
		case ON_PLAYER: return player + field->offset;
		case ON_PERSON: return player + PLAYER_PERSON + field->offset;
		case ON_HAPPINESS: {
			uint8_t *record = happinessRecord(player);
			return record ? record + field->offset : NULL;
		}
		case ON_TEAM_TRAINING: {
			uint8_t *team = trainingTeam(player);
			uint8_t *data = NULL;
			uint8_t *info = NULL;
			return team && memory_readPointer(team, TEAM_IN_GAME_DATA, &data) && data &&
								 memory_readPointer(data, TEAM_TRAINING_INFO, &info) && info
							 ? info + field->offset
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
							 ? facilities + field->offset
							 : NULL;
		}
		case ON_FOCUS: {
			uint8_t *record = NULL;
			return memory_readPointer(player, PLAYER_FOCUS_RECORD, &record) && record
							 ? record + field->offset
							 : NULL;
		}
	}
	return NULL;
}

static int valueAt(const Field *field, const uint8_t *bytes) {
	uint16_t word = memory_loadU16(bytes);
	switch (field->kind) {
		case KIND_U8:
		case KIND_RATINGS:
		case KIND_CATEGORIES: return bytes[0];
		case KIND_S8: return (int8_t)bytes[0];
		case KIND_U16: return field->mask ? word & field->mask : word;
		case KIND_S16: return (int16_t)word;
		case KIND_F32: return 0; // not range-checked
	}
	return 0;
}

static const char *keyOf(const Field *field) {
	static const char *const keys[] = {"u8", "u16", "s8", "s16", "u8map", "hex", "u8map"};
	return keys[field->kind];
}

static bool writeField(FILE *file, uint8_t *player, const Field *field, const char *heading);
static void writeAttributeChanges(FILE *file, uint8_t *player);
static void writeSchedule(FILE *file, const uint16_t *schedule);

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
	fprintf(
		file,
		"{\n"
		"  // Edit values, then: fm24ctl develop <this file> <months> <seeds> <output.csv>\n"
		"  // Change values only: don't add, rename or reorder fields, ratings or categories.\n"
		"  // Remove a field to keep the player's own value. More in docs/CLI.md.\n"
		"  // Add match_points.month1, month2 ... (u16) and matches.month1.count / .minutes ...\n"
		"  // (u16) to plan match time,\n"
		"  // training_focus.month1 ... (u8) to plan the focus month by month, and\n"
		"  // injury.window1.start / .days ... (u16) to plan injuries; see docs/CLI.md.\n"
	);
	uint16_t day = 0;
	uint16_t year = 0;
	const uint8_t *date = game_address(RVA_CURRENT_DATE);
	if (memory_readU16(date, 0, &day) && memory_readU16(date, 2, &year)) {
		fprintf(file, "  // Exported on day %u of %u (game date).\n", day & 0x1FF, year);
	}
	writeAttributeChanges(file, player);
	fprintf(file, "  \"format\": \"fm24-development/1\",\n  \"source\": ");
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
		writeSchedule(file, schedule);
	}
	const char *heading =
		"\n    // here be dragons\n"
		"    // The game computes these as it plays; to simulate training, leave them alone.\n";
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

// One field as ",\n" + `heading` (unless NULL) + its note + the entry. False if the player has no
// such field (no training happiness record, or no team coaching record).
static bool writeField(FILE *file, uint8_t *player, const Field *field, const char *heading) {
	const uint8_t *at = locate(player, field);
	if (!at) {
		return false;
	}
	fprintf(file, ",\n%s", heading ? heading : "");
	if (field->note) {
		fprintf(file, "    // %s\n", field->note);
	}
	fprintf(file, "    {\"name\": \"%s\", \"%s\": ", field->name, keyOf(field));
	if (field->kind == KIND_RATINGS || field->kind == KIND_CATEGORIES) {
		bool ratings = field->kind == KIND_RATINGS;
		fprintf(file, "{");
		for (size_t k = 0; k < sizeOf(field); ++k) {
			const char *key = ratings ? labels_ratingName(k) : labels_categoryName(k);
			fprintf(file, "%s\"%s\": %u", k ? ", " : "", key, at[k]);
		}
		fprintf(file, "}}");
	} else if (field->kind == KIND_F32) {
		fprintf(file, "\"%02x%02x%02x%02x\"}", at[0], at[1], at[2], at[3]);
	} else {
		fprintf(file, "%d}", valueAt(field, at));
	}
	return true;
}

// The schedule as schedule.week1: session names, one day per line.
static void writeSchedule(FILE *file, const uint16_t *schedule) {
	fprintf(
		file,
		",\n    // sessions by name as in the game (or ID); add %s2 etc. for a rotation, or"
		" remove to train on the club's schedules\n    {\"name\": \"%s1\", \"sessions\": {",
		SCHEDULE_PREFIX,
		SCHEDULE_PREFIX
	);
	for (size_t i = 0; i < SCHEDULE_SESSIONS; ++i) {
		size_t slot = i % SCHEDULE_SLOTS;
		const char *separator = slot ? ", " : i ? ",\n      " : "\n      ";
		fprintf(file, "%s\"day%zu.slot%zu\": ", separator, i / SCHEDULE_SLOTS + 1, slot + 1);
		const char *session = session_name(schedule[i]);
		if (session) {
			configWriter_writeString(file, session);
		} else {
			fprintf(file, "%u", schedule[i]); // a session the harness has no name for
		}
	}
	fprintf(file, "\n    }}");
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
	size_t size = sizeOf(field);
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
	size_t values = field->kind == KIND_RATINGS || field->kind == KIND_CATEGORIES ? size : 1;
	for (size_t i = 0; i < values; ++i) {
		int value = valueAt(field, bytes + i);
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
		if (field->base != ON_FOCUS && !locate(player, field)) { // apply creates a focus record
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
			memcpy(out->bytes + used, at, sizeOf(&fields[i]));
		}
		used += sizeOf(&fields[i]);
	}
}

void developmentFile_restore(uint8_t *player, const DevelopmentSnapshot *snapshot) {
	size_t used = 0;
	for (size_t i = 0; i < FIELD_COUNT; ++i) {
		uint8_t *at = locate(player, &fields[i]);
		if (at && snapshot->present[i]) {
			memcpy(at, snapshot->bytes + used, sizeOf(&fields[i]));
		} else if (at && fields[i].base == ON_FOCUS) {
			// shortcut: drops the focus record apply created, leaking its 10 pool bytes; free it
			// through the pool if records ever pile up.
			memcpy(player + PLAYER_FOCUS_RECORD, &(void *){NULL}, sizeof(void *));
		}
		used += sizeOf(&fields[i]);
	}
}

void developmentFile_apply(uint8_t *player, const DevelopmentValues *values) {
	for (size_t i = 0; i < values->count; ++i) {
		const Field *field = &fields[values->field[i]];
		if (field->base == ON_FOCUS) {
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
			memcpy(at, value, sizeOf(field));
		}
	}
	// develop writes each month's focus into the record, so it must exist; restore removes it again
	// if it was made here, as for training_focus.
	if (values->focusMonths) {
		((FocusRecordFn)game_function(RVA_FOCUS_RECORD))((uint8_t **)(player + PLAYER_FOCUS_RECORD));
	}
}
