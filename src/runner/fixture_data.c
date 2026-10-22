// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture_data.h"

#include "match_config.h"
#include "tactic_names.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef enum Section {
	SECTION_NONE,
	SECTION_TEAM,
	SECTION_PLAN,
	SECTION_PLAYER,
	SECTION_FIXTURE,
	SECTION_VENUE,
	SECTION_OFFICIAL
} Section;

typedef struct ValueSpan {
	size_t offset;
	size_t length;
} ValueSpan;

// Where a player's editable values are in the JSON: the five wellbeing values, then each
// familiarity category's first entry (setting, amount).
typedef struct PlayerSpans {
	ValueSpan wellbeing[5];
	ValueSpan familiarity[FIXTURE_FAMILIARITY_CATEGORIES][2];
} PlayerSpans;

// A player's familiarity categories as labels.c names them, in file order.
static const char *const familiarityNames[FIXTURE_FAMILIARITY_CATEGORIES] = {
	"mentality",
	"passing_style",
	"creative_freedom",
	"trigger_press",
	"marking",
	"tempo",
	"width",
};

typedef struct Reader {
	FixtureData *out;
	Section section;
	int side;
	unsigned player;
	unsigned official;
	unsigned strings;
	unsigned fitnessIndex;
	bool fitnessValid;
	FixtureNumber fitness[14];
	ValueSpan fitnessSpans[14];
	PlayerSpans (*playerSpans)[FIXTURE_PLAYERS];
	FixtureNumber previousU8; // the previous field's value, if it was a u8
	char firstName[FIXTURE_TEXT];
	bool noted; // the player block's "player" note named it, so its strings don't
	char error[192];
} Reader;

static const struct {
	const char *name;
	ConfigValueKind kind;
} venueFields[VENUE_FIELD_COUNT] = {
	{"venue.pitch.width", CONFIG_U32},
	{"venue", CONFIG_U32},
	{"venue.city_longitude", CONFIG_U32},
	{"venue.city_latitude", CONFIG_U32},
	{"venue.capacity", CONFIG_U32},
	{"venue.seating_capacity", CONFIG_U32},
	{"venue.home_fans", CONFIG_U32},
	{"venue.away_fans", CONFIG_U32},
	{"venue.season_ticket_holders", CONFIG_U32},
	{"venue.surface", CONFIG_U8},
	{"venue.pitch_condition", CONFIG_U16},
};

float fixtureData_toFloat(uint32_t bits) {
	float value;
	_Static_assert(sizeof value == sizeof bits, "fixture floats must be 32-bit");
	memcpy(&value, &bits, sizeof value);
	return value;
}

uint32_t fixtureData_fromFloat(float value) {
	uint32_t bits;
	memcpy(&bits, &value, sizeof bits);
	return bits;
}

static void copyText(Reader *reader, char *out, size_t size, const char *text, size_t length) {
	if (length >= size || memchr(text, 0, length)) {
		(void)snprintf(reader->error, sizeof reader->error, "fixture text is too long or contains NUL");
		return;
	}
	memcpy(out, text, length);
	out[length] = 0;
}

static bool scalar(const ConfigField *field, ConfigValueKind kind, FixtureNumber *out) {
	if (field->kind != kind) {
		return false;
	}
	uint64_t value = 0;
	for (size_t i = 0; i < field->length; ++i) {
		value |= (uint64_t)field->bytes[i] << (8 * i);
	}
	*out = (FixtureNumber){.value = value, .known = true};
	return true;
}

static bool indexed(const char *text, const char *prefix, unsigned *index, const char **suffix) {
	size_t length = strlen(prefix);
	if (strncmp(text, prefix, length) != 0 || text[length] < '0' || text[length] > '9') {
		return false;
	}
	char *end = NULL;
	unsigned long value = strtoul(text + length, &end, 10);
	if (*end != ']' || value >= FIXTURE_PLAYERS) {
		return false;
	}
	*index = (unsigned)value;
	*suffix = end + 1;
	return true;
}

static void setSection(Reader *reader, Section section) {
	reader->section = section;
	reader->strings = 0;
	reader->fitnessIndex = 0;
	reader->fitnessValid = false;
	reader->firstName[0] = 0;
	reader->noted = false;
}

static void teamField(Reader *reader, const ConfigField *field, const char *name) {
	static const char planPrefix[] = "manager.tactic.plan[0].";
	const size_t prefixLength = sizeof planPrefix - 1;
	FixtureTeam *team = &reader->out->teams[reader->side];
	unsigned index = 0;
	const char *suffix = NULL;
	if (indexed(name, "player[", &index, &suffix)) {
		if (!*suffix) {
			setSection(reader, SECTION_PLAYER);
			reader->player = index;
			team->players[index].present = true;
		}
		return;
	}
	if (strcmp(name, "team") == 0) {
		setSection(reader, SECTION_TEAM);
	} else if (strcmp(name, "manager.tactic.plan[0]") == 0) {
		setSection(reader, SECTION_PLAN);
	} else if (strncmp(name, planPrefix, prefixLength) == 0) {
		const char *planField = name + prefixLength;
		if (indexed(planField, "slot[", &index, &suffix) && index < FIXTURE_SLOTS) {
			if (strcmp(suffix, ".position") == 0) {
				(void)scalar(field, CONFIG_U32, &team->slots[index].position);
			} else if (strcmp(suffix, ".role") == 0) {
				(void)scalar(field, CONFIG_U64, &team->slots[index].role);
			}
		} else if (strcmp(planField, "mentality") == 0) {
			// The unnamed byte before it is the plan's +0x18 (fn_11ee0e10+0x18 in a --full export).
			if (scalar(field, CONFIG_U8, &team->mentality)) {
				team->planByte18 = reader->previousU8;
			}
		} else if (indexed(planField, "team_instructions[", &index, &suffix) && index < 3 && !*suffix) {
			(void)scalar(field, CONFIG_U32, &team->instructions[index]);
		}
	} else {
		setSection(reader, SECTION_NONE);
		if (strcmp(name, "manager") == 0 && !team->managerFlags.known) {
			(void)scalar(field, CONFIG_U32, &team->managerFlags);
		} else if (strcmp(name, "player_count") == 0) {
			(void)scalar(field, CONFIG_U8, &team->playerCount);
		}
	}
}

