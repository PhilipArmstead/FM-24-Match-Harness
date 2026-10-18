// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Job "get_player": a player's database values, as the fields a match config's player block copies
// from the database (docs/FIXTURE.md, "Values copied from the game's database"). The output is an
// "fm24-player/1" JSON file whose fields are named by their suffix in a player block
// ("ratings", "condition", ...), for `fm24ctl inject-player`. Nothing in the game is changed.

#include "config_writer.h"
#include "database.h"
#include "fixture.h"
#include "game.h"
#include "job.h"
#include "labels.h"
#include "log.h"
#include "memory.h"

#include <stdio.h>
#include <string.h>


enum { PERSONALITY_COUNT = 7, DAY_MASK = 0x1FF };

typedef struct PlayerValues {
	uint32_t uniqueId;
	uint8_t ratings[PLAYER_RATING_COUNT];
	uint16_t sharpness;
	uint16_t fatigue;
	uint16_t condition;
	uint16_t reputations[3]; // home, current, world
	uint16_t currentAbility;
	uint8_t morale;
	uint32_t lastTransferFee;
	uint8_t preferredPosition;
	uint64_t traits;
	uint32_t nationality;
	uint8_t personality[PERSONALITY_COUNT]; // database order (PERSON_PERSONALITY)
	uint8_t age;
	uint16_t height;
	uint8_t weight;
} PlayerValues;

static bool readValues(const uint8_t *person, const uint8_t *player, PlayerValues *out);
static uint8_t ageOn(const uint8_t *person);
static void writeField(FILE *file, const char *name, const char *key, uint64_t value);
static bool writePlayer(const wchar_t *path, const PlayerValues *values, const char *name);

void jobs_getPlayer(const Job *job) {
	PeopleIndex people;
	if (!database_loadPeople(&people)) {
		job_writeResult("error: cannot read the people list; is a save loaded?");
		return;
	}
	const uint8_t *person = database_findPerson(&people, job->personId);
	const uint8_t *player = person ? database_playerRecord(person) : NULL;
	char name[PLAYER_NAME_LENGTH];
	PlayerValues values;
	if (!person) {
		job_writeResult("error: no person with unique ID %u", job->personId);
	} else if (!player) {
		job_writeResult("error: person %u is not a player", job->personId);
	} else if (!readValues(person, player, &values)) {
		job_writeResult("error: cannot read player %u's database values", job->personId);
	} else {
		if (!database_personName(person, name, sizeof name)) {
			(void)snprintf(name, sizeof name, "(unnamed, UID %u)", job->personId);
		}
		if (!writePlayer(job->output, &values, name)) {
			job_writeResult("error: cannot write the player file");
		} else {
			log_message("get_player: %s (%u)", name, job->personId);
			job_writeResult("ok: %s (%u)", name, job->personId);
		}
	}
	database_freePeople(&people);
}

static bool readValues(const uint8_t *person, const uint8_t *player, PlayerValues *out) {
	*out = (PlayerValues){0};
	uint8_t *nation = NULL;
	bool ok =
		memory_readU32(person, PERSON_UNIQUE_ID, &out->uniqueId) &&
		memory_read(player + PLAYER_RATINGS, out->ratings, sizeof out->ratings) &&
		memory_readU16(player, PLAYER_SHARPNESS, &out->sharpness) &&
		memory_readU16(player, PLAYER_FATIGUE, &out->fatigue) &&
		memory_readU16(player, PLAYER_CONDITION, &out->condition) &&
		memory_read(player + PLAYER_HOME_REPUTATION, out->reputations, sizeof out->reputations) &&
		memory_readU16(player, PLAYER_CURRENT_ABILITY, &out->currentAbility) &&
		memory_read(player + PLAYER_MORALE, &out->morale, 1) &&
		memory_readU32(player, PLAYER_LAST_TRANSFER_FEE, &out->lastTransferFee) &&
		memory_read(player + PLAYER_PREFERRED_POSITION, &out->preferredPosition, 1) &&
		memory_read(person + PERSON_TRAITS, &out->traits, sizeof out->traits) &&
		memory_readPointer(person, PERSON_NATION, &nation) &&
		memory_read(person + PERSON_PERSONALITY, out->personality, PERSONALITY_COUNT) &&
		memory_readU16(player, PLAYER_HEIGHT, &out->height) &&
		memory_read(player + PLAYER_WEIGHT, &out->weight, 1);
	if (ok && nation) {
		ok = memory_readU32(nation, NATION_UNIQUE_ID, &out->nationality);
	}
	out->age = ageOn(person);
	return ok;
}

