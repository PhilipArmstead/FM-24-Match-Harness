// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "labels.h"

#include "memory.h"
#include "site_members.h"
#include "sites.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct SiteName {
	uint32_t site;
	const char *name;
} SiteName;

static const SiteName matchParts[] = {
	{SITE_PART_MATCH, "match"},
	{SITE_PART_OFFICIAL_0, "official[0]"},
	{SITE_PART_OFFICIAL_1, "official[1]"},
	{SITE_PART_OFFICIAL_2, "official[2]"},
	{SITE_PART_OFFICIAL_3, "official[3]"},
	{SITE_PART_REFEREE, "referee"},
	{SITE_PART_VENUE, "venue"},
	{SITE_PART_FIXTURE, "fixture"},
	{SITE_PART_FIXTURE_EXTRA, "fixture_extra"},
	{SITE_PART_HOME, "home"},
	{SITE_PART_AWAY, "away"},
	{SITE_PART_EXTRA_53952, "extra_53952"},
	{SITE_PART_EXTRA_1872, "extra_1872"},
};

static const SiteName teamParts[] = {
	{SITE_TEAM_BASE, "team"},
	{SITE_TEAM_INFO, "team_info"},
	{SITE_TEAM_MANAGER, "manager"},
	{SITE_TEAM_STAFF_0, "staff[0]"},
	{SITE_TEAM_STAFF_1, "staff[1]"},
	{SITE_TEAM_STAFF_2, "staff[2]"},
	{SITE_TEAM_PLAYER, "player"},
	{SITE_TEAM_EXTRA, "team_extra"},
	{SITE_TEAM_BACKROOM, "backroom"},
};

static const SiteName teamFields[] = {
	{SITE_TEAM_HAS_STAFF_1, "has_staff[1]"},
	{SITE_TEAM_HAS_STAFF_2, "has_staff[2]"},
	{SITE_TEAM_FLAG_961, "flag_961"},
	{SITE_TEAM_PLAYER_COUNT, "player_count"},
	{SITE_TEAM_HAS_EXTRA, "has_team_extra"},
	{SITE_TEAM_BACKROOM_COUNT, "backroom_count"},
};

