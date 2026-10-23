// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "development_file.h"

#include "config_writer.h"
#include "game.h"
#include "labels.h"
#include "memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef enum Base {
	ON_PLAYER,    // offset from the player record
	ON_PERSON,    // offset from the person inside it (PLAYER_PERSON)
	ON_HAPPINESS, // offset in the training happiness relationship record
} Base;

typedef enum Kind { KIND_U8, KIND_U16, KIND_S8, KIND_S16, KIND_RATINGS } Kind;

typedef struct Field {
	const char *name;
	Kind kind;
	Base base;
	size_t offset;
	int minimum; // per value; each byte for KIND_RATINGS
	int maximum;
	uint16_t mask; // the bits of a u16 the field owns, 0 for all of it
} Field;

// Everything the development routine is known to read that is plain data (no pointers).
static const Field fields[] = {
	{"current_ability", KIND_U16, ON_PLAYER, PLAYER_CURRENT_ABILITY, 1, 200, 0},
	{"potential_ability", KIND_U16, ON_PLAYER, PLAYER_POTENTIAL_ABILITY, 1, 200, 0},
	{"ratings", KIND_RATINGS, ON_PLAYER, PLAYER_RATINGS, 1, 100, 0},
	{"training_progress.strength", KIND_S8, ON_PLAYER, PLAYER_TRAINING_PROGRESS + 0, -100, 50, 0},
	{"training_progress.quickness", KIND_S8, ON_PLAYER, PLAYER_TRAINING_PROGRESS + 1, -100, 50, 0},
	{"training_progress.gk_shot_stopping",
		KIND_S8,
		ON_PLAYER,
		PLAYER_TRAINING_PROGRESS + 2,
		-100,
		50,
		0},
	{"training_progress.gk_handling", KIND_S8, ON_PLAYER, PLAYER_TRAINING_PROGRESS + 3, -100, 50, 0},
	{"training_progress.defending", KIND_S8, ON_PLAYER, PLAYER_TRAINING_PROGRESS + 4, -100, 50, 0},
	{"training_progress.tackling", KIND_S8, ON_PLAYER, PLAYER_TRAINING_PROGRESS + 5, -100, 50, 0},
	{"training_progress.tactics", KIND_S8, ON_PLAYER, PLAYER_TRAINING_PROGRESS + 6, -100, 50, 0},
	{"training_progress.ball_control", KIND_S8, ON_PLAYER, PLAYER_TRAINING_PROGRESS + 7, -100, 50, 0},
	{"training_progress.attacking", KIND_S8, ON_PLAYER, PLAYER_TRAINING_PROGRESS + 8, -100, 50, 0},
	{"training_progress.shooting", KIND_S8, ON_PLAYER, PLAYER_TRAINING_PROGRESS + 9, -100, 50, 0},
	{"training_progress.set_pieces", KIND_S8, ON_PLAYER, PLAYER_TRAINING_PROGRESS + 10, -100, 50, 0},
	// 0 or 1 normal, 2 double, 254 half, 255 the team's workload (knowledge-base section 6)
	{"training_intensity", KIND_U8, ON_PLAYER, PLAYER_TRAINING_INTENSITY, 0, 255, 0},
	{"condition", KIND_U16, ON_PLAYER, PLAYER_CONDITION, 0, 10000, 0},
	{"fatigue", KIND_S16, ON_PLAYER, PLAYER_FATIGUE, -32768, 32767, 0},
	{"sharpness", KIND_U16, ON_PLAYER, PLAYER_SHARPNESS, 0, 10000, 0},
	{"morale", KIND_U8, ON_PLAYER, PLAYER_MORALE, 0, 255, 0},
	{"height", KIND_U16, ON_PLAYER, PLAYER_HEIGHT, 100, 230, 0},
	{"weight", KIND_U8, ON_PLAYER, PLAYER_WEIGHT, 30, 150, 0},
	{"personality.adaptability", KIND_U8, ON_PERSON, PERSON_PERSONALITY + 0, 1, 20, 0},
	{"personality.ambition", KIND_U8, ON_PERSON, PERSON_PERSONALITY + 1, 1, 20, 0},
	{"personality.loyalty", KIND_U8, ON_PERSON, PERSON_PERSONALITY + 2, 1, 20, 0},
	{"personality.pressure", KIND_U8, ON_PERSON, PERSON_PERSONALITY + 3, 1, 20, 0},
	{"personality.professionalism", KIND_U8, ON_PERSON, PERSON_PERSONALITY + 4, 1, 20, 0},
	{"personality.sportsmanship", KIND_U8, ON_PERSON, PERSON_PERSONALITY + 5, 1, 20, 0},
	{"personality.temperament", KIND_U8, ON_PERSON, PERSON_PERSONALITY + 6, 1, 20, 0},
	{"personality.controversy", KIND_U8, ON_PERSON, PERSON_PERSONALITY + 7, 1, 20, 0},
	// The day of the year counts from 1; the u16's other bits are kept.
	{"date_of_birth.day", KIND_U16, ON_PERSON, PERSON_DATE_OF_BIRTH, 1, 366, 0x1FF},
	{"date_of_birth.year", KIND_U16, ON_PERSON, PERSON_DATE_OF_BIRTH + 2, 1900, 2100, 0},
	{"training_happiness", KIND_U8, ON_HAPPINESS, 0x0C, 0, 100, 0},
	// Changes at the weekly training review; meaning unknown (knowledge-base section 13).
	{"training_happiness.unknown_08", KIND_U16, ON_HAPPINESS, 0x08, 0, 65535, 0},
};