// The person's age in years on the human manager's next match day (as a match built now would
// have it), or on the current date if there is no next fixture.
static uint8_t ageOn(const uint8_t *person) {
	uint16_t birthDay = 0;
	uint16_t birthYear = 0;
	if (!memory_readU16(person, PERSON_DATE_OF_BIRTH, &birthDay) ||
			!memory_readU16(person, PERSON_DATE_OF_BIRTH + 2, &birthYear)) {
		return 0;
	}
	NextFixture next;
	const char *error = NULL;
	const uint8_t *date =
		fixture_findNext(&next, &error) ? next.fixture + FIXTURE_DATE : game_address(RVA_CURRENT_DATE);
	uint16_t day = 0;
	uint16_t year = 0;
	if (!memory_readU16(date, 0, &day) || !memory_readU16(date, 2, &year) || year < birthYear) {
		return 0;
	}
	int age = year - birthYear - ((day & DAY_MASK) < (birthDay & DAY_MASK));
	return (uint8_t)(age < 0 ? 0 : age > UINT8_MAX ? UINT8_MAX : age);
}

// One more scalar field (after the ratings, so always preceded by a comma).
static void writeField(FILE *file, const char *name, const char *key, uint64_t value) {
	fprintf(file, ",\n    {\"name\": \"%s\", \"%s\": %llu}", name, key, (unsigned long long)value);
}

static bool writePlayer(const wchar_t *path, const PlayerValues *values, const char *name) {
	static const char *const personalityNames[PERSONALITY_COUNT] = {
		"personality.adaptability",
		"personality.ambition",
		"personality.loyalty",
		"personality.pressure",
		"personality.professionalism",
		"personality.sportsmanship",
		"personality.temperament",
	};
	FILE *file = _wfopen(path, L"wb");
	if (!file) {
		return false;
	}
	fprintf(file, "{\n  \"format\": \"fm24-player/1\",\n  \"source\": ");
	configWriter_writeString(file, name);
	fprintf(file, ",\n  \"fields\": [\n    {\"name\": \"person_uid\", \"player\": ");
	configWriter_writeString(file, name);
	fprintf(file, ", \"u32\": %u},\n    {\"name\": \"ratings\", \"u8map\": {", values->uniqueId);
	for (size_t k = 0; k < PLAYER_RATING_COUNT; ++k) {
		fprintf(file, "%s\"%s\": %u", k ? ", " : "", labels_ratingName(k), values->ratings[k]);
	}
	fprintf(file, "}}");
	writeField(file, "condition", "u16", values->condition);
	writeField(file, "sharpness[0]", "u16", values->sharpness);
	writeField(file, "sharpness[1]", "u16", values->sharpness);
	writeField(file, "fatigue", "u16", values->fatigue);
	writeField(file, "home_reputation", "u16", values->reputations[0]);
	writeField(file, "current_reputation", "u16", values->reputations[1]);
	writeField(file, "world_reputation", "u16", values->reputations[2]);
	writeField(file, "current_ability", "u16", values->currentAbility);
	writeField(file, "morale", "u8", values->morale);
	writeField(file, "last_transfer_fee", "u32", values->lastTransferFee);
	writeField(file, "preferred_position", "u8", values->preferredPosition);
	writeField(file, "traits", "u64", values->traits);
	writeField(file, "nationality", "u32", values->nationality);
	for (size_t k = 0; k < PERSONALITY_COUNT; ++k) {
		writeField(file, personalityNames[k], "u8", values->personality[k]);
	}
	writeField(file, "age", "u8", values->age);
	// The match stores height in metres as a float (docs/FIXTURE.md); the field holds its bits.
	float metres = (float)values->height / 100.0F;
	uint32_t heightBits = 0;
	memcpy(&heightBits, &metres, sizeof heightBits);
	writeField(file, "height", "u32", heightBits);
	writeField(file, "weight", "u8", values->weight);
	fprintf(file, "\n  ]\n}\n");
	bool ok = !ferror(file);
	return fclose(file) == 0 && ok;
}
