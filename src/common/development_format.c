// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "development_format.h"

#include "session_names.h"


// Everything the development routine is known to read that is plain data (no pointers).
const DevelopmentField developmentFields[DEVELOPMENT_FIELD_COUNT] = {
	[DEVELOPMENT_CURRENT_ABILITY] = {"current_ability", DEVELOPMENT_U16, 1, 200, 0, false, "1-200"},
	[DEVELOPMENT_POTENTIAL_ABILITY] =
		{"potential_ability", DEVELOPMENT_U16, 1, 200, 0, false, "1-200"},
	[DEVELOPMENT_RATINGS_FIELD] =
		{"ratings", DEVELOPMENT_RATINGS, 1, 100, 0, false, "by name: positions 1-20, attributes 1-100"},
	[DEVELOPMENT_PROGRESS_STRENGTH] = {"training_progress.strength",
		DEVELOPMENT_S8,
		-100,
		50,
		0,
		true,
		"per category, -100 (untrained) to 50; development turns it into attribute growth"},
	[DEVELOPMENT_PROGRESS_QUICKNESS] =
		{"training_progress.quickness", DEVELOPMENT_S8, -100, 50, 0, true, NULL},
	[DEVELOPMENT_PROGRESS_GK_SHOT_STOPPING] =
		{"training_progress.gk_shot_stopping", DEVELOPMENT_S8, -100, 50, 0, true, NULL},
	[DEVELOPMENT_PROGRESS_GK_HANDLING] =
		{"training_progress.gk_handling", DEVELOPMENT_S8, -100, 50, 0, true, NULL},
	[DEVELOPMENT_PROGRESS_DEFENDING] =
		{"training_progress.defending", DEVELOPMENT_S8, -100, 50, 0, true, NULL},
	[DEVELOPMENT_PROGRESS_TACKLING] =
		{"training_progress.tackling", DEVELOPMENT_S8, -100, 50, 0, true, NULL},
	[DEVELOPMENT_PROGRESS_TACTICS] =
		{"training_progress.tactics", DEVELOPMENT_S8, -100, 50, 0, true, NULL},
	[DEVELOPMENT_PROGRESS_BALL_CONTROL] =
		{"training_progress.ball_control", DEVELOPMENT_S8, -100, 50, 0, true, NULL},
	[DEVELOPMENT_PROGRESS_ATTACKING] =
		{"training_progress.attacking", DEVELOPMENT_S8, -100, 50, 0, true, NULL},
	[DEVELOPMENT_PROGRESS_SHOOTING] =
		{"training_progress.shooting", DEVELOPMENT_S8, -100, 50, 0, true, NULL},
	[DEVELOPMENT_PROGRESS_SET_PIECES] =
		{"training_progress.set_pieces", DEVELOPMENT_S8, -100, 50, 0, true, NULL},
	// 0 or 1 normal, 2 double, 254 half, 255 the team's workload (knowledge-base section 6)
	[DEVELOPMENT_TRAINING_INTENSITY] = {"training_intensity",
		DEVELOPMENT_U8,
		0,
		255,
		0,
		false,
		"0 or 1 normal, 2 double, 254 half, 255 the team's workload"},
	// The monthly routine boosts the focus's attributes for IDs 5 and up; 0-4 are the injury
	// recovery focuses (knowledge-base section 15).
	[DEVELOPMENT_TRAINING_FOCUS] = {"training_focus",
		DEVELOPMENT_U8,
		0,
		255,
		0,
		false,
		"additional focus: 255 none, 9 quickness, 12 endurance, 15 shooting ... (IDs in docs/CLI.md)"},
	[DEVELOPMENT_CONDITION] = {"condition", DEVELOPMENT_U16, 0, 10000, 0, true, "0-10000"},
	[DEVELOPMENT_FATIGUE] = {"fatigue", DEVELOPMENT_S16, -32768, 32767, 0, true, "0 is fresh"},
	[DEVELOPMENT_SHARPNESS] = {"sharpness", DEVELOPMENT_U16, 0, 10000, 0, true, "0-10000"},
	[DEVELOPMENT_MORALE] = {"morale", DEVELOPMENT_U8, 0, 255, 0, true, "0-255"},
	[DEVELOPMENT_HEIGHT] = {"height", DEVELOPMENT_U16, 100, 230, 0, false, "cm"},
	[DEVELOPMENT_WEIGHT] = {"weight", DEVELOPMENT_U8, 30, 150, 0, false, "kg"},
	[DEVELOPMENT_ADAPTABILITY] =
		{"personality.adaptability", DEVELOPMENT_U8, 1, 20, 0, false, "1-20 each"},
	[DEVELOPMENT_AMBITION] = {"personality.ambition", DEVELOPMENT_U8, 1, 20, 0, false, NULL},
	[DEVELOPMENT_LOYALTY] = {"personality.loyalty", DEVELOPMENT_U8, 1, 20, 0, false, NULL},
	[DEVELOPMENT_PRESSURE] = {"personality.pressure", DEVELOPMENT_U8, 1, 20, 0, false, NULL},
	[DEVELOPMENT_PROFESSIONALISM] =
		{"personality.professionalism", DEVELOPMENT_U8, 1, 20, 0, false, NULL},
	[DEVELOPMENT_SPORTSMANSHIP] =
		{"personality.sportsmanship", DEVELOPMENT_U8, 1, 20, 0, false, NULL},
	[DEVELOPMENT_TEMPERAMENT] = {"personality.temperament", DEVELOPMENT_U8, 1, 20, 0, false, NULL},
	[DEVELOPMENT_CONTROVERSY] = {"personality.controversy", DEVELOPMENT_U8, 1, 20, 0, false, NULL},
	// The day of the year counts from 1; the u16's other bits are kept.
	[DEVELOPMENT_BIRTH_DAY] =
		{"date_of_birth.day", DEVELOPMENT_U16, 1, 366, 0x1FF, false, "day of the year, from 1"},
	[DEVELOPMENT_BIRTH_YEAR] = {"date_of_birth.year", DEVELOPMENT_U16, 1900, 2100, 0, false, NULL},
	[DEVELOPMENT_TRAINING_HAPPINESS] =
		{"training_happiness", DEVELOPMENT_S8, -100, 100, 0, true, "-100 to 100"},
	// The last change to training_happiness (new - old, as s16), written with it (sub_143923980).
	[DEVELOPMENT_HAPPINESS_CHANGE] = {"training_happiness.unknown_08",
		DEVELOPMENT_U16,
		0,
		65535,
		0,
		true,
		"the last change to training_happiness, as s16 bits"},
	// Development state the routine reads and updates (knowledge-base section 16).
	[DEVELOPMENT_LAST_GAINS] = {"last_development.gains",
		DEVELOPMENT_S16,
		-32768,
		32767,
		0,
		true,
		"rating points from the last development"},
	[DEVELOPMENT_LAST_OTHER_GAINS] =
		{"last_development.other_gains", DEVELOPMENT_S16, -32768, 32767, 0, true, NULL},
	[DEVELOPMENT_WEAK_FOOT_PROGRESS] = {"weak_foot_progress",
		DEVELOPMENT_U16,
		0,
		65535,
		0,
		true,
		"the weaker foot gains 1 above 10 x its rating + 500"},
	[DEVELOPMENT_PERIOD_TOTAL] = {"development_period.total",
		DEVELOPMENT_U16,
		0,
		65535,
		0,
		true,
		"match points and matches since the last development, moved to last_* on that day; plan "
		"later months with match_points.monthN (docs/CLI.md)"},
	[DEVELOPMENT_PERIOD_LAST_TOTAL] =
		{"development_period.last_total", DEVELOPMENT_S16, -32768, 32767, 0, true, NULL},
	[DEVELOPMENT_PERIOD_LAST_COUNT] =
		{"development_period.last_count", DEVELOPMENT_U8, 0, 255, 0, true, NULL},
	[DEVELOPMENT_PERIOD_COUNT] = {"development_period.count", DEVELOPMENT_U8, 0, 255, 0, true, NULL},
	[DEVELOPMENT_IMPORTANT_MATCHES_PROGRESS] = {"rating_progress.important_matches",
		DEVELOPMENT_U16,
		0,
		65535,
		0,
		true,
		"the rating gains 1 at a threshold"},
	[DEVELOPMENT_DETERMINATION_PROGRESS] =
		{"rating_progress.determination", DEVELOPMENT_U16, 0, 65535, 0, true, NULL},
	[DEVELOPMENT_PRESSURE_PROGRESS] = {"personality_progress.pressure",
		DEVELOPMENT_U16,
		0,
		65535,
		0,
		true,
		"the trait gains 1 at a threshold"},
	[DEVELOPMENT_PROFESSIONALISM_PROGRESS] =
		{"personality_progress.professionalism", DEVELOPMENT_U16, 0, 65535, 0, true, NULL},
	[DEVELOPMENT_PLAYED_LEVEL] = {"played_level",
		DEVELOPMENT_U8,
		0,
		255,
		0,
		true,
		"best league reputation / 10 with 11+ league appearances in a season"},
	[DEVELOPMENT_PLAYED_APPEARANCES] =
		{"played_level.appearances", DEVELOPMENT_U8, 0, 120, 0, true, "appearances there, 0-120"},
	[DEVELOPMENT_FLAGS] = {"development_flags",
		DEVELOPMENT_U16,
		0,
		65535,
		0,
		true,
		"0x40 late developer, 0x200 late growth due, 0x100 late growth had"},
	[DEVELOPMENT_TRAINING_RATING] = {"training_rating", DEVELOPMENT_U8, 0, 20, 0, true, "0-20"},
	// shortcut: the float travels as its 4 bytes ("hex"); add a float kind if people edit it.
	[DEVELOPMENT_ABILITY_CARRY] = {"ability_carry",
		DEVELOPMENT_F32,
		0,
		0,
		0,
		true,
		"the 4 bytes of a float: CA growth carried to the next development"},
	// The team's, not the player's: the weekly training reads them (knowledge-base section 18).
	[DEVELOPMENT_COACHING] = {"coaching",
		DEVELOPMENT_CATEGORIES,
		0,
		100,
		0,
		false,
		"the team's, 0-100 per category: best coach + training facilities - 10"},
	[DEVELOPMENT_COACH_WORKLOAD] = {"coach_workload",
		DEVELOPMENT_CATEGORIES,
		0,
		100,
		0,
		false,
		"the team's, 0-100 per category: progress builds (100 - workload)% as fast"},
	[DEVELOPMENT_TRAINING_FACILITIES] = {"training_facilities",
		DEVELOPMENT_U8,
		1,
		20,
		0,
		false,
		"the club's, 1-20; change coaching by the same amount"},
};