static void namedField(Reader *reader, const ConfigField *field) {
	const char *name = field->name;
	if (strcmp(name, "venue") == 0 || strncmp(name, "venue.", 6) == 0) {
		for (unsigned i = 0; i < VENUE_FIELD_COUNT; ++i) {
			if (strcmp(name, venueFields[i].name) == 0 &&
					!scalar(field, venueFields[i].kind, &reader->out->venueValues[i])) {
				(void)snprintf(reader->error, sizeof reader->error, "wrong type for %s", name);
			}
		}
	}
	if (strncmp(name, "home.", 5) == 0 || strncmp(name, "away.", 5) == 0) {
		reader->side = name[0] == 'h' ? 0 : 1;
		teamField(reader, field, name + 5);
		return;
	}
	unsigned index = 0;
	const char *suffix = NULL;
	if (strcmp(name, "fixture") == 0) {
		setSection(reader, SECTION_FIXTURE);
	} else if (strcmp(name, "venue") == 0) {
		setSection(reader, SECTION_VENUE);
	} else if (strcmp(name, "referee") == 0 ||
						 (indexed(name, "official[", &index, &suffix) && !*suffix && index < 4)) {
		setSection(reader, SECTION_OFFICIAL);
		reader->official = strcmp(name, "referee") == 0 ? 4 : index;
	} else if (strncmp(name, "referee.", 8) != 0 && strncmp(name, "official[", 9) != 0 &&
						 strncmp(name, "venue.", 6) != 0) {
		setSection(reader, SECTION_NONE);
	}
}

// Checks one venue value on its own: its range and, for the floats, that it is a usable number.
static bool validVenueValue(
	const FixtureNumber values[VENUE_FIELD_COUNT],
	unsigned i,
	char *error,
	size_t errorSize
) {
	uint64_t maximum = i == VENUE_SURFACE ? UINT8_MAX : i == VENUE_CONDITION ? 255 : UINT32_MAX;
	if (values[i].value > maximum) {
		(void)snprintf(error, errorSize, "%s is out of range.", venueFields[i].name);
		return false;
	}
	if (i > VENUE_LATITUDE) {
		return true;
	}
	float value = fixtureData_toFloat((uint32_t)values[i].value);
	float limit = i == VENUE_LONGITUDE ? 180.0f : 90.0f;
	if (isfinite(value) && (i <= VENUE_LENGTH ? value > 0 : fabsf(value) <= limit)) {
		return true;
	}
	(void)snprintf(
		error,
		errorSize,
		"Invalid %s: enter a finite %s.",
		venueFields[i].name,
		i <= VENUE_LENGTH ? "positive pitch dimension" : "coordinate within range"
	);
	return false;
}

bool fixtureData_validateVenue(
	const FixtureNumber values[VENUE_FIELD_COUNT],
	char *error,
	size_t errorSize
) {
	for (unsigned i = 0; i < VENUE_FIELD_COUNT; ++i) {
		if (values[i].known && !validVenueValue(values, i, error, errorSize)) {
			return false;
		}
	}
	if (values[VENUE_SURFACE].known && values[VENUE_SURFACE].value != 1 &&
			values[VENUE_SURFACE].value != 8) {
		(void)snprintf(error, errorSize, "Select Grass or Grass/Synthetic mix.");
		return false;
	}
	uint64_t fans = (values[VENUE_HOME_FANS].known ? values[VENUE_HOME_FANS].value : 0) +
									(values[VENUE_AWAY_FANS].known ? values[VENUE_AWAY_FANS].value : 0);
	if (fans > UINT32_MAX || (values[VENUE_CAPACITY].known && fans > values[VENUE_CAPACITY].value)) {
		(void)snprintf(error, errorSize, "Home fans + Away fans cannot exceed Capacity.");
		return false;
	}
	if (values[VENUE_CAPACITY].known && values[VENUE_SEATS].known &&
			values[VENUE_SEATS].value > values[VENUE_CAPACITY].value) {
		(void)snprintf(error, errorSize, "Seat Capacity cannot exceed Capacity.");
		return false;
	}
	return true;
}

typedef struct VenueWriter {
	const char *json;
	const FixtureNumber *values;
	ByteBuffer *out;
	size_t copied;
	unsigned found;
	char error[192];
} VenueWriter;

