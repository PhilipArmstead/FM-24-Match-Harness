// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture_data.h"

#include "match_config.h"
#include "tactic_names.h"

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
	char firstName[FIXTURE_TEXT];
	char error[192];
} Reader;

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
		if (indexed(name + prefixLength, "slot[", &index, &suffix) && index < FIXTURE_SLOTS) {
			if (strcmp(suffix, ".position") == 0) {
				(void)scalar(field, CONFIG_U32, &team->slots[index].position);
			} else if (strcmp(suffix, ".role") == 0) {
				(void)scalar(field, CONFIG_U64, &team->slots[index].role);
			}
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

static void playerValue(Reader *reader, const ConfigField *field) {
	FixturePlayer *player = &reader->out->teams[reader->side].players[reader->player];
	const char *suffix = strchr(field->name, ']');
	suffix = suffix ? suffix + 1 : "";
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
		if (strcmp(suffix, values[i].name) == 0 && !scalar(field, values[i].kind, numbers[i])) {
			(void)snprintf(reader->error, sizeof reader->error, "wrong type for %s", field->name);
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
	if (reader->fitnessValid && reader->fitnessIndex == 14) {
		player->condition = reader->fitness[0];
		player->sharpness[0] = reader->fitness[1];
		player->sharpness[1] = reader->fitness[2];
		player->fatigue = reader->fitness[6];
		player->morale = reader->fitness[13];
	}
}

static void personName(Reader *reader, const char *text, size_t length, unsigned index) {
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
		}
	}
	if (field->kind == CONFIG_STRING) {
		textField(reader, field);
	}
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

bool fixtureData_read(
	const char *json,
	size_t length,
	FixtureData *out,
	char *error,
	size_t errorSize
) {
	*out = (FixtureData){0};
	Reader reader = {.out = out};
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