const char developmentFormat_header[] =
	"  // Edit values, then: fm24ctl develop <this file> <months> <seeds> <output.csv>\n"
	"  // Change values only: don't add, rename or reorder fields, ratings or categories.\n"
	"  // Remove a field to keep the player's own value. More in docs/CLI.md.\n"
	"  // Add match_points.month1, month2 ... (u16) and matches.month1.count / .minutes ...\n"
	"  // (u16) to plan match time,\n"
	"  // training_focus.month1 ... (u8) to plan the focus month by month, and\n"
	"  // injury.window1.start / .days ... (u16) to plan injuries; see docs/CLI.md.\n";

const char developmentFormat_dragons[] =
	"\n    // here be dragons\n"
	"    // The game computes these as it plays; to simulate training, leave them alone.\n";

static const char *const ratingNames[DEVELOPMENT_RATING_COUNT] = {
	"pos_gk",
	"pos_sw",
	"pos_dl",
	"pos_dc",
	"pos_dr",
	"pos_dm",
	"pos_ml",
	"pos_mc",
	"pos_mr",
	"pos_aml",
	"pos_amc",
	"pos_amr",
	"pos_st",
	"pos_wbl",
	"pos_wbr",
	"crossing",
	"dribbling",
	"finishing",
	"heading",
	"long_shots",
	"marking",
	"off_the_ball",
	"passing",
	"penalty_taking",
	"tackling",
	"vision",
	"handling",
	"aerial_reach",
	"command_of_area",
	"communication",
	"kicking",
	"throwing",
	"anticipation",
	"decisions",
	"one_on_ones",
	"positioning",
	"reflexes",
	"first_touch",
	"technique",
	"left_foot",
	"right_foot",
	"flair",
	"corners",
	"teamwork",
	"work_rate",
	"long_throws",
	"eccentricity",
	"rushing_out",
	"punching",
	"acceleration",
	"free_kicks",
	"strength",
	"stamina",
	"pace",
	"jumping_reach",
	"leadership",
	"dirtiness",
	"balance",
	"bravery",
	"consistency",
	"aggression",
	"agility",
	"important_matches",
	"injury_proneness",
	"versatility",
	"natural_fitness",
	"determination",
	"composure",
	"concentration",
};