// The attendance and fan counts the fixture copies from the venue's home and away fans.
static bool attendanceValue(const FixtureNumber *values, const char *name, uint32_t *out) {
	if (values[VENUE_HOME_FANS].known && values[VENUE_AWAY_FANS].known &&
			(strcmp(name, "venue.used_capacity") == 0 || strcmp(name, "venue.attendance") == 0 ||
				strcmp(name, "fixture.attendance") == 0 || strcmp(name, "fixture_extra") == 0)) {
		*out = (uint32_t)(values[VENUE_HOME_FANS].value + values[VENUE_AWAY_FANS].value);
		return true;
	}
	if (values[VENUE_HOME_FANS].known && strcmp(name, "fixture_extra.home_fans") == 0) {
		*out = (uint32_t)values[VENUE_HOME_FANS].value;
		return true;
	}
	if (values[VENUE_AWAY_FANS].known &&
			(strcmp(name, "fixture.away_fans") == 0 || strcmp(name, "fixture_extra.away_fans") == 0)) {
		*out = (uint32_t)values[VENUE_AWAY_FANS].value;
		return true;
	}
	return false;
}

// The pitch's half and quarter width and length.
static bool pitchFractionValue(const FixtureNumber *values, const char *point, uint32_t *out) {
	for (unsigned i = 0; i < 2; ++i) {
		unsigned dimension = i ? VENUE_LENGTH : VENUE_WIDTH;
		if (!values[dimension].known) {
			continue;
		}
		float value = fixtureData_toFloat((uint32_t)values[dimension].value);
		if (strcmp(point, i ? "half_length" : "half_width") == 0) {
			*out = fixtureData_fromFloat(value * 0.5f);
			return true;
		}
		if (strcmp(point, i ? "quarter_length" : "quarter_width") == 0) {
			*out = fixtureData_fromFloat(value * 0.25f);
			return true;
		}
	}
	return false;
}

// One coordinate of landmark p (an index into the landmarks in pitchLandmarkValue) at an end.
static float landmarkCoordinate(const FixtureNumber *values, unsigned p, char axis, bool end) {
	float halfWidth = fixtureData_toFloat((uint32_t)values[VENUE_WIDTH].value) * 0.5f;
	float halfLength = fixtureData_toFloat((uint32_t)values[VENUE_LENGTH].value) * 0.5f;
	if (axis == 'y') {
		return (end ? 1.0f : -1.0f) * (halfLength - (p == 5 ? 10.97f : 0));
	}
	if (axis != 'x') {
		return 0;
	}
	float result = p == 1 ? 3.66f : p == 2 ? -3.66f : p == 3 ? -halfWidth : p == 4 ? halfWidth : 0;
	// The engine numbers posts and corners in opposite directions at the two ends.
	return end && result != 0 ? -result : result;
}

// A landmark's coordinate: "centre_spot.<axis>" or "end[n].<landmark>.<axis>".
static bool pitchLandmarkValue(const FixtureNumber *values, const char *point, uint32_t *out) {
	if (!values[VENUE_WIDTH].known || !values[VENUE_LENGTH].known) {
		return false;
	}
	static const char *const points[] = {
		"goal", "post[0]", "post[1]", "corner[0]", "corner[1]", "penalty_spot"};
	const char *axis = strrchr(point, '.');
	if (!axis || !axis[1] || axis[2] || !strchr("xyz", axis[1])) {
		return false;
	}
	if ((size_t)(axis - point) == 11 && strncmp(point, "centre_spot", 11) == 0) {
		*out = fixtureData_fromFloat(0);
		return true;
	}
	bool end = strncmp(point, "end[1].", 7) == 0;
	if (!end && strncmp(point, "end[0].", 7) != 0) {
		return false;
	}
	point += 7;
	for (unsigned p = 0; p < 6; ++p) {
		size_t length = strlen(points[p]);
		if (axis - point == (ptrdiff_t)length && strncmp(point, points[p], length) == 0) {
			*out = fixtureData_fromFloat(landmarkCoordinate(values, p, axis[1], end));
			return true;
		}
	}
	return false;
}

static bool derivedVenueValue(const VenueWriter *writer, const char *name, uint32_t *out) {
	if (attendanceValue(writer->values, name, out)) {
		return true;
	}
	if (strncmp(name, "venue.pitch.", 12) != 0) {
		return false;
	}
	return pitchFractionValue(writer->values, name + 12, out) ||
				 pitchLandmarkValue(writer->values, name + 12, out);
}

static void writeVenueField(void *context, const ConfigField *field) {
	VenueWriter *writer = context;
	if (writer->error[0] || (field->name[0] != 'v' && field->name[0] != 'f')) {
		return;
	}
	uint32_t value = 0;
	ConfigValueKind kind = CONFIG_U32;
	bool found = false;
	for (unsigned i = 0; i < VENUE_FIELD_COUNT; ++i) {
		if (writer->values[i].known && strcmp(field->name, venueFields[i].name) == 0) {
			value = (uint32_t)writer->values[i].value;
			kind = venueFields[i].kind;
			writer->found |= 1U << i;
			found = true;
			break;
		}
	}
	if (!found && !derivedVenueValue(writer, field->name, &value)) {
		return;
	}
	if (field->kind != kind) {
		(void)snprintf(writer->error, sizeof writer->error, "Wrong type for %s.", field->name);
		return;
	}
	FixtureNumber current;
	(void)scalar(field, kind, &current);
	if (current.value == value) {
		return;
	}
	char number[16];
	int length = snprintf(number, sizeof number, "%u", value);
	if (!buffer_append(
				writer->out, writer->json + writer->copied, field->valueOffset - writer->copied
			) ||
			!buffer_append(writer->out, number, (size_t)length)) {
		(void)snprintf(writer->error, sizeof writer->error, "Out of memory editing the venue.");
	}
	writer->copied = field->valueOffset + field->valueLength;
}

