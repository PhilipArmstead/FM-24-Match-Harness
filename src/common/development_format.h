// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// The "fm24-development/1" file's fields: names, value kinds, legal ranges and the comments written
// above them. The DLL (`get-development`, `develop`) and the viewer's Training tab both write and
// check the file from this one table; the DLL adds where each field lives in the game.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>


#define DEVELOPMENT_FORMAT "fm24-development/1"

enum {
	DEVELOPMENT_RATING_COUNT = 69,   // 15 positions, then 54 attributes (database order)
	DEVELOPMENT_POSITION_COUNT = 15, // rated 1-20; the attributes 1-100
	DEVELOPMENT_POSITION_MAXIMUM = 20,
	DEVELOPMENT_CATEGORY_COUNT = 11,    // training categories, strength ... set_pieces
	DEVELOPMENT_SCHEDULE_SESSIONS = 21, // 7 days of 3 slots
};

// DEVELOPMENT_RATINGS and DEVELOPMENT_CATEGORIES are u8 maps: 69 ratings, or one value per training
// category.
typedef enum DevelopmentKind {
	DEVELOPMENT_U8,
	DEVELOPMENT_U16,
	DEVELOPMENT_S8,
	DEVELOPMENT_S16,
	DEVELOPMENT_RATINGS,
	DEVELOPMENT_F32,
	DEVELOPMENT_CATEGORIES,
} DevelopmentKind;

// The table's fields, in file order (internal ones last when written).
typedef enum DevelopmentFieldId {
	DEVELOPMENT_CURRENT_ABILITY,
	DEVELOPMENT_POTENTIAL_ABILITY,
	DEVELOPMENT_RATINGS_FIELD,
	DEVELOPMENT_PROGRESS_STRENGTH,
	DEVELOPMENT_PROGRESS_QUICKNESS,
	DEVELOPMENT_PROGRESS_GK_SHOT_STOPPING,
	DEVELOPMENT_PROGRESS_GK_HANDLING,
	DEVELOPMENT_PROGRESS_DEFENDING,
	DEVELOPMENT_PROGRESS_TACKLING,
	DEVELOPMENT_PROGRESS_TACTICS,
	DEVELOPMENT_PROGRESS_BALL_CONTROL,
	DEVELOPMENT_PROGRESS_ATTACKING,
	DEVELOPMENT_PROGRESS_SHOOTING,
	DEVELOPMENT_PROGRESS_SET_PIECES,
	DEVELOPMENT_TRAINING_INTENSITY,
	DEVELOPMENT_TRAINING_FOCUS,
	DEVELOPMENT_CONDITION,
	DEVELOPMENT_FATIGUE,
	DEVELOPMENT_SHARPNESS,
	DEVELOPMENT_MORALE,
	DEVELOPMENT_HEIGHT,
	DEVELOPMENT_WEIGHT,
	DEVELOPMENT_ADAPTABILITY,
	DEVELOPMENT_AMBITION,
	DEVELOPMENT_LOYALTY,
	DEVELOPMENT_PRESSURE,
	DEVELOPMENT_PROFESSIONALISM,
	DEVELOPMENT_SPORTSMANSHIP,
	DEVELOPMENT_TEMPERAMENT,
	DEVELOPMENT_CONTROVERSY,
	DEVELOPMENT_BIRTH_DAY,
	DEVELOPMENT_BIRTH_YEAR,
	DEVELOPMENT_TRAINING_HAPPINESS,
	DEVELOPMENT_HAPPINESS_CHANGE,
	DEVELOPMENT_LAST_GAINS,
	DEVELOPMENT_LAST_OTHER_GAINS,
	DEVELOPMENT_WEAK_FOOT_PROGRESS,
	DEVELOPMENT_PERIOD_TOTAL,
	DEVELOPMENT_PERIOD_LAST_TOTAL,
	DEVELOPMENT_PERIOD_LAST_COUNT,
	DEVELOPMENT_PERIOD_COUNT,
	DEVELOPMENT_IMPORTANT_MATCHES_PROGRESS,
	DEVELOPMENT_DETERMINATION_PROGRESS,
	DEVELOPMENT_PRESSURE_PROGRESS,
	DEVELOPMENT_PROFESSIONALISM_PROGRESS,
	DEVELOPMENT_PLAYED_LEVEL,
	DEVELOPMENT_PLAYED_APPEARANCES,
	DEVELOPMENT_FLAGS,
	DEVELOPMENT_TRAINING_RATING,
	DEVELOPMENT_ABILITY_CARRY,
	DEVELOPMENT_COACHING,
	DEVELOPMENT_COACH_WORKLOAD,
	DEVELOPMENT_TRAINING_FACILITIES,
	DEVELOPMENT_FIELD_COUNT,
} DevelopmentFieldId;

typedef struct DevelopmentField {
	const char *name;
	DevelopmentKind kind;
	int minimum; // per value; each byte for the maps
	int maximum;
	uint16_t mask;    // the bits of a u16 the field owns, 0 for all of it
	bool internal;    // state the game computes: written last, under "here be dragons"
	const char *note; // written as a comment above the field, or NULL
} DevelopmentField;

extern const DevelopmentField developmentFields[DEVELOPMENT_FIELD_COUNT];

// The comment lines that open every file, after its "{" line.
extern const char developmentFormat_header[];

// Written once before the first internal field.
extern const char developmentFormat_dragons[];

// The field's size in bytes.
size_t developmentFormat_size(const DevelopmentField *field);

// One value of the field (one byte of a map) as the file shows it.
int developmentFormat_value(const DevelopmentField *field, const uint8_t *bytes);

// The ratings map's keys (database order) and the categories map's keys, or "?" past the end.
const char *developmentFormat_ratingName(size_t index);
const char *developmentFormat_categoryName(size_t index);

// The field (its `size` bytes) as ",\n" + `heading` (unless NULL) + its note + the entry.
void developmentFormat_writeField(
	FILE *file,
	const DevelopmentField *field,
	const uint8_t *bytes,
	const char *heading
);

// DEVELOPMENT_SCHEDULE_SESSIONS session IDs as ",\n" + schedule.week1 by session name (the ID
// for a session without one).
void developmentFormat_writeSchedule(FILE *file, const uint16_t *sessions);