// The 11 training categories (knowledge-base/FM24 Training — Attribute Growth.md, section 3).
static const char *const categoryNames[DEVELOPMENT_CATEGORY_COUNT] = {
	"strength",
	"quickness",
	"gk_shot_stopping",
	"gk_handling",
	"defending",
	"tackling",
	"tactics",
	"ball_control",
	"attacking",
	"shooting",
	"set_pieces",
};

size_t developmentFormat_size(const DevelopmentField *field) {
	switch (field->kind) {
		case DEVELOPMENT_U8:
		case DEVELOPMENT_S8: return 1;
		case DEVELOPMENT_U16:
		case DEVELOPMENT_S16: return 2;
		case DEVELOPMENT_F32: return 4;
		case DEVELOPMENT_RATINGS: return DEVELOPMENT_RATING_COUNT;
		case DEVELOPMENT_CATEGORIES: return DEVELOPMENT_CATEGORY_COUNT;
	}
	return 0;
}

int developmentFormat_value(const DevelopmentField *field, const uint8_t *bytes) {
	uint16_t word = (uint16_t)(bytes[0] | bytes[1] << 8);
	switch (field->kind) {
		case DEVELOPMENT_U8:
		case DEVELOPMENT_RATINGS:
		case DEVELOPMENT_CATEGORIES: return bytes[0];
		case DEVELOPMENT_S8: return (int8_t)bytes[0];
		case DEVELOPMENT_U16: return field->mask ? word & field->mask : word;
		case DEVELOPMENT_S16: return (int16_t)word;
		case DEVELOPMENT_F32: return 0; // not range-checked
	}
	return 0;
}