bool fixtureData_writeVenue(
	const char *json,
	size_t length,
	const FixtureNumber values[VENUE_FIELD_COUNT],
	ByteBuffer *out,
	char *error,
	size_t errorSize
) {
	buffer_clear(out);
	if (!fixtureData_validateVenue(values, error, errorSize)) {
		return false;
	}
	VenueWriter writer = {.json = json, .values = values, .out = out};
	ConfigVisitor visitor = {.context = &writer, .field = writeVenueField};
	bool ok = matchConfig_visit(json, length, &visitor, error, errorSize);
	for (unsigned i = 0; ok && !writer.error[0] && i < VENUE_FIELD_COUNT; ++i) {
		if (values[i].known && !(writer.found & (1U << i))) {
			(void)snprintf(
				writer.error, sizeof writer.error, "Missing %s in the fixture.", venueFields[i].name
			);
		}
	}
	if (ok && writer.error[0]) {
		(void)snprintf(error, errorSize, "%s", writer.error);
		ok = false;
	}
	if (ok && !buffer_append(out, json + writer.copied, length - writer.copied)) {
		(void)snprintf(error, errorSize, "Out of memory editing the venue.");
		ok = false;
	}
	if (!ok) {
		buffer_clear(out);
	}
	return ok;
}

// The familiarity category `*text` starts with (skipping it), or FIXTURE_FAMILIARITY_CATEGORIES.
static unsigned familiarityCategory(const char **text) {
	size_t length = strcspn(*text, ".[");
	for (unsigned category = 0; category < FIXTURE_FAMILIARITY_CATEGORIES; ++category) {
		if (strlen(familiarityNames[category]) == length &&
				strncmp(*text, familiarityNames[category], length) == 0) {
			*text += length;
			return category;
		}
	}
	return FIXTURE_FAMILIARITY_CATEGORIES;
}

// Reads a player's ".tactical_familiarity.<category>.count", "[N].setting" and "[N].amount".
static void familiarityValue(Reader *reader, const ConfigField *field, const char *suffix) {
	static const char prefix[] = ".tactical_familiarity.";
	if (strncmp(suffix, prefix, sizeof prefix - 1) != 0) {
		return;
	}
	const char *rest = suffix + sizeof prefix - 1;
	unsigned category = familiarityCategory(&rest);
	if (category == FIXTURE_FAMILIARITY_CATEGORIES) {
		return;
	}
	FixturePlayer *player = &reader->out->teams[reader->side].players[reader->player];
	FixtureFamiliarity *familiarity = &player->familiarity[category];
	FixtureNumber number = {0};
	if (strcmp(rest, ".count") == 0) {
		if (scalar(field, CONFIG_U32, &number)) {
			familiarity->known = number.value <= FIXTURE_FAMILIARITY_ENTRIES;
			familiarity->count = familiarity->known ? (uint8_t)number.value : 0;
		}
		return;
	}
	char *end = NULL;
	unsigned long entry = rest[0] == '[' ? strtoul(rest + 1, &end, 10) : ULONG_MAX;
	if (!end || end == rest + 1 || *end != ']' || entry >= familiarity->count) {
		return;
	}
	bool setting = strcmp(end + 1, ".setting") == 0;
	if (!setting && strcmp(end + 1, ".amount") != 0) {
		return;
	}
	if (!scalar(field, setting ? CONFIG_U8 : CONFIG_U32, &number) ||
			(!setting && number.value > 10000)) {
		familiarity->known = false;
		return;
	}
	if (setting) {
		familiarity->setting[entry] = (int8_t)(uint8_t)number.value;
	} else {
		familiarity->amount[entry] = (uint16_t)number.value;
	}
	if (entry == 0 && reader->playerSpans) {
		reader->playerSpans[reader->side][reader->player].familiarity[category][setting ? 0 : 1] =
			(ValueSpan){field->valueOffset, field->valueLength};
	}
}

static void playerValue(Reader *reader, const ConfigField *field) {
	FixturePlayer *player = &reader->out->teams[reader->side].players[reader->player];
	const char *suffix = strchr(field->name, ']');
	suffix = suffix ? suffix + 1 : "";
	if (*suffix) {
		familiarityValue(reader, field, suffix);
	}
	static const struct {
		const char *name;
		ConfigValueKind kind;
	} values[] = {
		{".condition", CONFIG_U16},
		{".sharpness[0]", CONFIG_U16},
		{".sharpness[1]", CONFIG_U16},
		{".fatigue", CONFIG_U16},
		{".morale", CONFIG_U8},
	};
	FixtureNumber *numbers[] = {
		&player->condition,
		&player->sharpness[0],
		&player->sharpness[1],
		&player->fatigue,
		&player->morale,
	};
	for (size_t i = 0; i < sizeof values / sizeof values[0]; ++i) {
		if (strcmp(suffix, values[i].name) == 0) {
			if (!scalar(field, values[i].kind, numbers[i])) {
				(void)snprintf(reader->error, sizeof reader->error, "wrong type for %s", field->name);
			} else if (reader->playerSpans) {
				reader->playerSpans[reader->side][reader->player].wellbeing[i] =
					(ValueSpan){field->valueOffset, field->valueLength};
			}
		}
	}
	if (field->kind == CONFIG_RATINGS) {
		reader->fitnessIndex = 0;
		reader->fitnessValid = field->length == 69;
		return;
	}
	// Older exports lack wellbeing labels. Accept only the complete, typed sequence following
	// ratings; never use absolute field numbers (player blocks have variable lengths).
	static const ConfigValueKind kinds[14] = {
		CONFIG_U16,
		CONFIG_U16,
		CONFIG_U16,
		CONFIG_U8,
		CONFIG_U8,
		CONFIG_U8,
		CONFIG_U16,
		CONFIG_U8,
		CONFIG_U16,
		CONFIG_U16,
		CONFIG_U16,
		CONFIG_U16,
		CONFIG_U16,
		CONFIG_U8,
	};
	if (!reader->fitnessValid || reader->fitnessIndex >= 14) {
		return;
	}
	unsigned index = reader->fitnessIndex++;
	reader->fitnessValid = scalar(field, kinds[index], &reader->fitness[index]);
	reader->fitnessSpans[index] = (ValueSpan){field->valueOffset, field->valueLength};
	if (reader->fitnessValid && reader->fitnessIndex == 14) {
		player->condition = reader->fitness[0];
		player->sharpness[0] = reader->fitness[1];
		player->sharpness[1] = reader->fitness[2];
		player->fatigue = reader->fitness[6];
		player->morale = reader->fitness[13];
		if (reader->playerSpans) {
			static const unsigned indices[] = {0, 1, 2, 6, 13};
			for (unsigned i = 0; i < 5; ++i) {
				reader->playerSpans[reader->side][reader->player].wellbeing[i] =
					reader->fitnessSpans[indices[i]];
			}
		}
	}
}