static const char *const ratingNames[69] = {
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

// Role IDs are bit indices of the game's role mask (sub_140ABC790); "" = not seen.
static const char *const roleNames[] = {
	"goalkeeper",
	"central_defender",
	"full_back",
	"wing_back",
	"defensive_midfielder",
	"central_midfielder",
	"wide_midfielder",
	"winger",
	"",
	"attacking_midfielder",
	"deep_lying_forward",
	"advanced_forward",
	"sweeper_keeper",
	"",
	"libero",
	"deep_lying_playmaker",
	"box_to_box_midfielder",
	"advanced_playmaker",
	"target_man",
	"poacher",
	"complete_forward",
	"",
	"",
	"",
	"ball_playing_defender",
	"",
	"",
	"inside_forward",
	"ball_winning_midfielder",
	"no_nonsense_centre_back",
	"defensive_winger",
	"pressing_forward",
	"trequartista",
	"anchor_man",
	"",
	"half_back",
	"no_nonsense_full_back",
	"enganche",
	"complete_wing_back",
	"regista",
	"false_nine",
	"shadow_striker",
	"wide_target_man",
	"wide_playmaker",
	"inverted_wing_back",
	"raumdeuter",
	"roaming_playmaker",
	"mezzala",
	"carrilero",
	"inverted_winger",
	"segundo_volante",
	"wide_centre_back",
	"inverted_full_back",
};

// Position codes in role rating entries (code 1 has not been seen).
static const char *const positionNames[] = {
	"GK",
	"",
	"DR",
	"DL",
	"DC",
	"WBR",
	"WBL",
	"DM",
	"MR",
	"ML",
	"MC",
	"AMR",
	"AML",
	"AMC",
	"ST",
};

const char *labels_ratingName(size_t index) {
	return index < sizeof ratingNames / sizeof ratingNames[0] ? ratingNames[index] : "?";
}

static const char *lookup(const SiteName *table, size_t count, uint32_t site) {
	for (size_t i = 0; i < count; ++i) {
		if (table[i].site == site) {
			return table[i].name;
		}
	}
	return NULL;
}

// The first two RVAs of a path ("outer>next>..."); returns how many were present (0-2).
static int pathHead(const char *path, uint32_t *outer, uint32_t *next) {
	char *end = NULL;
	if (!*path) {
		return 0;
	}
	*outer = (uint32_t)strtoul(path, &end, 16);
	if (*end != '>') {
		return 1;
	}
	*next = (uint32_t)strtoul(end + 1, NULL, 16);
	return 2;
}

// Walk state: where in the match the current record is.
typedef struct Walk {
	const Serialised *serialised;
	const PeopleIndex *people;
	FieldLabel *labels;
	const char *side; // "home" / "away" while inside a team
	int player;
	int backroom;
	size_t playerStart;
	bool playerNamed;
	char block[LABEL_LENGTH];
	char previousBlock[LABEL_LENGTH];
} Walk;

// Extends the current block label: ".text" or ".text[index]".
static void appendBlock(Walk *walk, const char *text) {
	size_t length = strlen(walk->block);
	(void)snprintf(walk->block + length, sizeof walk->block - length, ".%s", text);
}

static void appendIndexedBlock(Walk *walk, const char *text, int index) {
	size_t length = strlen(walk->block);
	(void)snprintf(walk->block + length, sizeof walk->block - length, ".%s[%d]", text, index);
}

// "winger@AMR" for the role rating entry whose rating is record `first`.
static void roleEntryName(const Walk *walk, size_t first, char *out, size_t size) {
	const Serialised *serialised = walk->serialised;
	if (first + 2 >= serialised->recordCount) {
		(void)snprintf(out, size, "?");
		return;
	}
	unsigned role = serialised->bytes.data[serialised->records[first + 1].offset];
	unsigned position = serialised->bytes.data[serialised->records[first + 2].offset];
	const char *roleName = role < sizeof roleNames / sizeof roleNames[0] ? roleNames[role] : "";
	const char *positionName =
		position < sizeof positionNames / sizeof positionNames[0] ? positionNames[position] : "";
	char roleText[16];
	char positionText[16];
	(void)snprintf(roleText, sizeof roleText, "role%u", role);
	(void)snprintf(positionText, sizeof positionText, "pos%u", position);
	(void)snprintf(
		out,
		size,
		"%s@%s",
		roleName[0] ? roleName : roleText,
		positionName[0] ? positionName : positionText
	);
}

// Names the derived values in a player's data whose meaning is known (docs/PLAN.md Phase 11).
static void labelDerivedField(Walk *walk, size_t i, const TraceRecord *record) {
	FieldLabel *label = &walk->labels[i];
	char entry[64];
	switch (record->site) {
		case SITE_PLAYER_ABILITY:
			(void)snprintf(label->name, sizeof label->name, "%s.ability", walk->block);
			break;
		case SITE_PLAYER_LEADERSHIP:
			(void)snprintf(label->name, sizeof label->name, "%s.leadership_value", walk->block);
			break;
		case SITE_ROLE_RATING:
			roleEntryName(walk, i, entry, sizeof entry);
			(void)snprintf(label->name, sizeof label->name, "%s.role_rating[%s]", walk->block, entry);
			break;
		case SITE_ROLE_ID:
			roleEntryName(walk, i - 1, entry, sizeof entry);
			(void)snprintf(
				label->name, sizeof label->name, "%s.role_rating[%s].role", walk->block, entry
			);
			break;
		case SITE_ROLE_POSITION:
			roleEntryName(walk, i - 2, entry, sizeof entry);
			(void)snprintf(
				label->name, sizeof label->name, "%s.role_rating[%s].position", walk->block, entry
			);
			break;
		default: break;
	}
}

static void labelPlayerField(Walk *walk, size_t i, const TraceRecord *record) {
	if (record->site == SITE_PLAYER_FIRST) {
		++walk->player;
		walk->playerStart = i;
		walk->playerNamed = false;
	}
	appendIndexedBlock(walk, "player", walk->player);
	FieldLabel *label = &walk->labels[i];
	if (record->site == SITE_PLAYER_UID && record->length == 4 && !walk->playerNamed) {
		uint32_t uniqueId = memory_loadU32(walk->serialised->bytes.data + record->offset);
		const uint8_t *person = database_findPerson(walk->people, uniqueId);
		if (person) {
			FieldLabel *first = &walk->labels[walk->playerStart];
			(void)database_personName(person, first->player, sizeof first->player);
			(void)snprintf(label->name, sizeof label->name, "%s.person_uid", walk->block);
			walk->playerNamed = true;
		}
	}
	if (record->site == SITE_PLAYER_RATINGS && record->length == PLAYER_RATING_COUNT) {
		(void)snprintf(label->name, sizeof label->name, "%s.ratings", walk->block);
		label->ratings = true;
	}
	labelDerivedField(walk, i, record);
}

static void labelTeamField(
	Walk *walk,
	size_t i,
	const TraceRecord *record,
	const char *side,
	int depth,
	uint32_t teamSite
) {
	if (side != walk->side) {
		walk->player = -1;
		walk->backroom = -1;
		walk->side = side;
	}
	const char *part =
		depth > 1 ? lookup(teamParts, sizeof teamParts / sizeof teamParts[0], teamSite) : NULL;
	const char *field =
		depth == 1 ? lookup(teamFields, sizeof teamFields / sizeof teamFields[0], record->site) : NULL;
	if (part && strcmp(part, "player") == 0) {
		labelPlayerField(walk, i, record);
	} else if (part && strcmp(part, "backroom") == 0) {
		if (record->site == SITE_BACKROOM_FIRST) {
			++walk->backroom;
		}
		appendIndexedBlock(walk, "backroom", walk->backroom);
	} else if (part) {
		appendBlock(walk, part);
	} else if (field) {
		appendBlock(walk, field);
		(void)snprintf(walk->labels[i].name, sizeof walk->labels[i].name, "%s", walk->block);
	}
}

static void labelRecord(Walk *walk, size_t i) {
	const TraceRecord *record = &walk->serialised->records[i];
	uint32_t outer = 0;
	uint32_t next = 0;
	int depth = pathHead(walk->serialised->paths[record->path], &outer, &next);
	const char *part =
		depth ? lookup(matchParts, sizeof matchParts / sizeof matchParts[0], outer) : NULL;
	(void)snprintf(walk->block, sizeof walk->block, "%s", part ? part : "unknown");
	if (part && (strcmp(part, "home") == 0 || strcmp(part, "away") == 0)) {
		labelTeamField(walk, i, record, part, depth, next);
	}
	FieldLabel *label = &walk->labels[i];
	memcpy(label->block, walk->block, sizeof label->block);
	label->member = siteMembers_find(record->site);
	if (strcmp(walk->block, walk->previousBlock) != 0 && label->name[0] == 0) {
		(void)snprintf(label->name, sizeof label->name, "%s", walk->block);
	}
	memcpy(walk->previousBlock, walk->block, sizeof walk->block);
}

FieldLabel *labels_compute(const Serialised *serialised, const PeopleIndex *people) {
	FieldLabel *labels =
		calloc(serialised->recordCount ? serialised->recordCount : 1, sizeof *labels);
	if (!labels) {
		return NULL;
	}
	Walk walk = {
		.serialised = serialised, .people = people, .labels = labels, .player = -1, .backroom = -1};
	for (size_t i = 0; i < serialised->recordCount; ++i) {
		labelRecord(&walk, i);
	}
	return labels;
}

static size_t countPlayers(const Serialised *serialised) {
	size_t count = 0;
	for (size_t i = 0; i < serialised->recordCount; ++i) {
		count += serialised->records[i].site == SITE_PLAYER_FIRST;
	}
	return count;
}

PlayerRatings *
labels_players(const Serialised *serialised, const PeopleIndex *people, size_t *count) {
	*count = 0;
	PlayerRatings *players = calloc(countPlayers(serialised) + 1, sizeof *players);
	bool haveId = false;
	for (size_t i = 0; players && i < serialised->recordCount; ++i) {
		const TraceRecord *record = &serialised->records[i];
		const uint8_t *value = serialised->bytes.data + record->offset;
		if (record->site == SITE_PLAYER_FIRST) {
			players[(*count)++] = (PlayerRatings){0};
			haveId = false;
		}
		if (*count == 0) {
			continue;
		}
		PlayerRatings *player = &players[*count - 1];
		if (record->site == SITE_PLAYER_UID && record->length == 4 && !haveId &&
				database_findPerson(people, memory_loadU32(value))) {
			player->uniqueId = memory_loadU32(value);
			haveId = true;
		}
		if (record->site == SITE_PLAYER_RATINGS && record->length == PLAYER_RATING_COUNT) {
			memcpy(player->ratings, value, PLAYER_RATING_COUNT);
		}
	}
	return players;
}