const char *developmentFormat_ratingName(size_t index) {
	return index < DEVELOPMENT_RATING_COUNT ? ratingNames[index] : "?";
}

const char *developmentFormat_categoryName(size_t index) {
	return index < DEVELOPMENT_CATEGORY_COUNT ? categoryNames[index] : "?";
}

void developmentFormat_writeField(
	FILE *file,
	const DevelopmentField *field,
	const uint8_t *bytes,
	const char *heading
) {
	static const char *const keys[] = {"u8", "u16", "s8", "s16", "u8map", "hex", "u8map"};
	fprintf(file, ",\n%s", heading ? heading : "");
	if (field->note) {
		fprintf(file, "    // %s\n", field->note);
	}
	fprintf(file, "    {\"name\": \"%s\", \"%s\": ", field->name, keys[field->kind]);
	if (field->kind == DEVELOPMENT_RATINGS || field->kind == DEVELOPMENT_CATEGORIES) {
		bool ratings = field->kind == DEVELOPMENT_RATINGS;
		fprintf(file, "{");
		for (size_t k = 0; k < developmentFormat_size(field); ++k) {
			const char *key = ratings ? ratingNames[k] : categoryNames[k];
			fprintf(file, "%s\"%s\": %u", k ? ", " : "", key, bytes[k]);
		}
		fprintf(file, "}}");
	} else if (field->kind == DEVELOPMENT_F32) {
		fprintf(file, "\"%02x%02x%02x%02x\"}", bytes[0], bytes[1], bytes[2], bytes[3]);
	} else {
		fprintf(file, "%d}", developmentFormat_value(field, bytes));
	}
}

// Session names are plain ASCII (no quotes or backslashes), so they need no JSON escaping.
void developmentFormat_writeSchedule(FILE *file, const uint16_t *sessions) {
	fprintf(
		file,
		",\n    // sessions by name as in the game (or ID); add schedule.week2 etc. for a rotation, or"
		" remove to train on the club's schedules\n    {\"name\": \"schedule.week1\", \"sessions\": {"
	);
	for (size_t i = 0; i < DEVELOPMENT_SCHEDULE_SESSIONS; ++i) {
		size_t slot = i % 3;
		const char *separator = slot ? ", " : i ? ",\n      " : "\n      ";
		fprintf(file, "%s\"day%zu.slot%zu\": ", separator, i / 3 + 1, slot + 1);
		const char *session = session_name(sessions[i]);
		if (session) {
			fprintf(file, "\"%s\"", session);
		} else {
			fprintf(file, "%u", sessions[i]); // a session the harness has no name for
		}
	}
	fprintf(file, "\n    }}");
}