// A player block's strings stay its exported player's after inject-player, which renames only the
// block's note, so they name the player only when there is no note (as in a .bin dump).
static void personName(Reader *reader, const char *text, size_t length, unsigned index) {
	if (reader->section == SECTION_PLAYER && reader->noted) {
		return;
	}
	char *target = reader->section == SECTION_PLAYER
									 ? reader->out->teams[reader->side].players[reader->player].name
									 : reader->out->officials[reader->official];
	if (index == 0) {
		copyText(reader, reader->firstName, sizeof reader->firstName, text, length);
	} else if (index == 1) {
		char name[FIXTURE_TEXT] = "";
		copyText(reader, name, sizeof name, text, length);
		if (reader->error[0]) {
			return;
		}
		char combined[2 * FIXTURE_TEXT];
		int written = snprintf(
			combined,
			sizeof combined,
			"%s%s%s",
			reader->firstName,
			reader->firstName[0] && name[0] ? " " : "",
			name
		);
		copyText(reader, target, FIXTURE_TEXT, combined, (size_t)written);
	} else if (index == 2 && length) {
		copyText(reader, target, FIXTURE_TEXT, text, length);
	}
}

static void textField(Reader *reader, const ConfigField *field) {
	const char *text = (const char *)field->bytes + 4;
	size_t length = field->length - 4;
	unsigned index = reader->strings++;
	if (reader->section == SECTION_PLAYER || reader->section == SECTION_OFFICIAL) {
		personName(reader, text, length, index);
		return;
	}
	if (index != 0) {
		return;
	}
	char *out = NULL;
	switch (reader->section) {
		case SECTION_TEAM: out = reader->out->teams[reader->side].name; break;
		case SECTION_PLAN: out = reader->out->teams[reader->side].tactic; break;
		case SECTION_FIXTURE: out = reader->out->competition; break;
		case SECTION_VENUE: out = reader->out->venue; break;
		default: break;
	}
	if (out && length) {
		copyText(reader, out, FIXTURE_TEXT, text, length);
	}
}

static void visitField(void *context, const ConfigField *field) {
	Reader *reader = context;
	if (reader->error[0]) {
		return;
	}
	if (field->name[0]) {
		namedField(reader, field);
	}
	if (reader->section == SECTION_PLAYER) {
		playerValue(reader, field);
		if (field->player[0]) {
			copyText(
				reader,
				reader->out->teams[reader->side].players[reader->player].name,
				FIXTURE_TEXT,
				field->player,
				strlen(field->player)
			);
			reader->noted = true;
		}
	}
	if (field->kind == CONFIG_STRING) {
		textField(reader, field);
	}
	reader->previousU8 = (FixtureNumber){0};
	(void)scalar(field, CONFIG_U8, &reader->previousU8);
}

static void visitSource(void *context, const char *source) {
	Reader *reader = context;
	copyText(reader, reader->out->source, sizeof reader->out->source, source, strlen(source));
}

static void describeFormation(FixtureTeam *team) {
	unsigned lines[5] = {0};
	unsigned count = 0;
	for (unsigned i = 0; i < FIXTURE_SLOTS; ++i) {
		if (!team->slots[i].position.known) {
			return;
		}
		char position[16];
		fixtureData_position(team->slots[i].position.value, position, sizeof position);
		if (strcmp(position, "Unknown") == 0) {
			return;
		}
		uint64_t mask = team->slots[i].position.value & 0xFFFF;
		if (!mask || (mask & (mask - 1))) {
			return;
		}
		if (mask == 1) {
			continue;
		}
		static const uint64_t lineLimits[] = {16, 128, 1024, 8192};
		unsigned line = 0;
		while (line < 4 && mask > lineLimits[line]) {
			++line;
		}
		++lines[line];
		++count;
	}
	if (count != 10) {
		return;
	}
	size_t used = 0;
	for (unsigned i = 0; i < 5; ++i) {
		if (lines[i]) {
			int written = snprintf(
				team->formation + used, sizeof team->formation - used, "%s%u", used ? "-" : "", lines[i]
			);
			used += (size_t)written;
		}
	}
}

