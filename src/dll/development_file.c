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
	{"training_happiness", KIND_U8, ON_HAPPINESS, 0x0C, 0, 100, 0, true, "0-100"},
	// Changes at the weekly training review; meaning unknown (knowledge-base section 13).
	{"training_happiness.unknown_08",
		KIND_U16,
		ON_HAPPINESS,
		0x08,
		0,
		65535,
		0,
		true,
		"changes at the weekly training review; meaning unknown"},
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
		"a total and a count moved to last_* each period; meaning unknown"},
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
		"best club reputation / 10 with 11+ appearances in a season"},
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
		"  \"format\": \"fm24-development/1\",\n  \"source\": "
	);
	configWriter_writeString(file, name);
	fprintf(
		file,
		",\n  \"fields\": [\n    // the player (don't change)\n"
		"    {\"name\": \"person_uid\", \"player\": "
	);
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
	size_t prefix = sizeof SCHEDULE_PREFIX - 1;
	if (colon && strncmp(text, SCHEDULE_PREFIX, prefix) == 0) {
		return parseWeek(text + prefix, colon, end, out, error, errorSize);
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
}