enum { FIELD_COUNT = sizeof fields / sizeof fields[0] };

static size_t sizeOf(const Field *field) {
	switch (field->kind) {
		case KIND_U8:
		case KIND_S8: return 1;
		case KIND_U16:
		case KIND_S16: return 2;
		case KIND_RATINGS: return PLAYER_RATING_COUNT;
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
	}
	return NULL;
}

static int valueAt(const Field *field, const uint8_t *bytes) {
	uint16_t word = memory_loadU16(bytes);
	switch (field->kind) {
		case KIND_U8:
		case KIND_RATINGS: return bytes[0];
		case KIND_S8: return (int8_t)bytes[0];
		case KIND_U16: return field->mask ? word & field->mask : word;
		case KIND_S16: return (int16_t)word;
	}
	return 0;
}

static const char *keyOf(const Field *field) {
	static const char *const keys[] = {"u8", "u16", "s8", "s16", "u8map"};
	return keys[field->kind];
}

bool developmentFile_write(
	const wchar_t *path,
	uint8_t *player,
	uint32_t uniqueId,
	const char *name
) {
	FILE *file = _wfopen(path, L"wb");
	if (!file) {
		return false;
	}
	fprintf(file, "{\n  \"format\": \"fm24-development/1\",\n  \"source\": ");
	configWriter_writeString(file, name);
	fprintf(file, ",\n  \"fields\": [\n    {\"name\": \"person_uid\", \"player\": ");
	configWriter_writeString(file, name);
	fprintf(file, ", \"u32\": %u}", uniqueId);
	for (size_t i = 0; i < FIELD_COUNT; ++i) {
		const Field *field = &fields[i];
		const uint8_t *at = locate(player, field);
		if (!at) {
			continue; // no training happiness record
		}
		fprintf(file, ",\n    {\"name\": \"%s\", \"%s\": ", field->name, keyOf(field));
		if (field->kind == KIND_RATINGS) {
			fprintf(file, "{");
			for (size_t k = 0; k < PLAYER_RATING_COUNT; ++k) {
				fprintf(file, "%s\"%s\": %u", k ? ", " : "", labels_ratingName(k), at[k]);
			}
			fprintf(file, "}}");
		} else {
			fprintf(file, "%d}", valueAt(field, at));
		}
	}
	fprintf(file, "\n  ]\n}\n");
	bool ok = !ferror(file);
	return fclose(file) == 0 && ok;
}

static const Field *findField(const char *name, size_t length) {
	for (size_t i = 0; i < FIELD_COUNT; ++i) {
		if (strlen(fields[i].name) == length && memcmp(fields[i].name, name, length) == 0) {
			return &fields[i];
		}
	}
	return NULL;
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
	size_t values = field->kind == KIND_RATINGS ? size : 1;
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
		if (!locate(player, field)) {
			(void)snprintf(error, errorSize, "the player has no %s record", field->name);
			return false;
		}
	}
	return true;
}

void developmentFile_save(uint8_t *player, DevelopmentSnapshot *out) {
	size_t used = 0;
	out->hasHappiness = happinessRecord(player) != NULL;
	for (size_t i = 0; i < FIELD_COUNT; ++i) {
		const uint8_t *at = locate(player, &fields[i]);
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
		if (at && (fields[i].base != ON_HAPPINESS || snapshot->hasHappiness)) {
			memcpy(at, snapshot->bytes + used, sizeOf(&fields[i]));
		}
		used += sizeOf(&fields[i]);
	}
}

void developmentFile_apply(uint8_t *player, const DevelopmentValues *values) {
	for (size_t i = 0; i < values->count; ++i) {
		const Field *field = &fields[values->field[i]];
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