static bool dateNumber(const char **text, unsigned width, unsigned *value) {
	unsigned digits = 0;
	*value = 0;
	while (digits < width && **text >= '0' && **text <= '9') {
		*value = *value * 10 + (unsigned)(*(*text)++ - '0');
		++digits;
	}
	return digits != 0;
}

static void describeWhen(FixtureData *data) {
	const char *dayText = strstr(data->source, " day ");
	if (!dayText) {
		return;
	}
	const char *start = dayText;
	while (start > data->source && start[-1] >= '0' && start[-1] <= '9') {
		--start;
	}
	(void)snprintf(data->when, sizeof data->when, "Invalid export date/time");
	if (dayText - start != 4) {
		return;
	}
	unsigned year = (unsigned)strtoul(start, NULL, 10);
	unsigned day = 0;
	unsigned hour = 0;
	unsigned minute = 0;
	const char *cursor = dayText + 5;
	if (!dateNumber(&cursor, 3, &day) || *cursor++ != ' ' || !dateNumber(&cursor, 2, &hour) ||
			*cursor++ != ':' || !dateNumber(&cursor, 2, &minute) || (*cursor && *cursor != ',') ||
			!year || hour > 23 || minute > 59) {
		return;
	}
	bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
	if (!day || day > 365U + (unsigned)leap) {
		return;
	}
	static const unsigned monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	unsigned month = 0;
	for (; month < 11; ++month) {
		unsigned days = monthDays[month] + (unsigned)(month == 1 && leap);
		if (day <= days) {
			break;
		}
		day -= days;
	}
	(void)snprintf(
		data->when, sizeof data->when, "%04u-%02u-%02u %02u:%02u", year, month + 1, day, hour, minute
	);
}

static bool readData(
	const char *json,
	size_t length,
	FixtureData *out,
	PlayerSpans spans[2][FIXTURE_PLAYERS],
	char *error,
	size_t errorSize
) {
	*out = (FixtureData){0};
	Reader reader = {.out = out, .playerSpans = spans};
	ConfigVisitor visitor = {.context = &reader, .field = visitField, .source = visitSource};
	if (!matchConfig_visit(json, length, &visitor, error, errorSize)) {
		return false;
	}
	if (reader.error[0]) {
		(void)snprintf(error, errorSize, "%s", reader.error);
		return false;
	}
	for (unsigned side = 0; side < 2; ++side) {
		FixtureTeam *team = &out->teams[side];
		if (!team->name[0] || !team->playerCount.known || team->playerCount.value < 11) {
			(void)snprintf(error, errorSize, "missing %s team or squad labels", side ? "away" : "home");
			return false;
		}
		for (unsigned i = 0; i < team->playerCount.value; ++i) {
			if (!team->players[i].present) {
				(void)snprintf(error, errorSize, "missing %s.player[%u]", side ? "away" : "home", i);
				return false;
			}
		}
		team->lineupKnown = team->managerFlags.known && !(team->managerFlags.value & 0x14000);
		describeFormation(team);
	}
	if (strcmp(out->competition, "Friendly") == 0) {
		(void)snprintf(out->matchType, sizeof out->matchType, "Friendly");
	} else {
		(void)snprintf(out->matchType, sizeof out->matchType, "Unknown (type not mapped)");
	}
	describeWhen(out);
	return true;
}

bool fixtureData_read(
	const char *json,
	size_t length,
	FixtureData *out,
	char *error,
	size_t errorSize
) {
	return readData(json, length, out, NULL, error, errorSize);
}

bool fixtureData_hasPlayerEdits(const FixturePlayerEdits *edits) {
	if (edits) {
		for (unsigned side = 0; side < 2; ++side) {
			for (unsigned i = 0; i < FIXTURE_PLAYERS; ++i) {
				if (edits->players[side][i]) {
					return true;
				}
			}
		}
	}
	return false;
}

typedef struct PlayerPatch {
	ValueSpan span;
	unsigned value;
} PlayerPatch;

static int comparePlayerPatches(const void *a, const void *b) {
	const PlayerPatch *left = a;
	const PlayerPatch *right = b;
	return (left->span.offset > right->span.offset) - (left->span.offset < right->span.offset);
}

// Checks one player's edit flags and appends a patch for each wellbeing field they set and, for
// maximum familiarity, each category's first entry: the tactic's setting at amount 10000. A
// category with no entries can't gain one (the file's structure is fixed), so it is left alone.
static bool addPlayerPatches(
	const FixtureTeam *team,
	const PlayerSpans *spans,
	unsigned side,
	unsigned i,
	unsigned flags,
	PlayerPatch *patches,
	size_t *count,
	char *error,
	size_t errorSize
) {
	const FixturePlayer *player = &team->players[i];
	const char *sideName = side ? "away" : "home";
	if (flags & ~(FIXTURE_MAX_HAPPINESS | FIXTURE_MAX_CONDITION | FIXTURE_MAX_FAMILIARITY) ||
			i >= team->playerCount.value || !player->present || !player->name[0] ||
			strcmp(player->name, "<Unknown>") == 0) {
		(void)snprintf(error, errorSize, "Cannot edit %s.player[%u].", sideName, i);
		return false;
	}
	for (unsigned field = 0; field < 5; ++field) {
		if (!(flags & (field == 4 ? FIXTURE_MAX_HAPPINESS : FIXTURE_MAX_CONDITION))) {
			continue;
		}
		if (!spans->wellbeing[field].length) {
			(void)snprintf(error, errorSize, "Missing wellbeing field for %s.player[%u].", sideName, i);
			return false;
		}
		patches[(*count)++] = (PlayerPatch){spans->wellbeing[field],
			field == 4   ? 20U
			: field == 3 ? 0U
									 : 10000U};
	}
	if (!(flags & FIXTURE_MAX_FAMILIARITY)) {
		return true;
	}
	int settings[FIXTURE_FAMILIARITY_CATEGORIES];
	if (!fixtureData_familiaritySettings(team, settings)) {
		(void)snprintf(error, errorSize, "Missing %s tactic values for familiarity.", sideName);
		return false;
	}
	for (unsigned k = 0; k < FIXTURE_FAMILIARITY_CATEGORIES; ++k) {
		const FixtureFamiliarity *familiarity = &player->familiarity[k];
		if (!familiarity->known || (familiarity->count && (!spans->familiarity[k][0].length ||
																												!spans->familiarity[k][1].length))) {
			(void)snprintf(
				error, errorSize, "Missing familiarity fields for %s.player[%u].", sideName, i
			);
			return false;
		}
		if (familiarity->count) {
			patches[(*count)++] = (PlayerPatch){spans->familiarity[k][0], (unsigned)settings[k]};
			patches[(*count)++] = (PlayerPatch){spans->familiarity[k][1], 10000U};
		}
	}
	return true;
}

// Copies the JSON to `out` with each patch's span replaced by its value. False when out of memory.
static bool applyPlayerPatches(
	const char *json,
	size_t length,
	PlayerPatch *patches,
	size_t count,
	ByteBuffer *out
) {
	qsort(patches, count, sizeof patches[0], comparePlayerPatches);
	size_t copied = 0;
	for (size_t i = 0; i < count; ++i) {
		char number[16];
		int size = snprintf(number, sizeof number, "%u", patches[i].value);
		if (!buffer_append(out, json + copied, patches[i].span.offset - copied) ||
				!buffer_append(out, number, (size_t)size)) {
			return false;
		}
		copied = patches[i].span.offset + patches[i].span.length;
	}
	return buffer_append(out, json + copied, length - copied);
}

bool fixtureData_writePlayers(
	const char *json,
	size_t length,
	const FixturePlayerEdits *edits,
	ByteBuffer *out,
	char *error,
	size_t errorSize
) {
	buffer_clear(out);
	struct {
		FixtureData data;
		PlayerSpans spans[2][FIXTURE_PLAYERS];
		PlayerPatch patches[2 * FIXTURE_PLAYERS * (5 + 2 * FIXTURE_FAMILIARITY_CATEGORIES)];
	} *work = calloc(1, sizeof *work);
	if (!work) {
		(void)snprintf(error, errorSize, "Out of memory editing players.");
		return false;
	}
	bool ok = readData(json, length, &work->data, work->spans, error, errorSize);
	size_t count = 0;
	for (unsigned side = 0; ok && edits && side < 2; ++side) {
		for (unsigned i = 0; ok && i < FIXTURE_PLAYERS; ++i) {
			unsigned flags = edits->players[side][i];
			ok = !flags || addPlayerPatches(
											 &work->data.teams[side],
											 &work->spans[side][i],
											 side,
											 i,
											 flags,
											 work->patches,
											 &count,
											 error,
											 errorSize
										 );
		}
	}
	if (ok && !applyPlayerPatches(json, length, work->patches, count, out)) {
		(void)snprintf(error, errorSize, "Out of memory editing players.");
		ok = false;
	}
	free(work);
	if (!ok) {
		buffer_clear(out);
	}
	return ok;
}

#define BIT(value, n) (((value) >> (n)) & 1)

// Tempo: from the mentality, within 5 of the passing style, then the tempo instructions.
static int tempoSetting(uint64_t words, unsigned mentality, int passing) {
	int tempo = mentality < 7 ? (int)(UINT64_C(0x100D0A07070713) >> (8 * mentality) & 0xFF) : 19;
	int base = tempo;
	tempo = tempo < passing - 5 ? passing - 5 : tempo;
	tempo = tempo > passing + 5 ? passing + 5 : tempo;
	tempo = tempo < base - 10 ? base - 10 : tempo;
	tempo = tempo > base + 10 ? base + 10 : tempo;
	tempo = tempo < 2 ? 1 : tempo;
	if (BIT(words, 41)) {
		return tempo < 15 ? 15 : tempo;
	}
	if (BIT(words, 42)) {
		return tempo < 12 ? 12 : tempo;
	}
	if (BIT(words, 44)) {
		return tempo > 5 ? 5 : tempo;
	}
	if (BIT(words, 43)) {
		return tempo >= 9 ? 8 : tempo;
	}
	return tempo;
}

// Width: from the mentality, then the attacking width instructions where the mentality allows.
static int widthSetting(uint64_t words, unsigned mentality) {
	int width = 16;
	bool fairlyWide = false;
	bool wide = false;
	bool fairlyNarrow = true;
	if (mentality <= 6) {
		width = (int)(UINT64_C(0xE0C0A0A080610) >> (8 * mentality) & 0xFF);
		fairlyWide = BIT(30U, mentality);
		wide = BIT(126U, mentality);
		fairlyNarrow = BIT(121U, mentality);
	}
	if (BIT(words, 46)) {
		return fairlyWide ? 12 : width;
	}
	if (BIT(words, 45)) {
		return wide ? 15 : width;
	}
	if (BIT(words, 47)) {
		return fairlyNarrow ? 8 : width;
	}
	return BIT(words, 48) ? 5 : width;
}

// Ports the game's sub_141BF1160 (FM24 24.2): each category's setting from the plan's mentality,
// team instruction bits (word 0 low, word 1 high, then word 2) and its byte +0x18.
bool fixtureData_familiaritySettings(
	const FixtureTeam *team,
	int settings[FIXTURE_FAMILIARITY_CATEGORIES]
) {
	if (!team->mentality.known || !team->planByte18.known || !team->instructions[0].known ||
			!team->instructions[1].known || !team->instructions[2].known) {
		return false;
	}
	uint64_t words = team->instructions[0].value | team->instructions[1].value << 32;
	uint64_t word2 = team->instructions[2].value;
	unsigned mentality = (unsigned)team->mentality.value;
	settings[0] = mentality < 7 ? (int)(UINT64_C(0x100D0A07030114) >> (8 * mentality) & 0xFF) : 20;
	// Passing style: passing directness.
	int passing = BIT(words, 1) ? 20 : 10;
	passing += BIT(words, 2) ? 5 : 0;
	passing -= BIT(words, 4) ? 10 : 0;
	passing -= BIT(words, 3) ? 5 : 0;
	settings[1] = passing;
	// Creative freedom: the plan's byte, then "be more expressive" / "be more disciplined".
	int creative = (int)(signed char)(uint8_t)(5 * team->planByte18.value + (BIT(words, 16) ? 5 : 0) -
																						 (BIT(words, 17) ? 5 : 0));
	settings[2] = creative < 2 ? 1 : creative >= 20 ? 20 : creative;
	// Trigger press, as the game names it: the line of engagement.
	int engagement = BIT(words, 58) ? 20 : 10;
	settings[3] = BIT(words, 60) ? engagement - 10 : engagement;
	// Marking: tackling.
	settings[4] = BIT(word2, 6) ? 5 * (int)BIT(word2, 5) : 5 * (int)BIT(word2, 5) + 5;
	settings[5] = tempoSetting(words, mentality, passing);
	settings[6] = widthSetting(words, mentality);
#undef BIT
	return true;
}

// Ports the game's sub_141BF3BB0: the best entry, each losing 1000 per step its setting is from the
// tactic's, but never below the category's floor.
bool fixtureData_familiarity(
	const FixtureTeam *team,
	const FixturePlayer *player,
	unsigned category,
	unsigned *value
) {
	static const int floors[FIXTURE_FAMILIARITY_CATEGORIES] = {
		1000, 1000, 1000, 1000, 1500, 2500, 2500};
	int settings[FIXTURE_FAMILIARITY_CATEGORIES];
	if (category >= FIXTURE_FAMILIARITY_CATEGORIES || !player->familiarity[category].known ||
			!fixtureData_familiaritySettings(team, settings)) {
		return false;
	}
	const FixtureFamiliarity *familiarity = &player->familiarity[category];
	int best = floors[category];
	for (unsigned i = 0; i < familiarity->count; ++i) {
		int distance = abs(familiarity->setting[i] - settings[category]);
		int worth = (int)familiarity->amount[i] - 1000 * distance;
		best = worth > best ? worth : best;
	}
	*value = (unsigned)best;
	return true;
}

void fixtureData_position(uint64_t mask, char *out, size_t size) {
	uint64_t base = mask & ~((UINT64_C(1) << TACTIC_LEFT_BIT) | (UINT64_C(1) << TACTIC_RIGHT_BIT));
	const char *name = NULL;
	for (unsigned bit = 0; bit < 15; ++bit) {
		if (base == (UINT64_C(1) << bit)) {
			name = tactic_positionName(bit);
		}
	}
	if (!name) {
		(void)snprintf(out, size, "Unknown");
		return;
	}
	const char *side = mask & (UINT64_C(1) << TACTIC_LEFT_BIT)    ? "L"
										 : mask & (UINT64_C(1) << TACTIC_RIGHT_BIT) ? "R"
																																: "";
	if ((mask & (UINT64_C(1) << TACTIC_LEFT_BIT)) && (mask & (UINT64_C(1) << TACTIC_RIGHT_BIT))) {
		(void)snprintf(out, size, "Unknown");
		return;
	}
	(void)snprintf(out, size, "%s%s%s", name, base == 128 && *side ? "C" : "", side);
}

const char *fixtureData_role(uint64_t mask) {
	mask &=
		~((UINT64_C(1) << TACTIC_DEFEND_BIT) | (UINT64_C(1) << TACTIC_SUPPORT_BIT) |
			(UINT64_C(1) << TACTIC_ATTACK_BIT) | (UINT64_C(1) << TACTIC_AUTOMATIC_BIT));
	for (unsigned bit = 0; bit < 64; ++bit) {
		if (mask == (UINT64_C(1) << bit)) {
			const char *name = tactic_roleName(bit);
			return name ? name : "Unknown";
		}
	}
	return "Unknown";
}

const char *fixtureData_duty(uint64_t mask) {
	const unsigned bits[] = {
		TACTIC_DEFEND_BIT, TACTIC_SUPPORT_BIT, TACTIC_ATTACK_BIT, TACTIC_AUTOMATIC_BIT};
	const char *const names[] = {"Defend", "Support", "Attack", "Automatic"};
	const char *found = NULL;
	for (unsigned i = 0; i < 4; ++i) {
		if (mask & (UINT64_C(1) << bits[i])) {
			if (found) {
				return "Unknown";
			}
			found = names[i];
		}
	}
	return found ? found : "Unknown";
}
