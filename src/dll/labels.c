// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "labels.h"

#include "memory.h"
#include "site_members.h"
#include "sites.h"
#include "tactic_flags.h"
#include "tactic_names.h"

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

// MATCH_STAFF+0x60, byte by byte, matched against four staff members' FMRTE profiles
// (docs/FIXTURE.md#staff-attributes). The named bytes 0-26 are on the 1-20 scale, 27-51 on 1-100.
static const char *const staffAttributeNames[STAFF_ATTRIBUTE_COUNT] = {
	"tactical_attacking",
	"business",
	"unknown_2",
	"tactical_directness",
	"level_of_discipline",
	"tactical_free_roles",
	"interference",
	"tactical_marking",
	"tactical_offside",
	"patience",
	"tactical_pressing",
	"resources",
	"working_with_youngsters",
	"unknown_13",
	"buying_players",
	"mind_games",
	"tactical_sitting_back",
	"tactical_use_of_playmaker",
	"tactical_use_of_substitutions",
	"tactical_depth",
	"tactical_flamboyancy",
	"tactical_flexibility",
	"hardness_of_training",
	"squad_rotation",
	"tactical_tempo",
	"tactical_width",
	"unknown_26",
	"coaching_gk_shot_stopping",
	"judging_ca",
	"judging_pa",
	"man_management",
	"motivating",
	"physiotherapy",
	"tactical_knowledge",
	"coaching_attacking",
	"coaching_defending",
	"coaching_fitness",
	"coaching_mental",
	"coaching_technical",
	"coaching_tactical",
	"coaching_dirtiness_allowance",
	"coaching_gk_handling",
	"coaching_gk_distribution",
	"coaching_versatility",
	"judging_player_data",
	"judging_team_data",
	"presenting_data",
	"sports_science",
	"unknown_48",
	"negotiating",
	"judging_staff_ability",
	"coaching_set_pieces",
};

// Player trait (preferred move) bits, in FMRTE's order (docs/PLAN.md Phase 21). Bit 39 is shown in
// game as "cuts inside from both wings".
static const char *const traitNames[] = {
	"runs with ball down left",
	"runs with ball down right",
	"runs with ball through centre",
	"gets into opposition area",
	"moves into channels",
	"gets forward whenever possible",
	"plays short simple passes",
	"tries killer balls often",
	"shoots from distance",
	"shoots with power",
	"places shots",
	"curls ball",
	"likes to round keeper",
	"likes to try to beat offside trap",
	"uses outside of foot",
	"marks opponent tightly",
	"winds up opponents",
	"argues with officials",
	"plays with back to goal",
	"comes deep to get ball",
	"plays one-twos",
	"likes to lob keeper",
	"dictates tempo",
	"attempts overhead kicks",
	"looks for pass rather than attempting to score",
	"plays no through balls",
	"stops play",
	"knocks ball past opponent",
	"moves ball to right foot before dribble attempt",
	"moves ball to left foot before dribble attempt",
	"dwells on ball",
	"arrives late in opponents area",
	"tries to play way out of trouble",
	"stays back at all times",
	"avoids using weaker foot",
	"tries tricks",
	"tries long range free kicks",
	"dives into tackles",
	"does not dive into tackles",
	"cuts inside from both wings",
	"hugs line",
	"gets crowd going",
	"tries first time shots",
	"tries long range passes",
	"likes ball played into feet",
	"hits free kick with power",
	"likes to beat man repeatedly",
	"likes to switch ball to other flank",
	"will retire at top",
	"will play football as long as possible",
	"possesses long flat throw",
	"runs with ball often",
	"runs with ball rarely",
	"attempts to develop weaker foot",
	"static target man",
	"uses long throw to start counter attack",
	"refrains from taking long shots",
	"cuts inside from left wing",
	"cuts inside from right wing",
	"crosses early",
	"brings ball out of defence",
	"plays ball with feet",
};

typedef struct MemberName {
	const char *member;
	const char *name;
	FlagKind flags;
} MemberName;

// Members that hold a copy of a database value (docs/PLAN.md Phase 21): every player's and staff
// member's value equals the database person/player/team field in the comment. The referee's
// person values come from elsewhere. Caps match the game's player screen but no single database
// offset.
static const MemberName memberNames[] = {
	{"MATCH_PLAYER_DATA+0x49", "squad_number", FLAGS_NONE},       // CONTRACT+0x55 (assigned if unset)
	{"MATCH_PLAYER_DATA+0x100", "traits", FLAGS_TRAITS},          // PERSON+0xF0, one bit per trait
	{"MATCH_PLAYER_DATA+0x68", "last_transfer_fee", FLAGS_NONE},  // PLAYER+0x1E0
	{"MATCH_PLAYER_DATA+0x4a", "condition", FLAGS_NONE},          // PLAYER+0x1F8, 0-10000
	{"MATCH_PLAYER_DATA+0x4c", "sharpness[0]", FLAGS_NONE},       // PLAYER+0x1F4, 0-10000
	{"MATCH_PLAYER_DATA+0x4e", "sharpness[1]", FLAGS_NONE},       // PLAYER+0x1F4 again
	{"MATCH_PLAYER_DATA+0x50", "fatigue", FLAGS_NONE},            // PLAYER+0x1F6, signed, 0 is best
	{"MATCH_PLAYER_DATA+0x56", "home_reputation", FLAGS_NONE},    // PLAYER+0x1FA
	{"MATCH_PLAYER_DATA+0x58", "current_reputation", FLAGS_NONE}, // PLAYER+0x1FC
	{"MATCH_PLAYER_DATA+0x5a", "world_reputation", FLAGS_NONE},   // PLAYER+0x1FE
	{"MATCH_PLAYER_DATA+0x5c", "current_ability", FLAGS_NONE},    // PLAYER+0x200
	{"MATCH_PLAYER_DATA+0x74", "morale", FLAGS_NONE}, // PLAYER+0x25F (constants.h: happiness)
	{"MATCH_PLAYER_DATA+0xf4", "preferred_position", FLAGS_NONE}, // PLAYER+0x266
	{"fn_1a7fdc0+0x40", "person_uid", FLAGS_NONE},                // PERSON+0x0C
	{"fn_1a7fdc0+0x54", "nationality", FLAGS_NONE},               // [PERSON+0x70] nation's unique ID
	{"fn_1a7fdc0+0x48", "caps", FLAGS_NONE},                      // international caps
	{"fn_1a7fdc0+0x69", "age", FLAGS_NONE}, // years, from PERSON+0x44 (date of birth)
	{"fn_1a7fdc0+0x62", "personality.professionalism", FLAGS_NONE}, // PERSON+0x7C
	{"fn_1a7fdc0+0x63", "personality.loyalty", FLAGS_NONE},         // PERSON+0x7A
	{"fn_1a7fdc0+0x64", "personality.sportsmanship", FLAGS_NONE},   // PERSON+0x7D
	{"fn_1a7fdc0+0x65", "personality.temperament", FLAGS_NONE},     // PERSON+0x7E
	{"fn_1a7fdc0+0x66", "personality.pressure", FLAGS_NONE},        // PERSON+0x7B
	{"fn_1a7fdc0+0x67", "personality.ambition", FLAGS_NONE},        // PERSON+0x79
	{"fn_1a7fdc0+0x68", "personality.adaptability", FLAGS_NONE},    // PERSON+0x78
	{"MATCH_TEAM_BASE+0x112", "reputation", FLAGS_NONE},            // TEAM+0xA8
	// Staff (manager, backroom and team_extra entries), matched against FMRTE profiles.
	{"MATCH_STAFF+0x98", "home_reputation", FLAGS_NONE},
	{"MATCH_STAFF+0x9a", "current_reputation", FLAGS_NONE},
	{"MATCH_STAFF+0x9c", "world_reputation", FLAGS_NONE},
	{"MATCH_STAFF+0x9e", "current_ability", FLAGS_NONE},
	{"MATCH_STAFF+0xa0", "potential_ability", FLAGS_NONE},
};

// Weather and venue members (docs/FIXTURE.md#officials-venue-and-weather), matched against the
// game's weather text and the stadium's FMRTE and in-game profiles in three saves.
static const MemberName venueMemberNames[] = {
	{"MATCH_BASE+0x6b0", "weather.wind", FLAGS_NONE},        // 0 none, 1 breezy, 2 gusty
	{"MATCH_BASE+0x6b2", "weather.temperature", FLAGS_NONE}, // degrees Celsius
	{"MATCH_BASE+0x6b3", "weather.rain", FLAGS_NONE},        // probably: 0 dry, 2 wet
	{"fn_43cc5e0+0x40", "weather.change_count", FLAGS_NONE},
	{"fn_456a9d0+0x14", "city_longitude", FLAGS_NONE}, // float
	{"fn_456a9d0+0x18", "city_latitude", FLAGS_NONE},  // float
	{"fn_456a9d0+0x20", "nation", FLAGS_NONE},         // the nation's unique ID
	{"fn_456a9d0+0x26", "state", FLAGS_NONE},          // probably 1 very good, 2 good
	{"fn_456a9d0+0x158", "attendance", FLAGS_NONE},
	{"fn_456a9d0+0x15c", "capacity", FLAGS_NONE},
	{"fn_456a9d0+0x160", "seating_capacity", FLAGS_NONE},
	{"fn_456a9d0+0x164", "used_capacity", FLAGS_NONE},
	{"fn_456a9d0+0x16c", "home_fans", FLAGS_NONE},
	{"fn_456a9d0+0x170", "away_fans", FLAGS_NONE},
	{"fn_456a9d0+0x178", "season_ticket_holders", FLAGS_NONE},
	{"fn_456a9d0+0x186", "surface", FLAGS_NONE},         // 1 grass, 8 grass/synthetic mix
	{"fn_456a9d0+0x188", "pitch_condition", FLAGS_NONE}, // FMRTE's "pitch condition"
};

// The venue's pitch landmarks, three floats each (across, along, height), in write order.
static const uint32_t pitchLandmarkSites[] = {
	0x1A8EC4A,
	0x1A8EC6C,
	0x1A8EC8E,
	0x1A8ECAC,
	0x1A8ECCB,
	0x1A8ECEA,
	0x1A8ED09,
	0x1A8ED28,
	0x1A8ED47,
	0x1A8ED66,
	0x1A8ED85,
	0x1A8EDA4,
	0x1A8EDC3,
	0x1A8EDE2,
	0x1A8EE01,
	0x1A8EE20,
	0x1A8EE3F,
	0x1A8EE5E,
	0x1A8EE7D,
	0x1A8EE9C,
	0x1A8EEBB,
	0x1A8EEDA,
	0x1A8EEF9,
	0x1A8EF18,
	0x1A8EF37,
	0x1A8EF56,
	0x1A8EF75,
	0x1A8EF97,
	0x1A8EFB9,
	0x1A8EFDB,
	0x1A8EFFD,
	0x1A8F01F,
	0x1A8F041,
	0x1A8F063,
	0x1A8F085,
	0x1A8F0A7,
	0x1A8F0C9,
	0x1A8F0EB,
	0x1A8F10D,
};

static const char *const pitchLandmarkNames[] = {
	"centre_spot",
	"end[0].goal",
	"end[0].post[0]",
	"end[0].post[1]",
	"end[0].corner[0]",
	"end[0].corner[1]",
	"end[0].penalty_spot",
	"end[1].goal",
	"end[1].post[0]",
	"end[1].post[1]",
	"end[1].corner[0]",
	"end[1].corner[1]",
	"end[1].penalty_spot",
};

// Bits of a stand record's flags byte, from FMRTE's 3D stadium view (bit 4 is set on every stand
// with seats or standing room; bit 5's meaning is unknown).
static const char *const standFlagNames[] = {"roof", "seats", NULL, "tunnel"};

// Extra bits of tactic position and role masks (docs/PLAN.md Phase 15, 17).
enum {
	POSITION_LEFT_BIT = 20,
	POSITION_RIGHT_BIT = 21,
	DUTY_DEFEND_BIT = 21,
	DUTY_SUPPORT_BIT = 22,
	DUTY_ATTACK_BIT = 23,
	DUTY_AUTOMATIC_BIT = 34,
};

// Walk state: where in the match the current record is.
typedef struct Walk {
	const Serialised *serialised;
	const PeopleIndex *people;
	FieldLabel *labels;
	const char *side; // "home"/"away" while inside a team
	int player;
	int backroom;
	size_t playerStart;
	bool playerNamed;
	int tacticSlot;    // formation slot of the current tactic plan
	unsigned planRole; // role keying the current set of player instructions
	size_t siteRunStart;
	char block[LABEL_LENGTH];
	char previousBlock[LABEL_LENGTH];
} Walk;

enum { TACTIC_SLOT_CALL_STRIDE = 0x17 }; // bytes between the slot serialiser calls

static const char *positionFlag(unsigned bit);
static const char *roleFlag(unsigned bit);
static const char *lookup(const SiteName *table, size_t count, uint32_t site);
static int pathHead(const char *path, uint32_t *outer, uint32_t *next);
static void appendBlock(Walk *walk, const char *text);
static void appendIndexedBlock(Walk *walk, const char *text, int index);
static void roleText(unsigned role, char *out, size_t size);
static void roleEntryName(const Walk *walk, size_t first, char *out, size_t size);
static void labelDerivedField(Walk *walk, size_t i, const TraceRecord *record);
static void labelPlayerField(Walk *walk, size_t i, const TraceRecord *record);
static void labelTeamField(
	Walk *walk,
	size_t i,
	const TraceRecord *record,
	const char *side,
	int depth,
	uint32_t teamSite
);
static const char *tacticPart(uint32_t address, int *slot);
static bool insideTactic(uint32_t address);
static unsigned lowestBit(uint64_t mask);
static void labelPlanField(Walk *walk, size_t i, const TraceRecord *record);
static void labelMemberField(const Walk *walk, FieldLabel *label);
static void labelPersonField(const Walk *walk, FieldLabel *label, const TraceRecord *record);
static void appendTacticPart(Walk *walk, size_t index, const TraceRecord *record);
static bool memberOffset(const char *member, const char *prefix, unsigned *offset);
static unsigned earlierWrites(const Walk *walk, size_t i);
static void labelVenueField(Walk *walk, size_t i, const TraceRecord *record);
static void labelRecord(Walk *walk, size_t i);

const char *labels_ratingName(size_t index) {
	return index < sizeof ratingNames / sizeof ratingNames[0] ? ratingNames[index] : "?";
}

const char *labels_staffAttributeName(size_t index) {
	return index < STAFF_ATTRIBUTE_COUNT ? staffAttributeNames[index] : "?";
}

const char *labels_flagName(const FieldLabel *label, unsigned bit) {
	switch (label->flags) {
		case FLAGS_TEAM_INSTRUCTIONS: return tacticFlags_teamInstruction(label->flagWord, bit);
		case FLAGS_PLAYER_INSTRUCTIONS: return tacticFlags_playerInstruction(label->flagWord, bit);
		case FLAGS_POSITION: return positionFlag(bit);
		case FLAGS_ROLE: return roleFlag(bit);
		case FLAGS_TRAITS:
			return bit < sizeof traitNames / sizeof traitNames[0] ? traitNames[bit] : NULL;
		case FLAGS_STAND:
			return bit < sizeof standFlagNames / sizeof standFlagNames[0] ? standFlagNames[bit] : NULL;
		case FLAGS_NONE:
		default: return NULL;
	}
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

static const char *positionFlag(unsigned bit) {
	const char *name = tactic_positionName(bit);
	if (name) {
		return name;
	}
	switch (bit) {
		case POSITION_LEFT_BIT: return "left";
		case POSITION_RIGHT_BIT: return "right";
		default: return NULL;
	}
}

static const char *roleFlag(unsigned bit) {
	switch (bit) {
		case DUTY_DEFEND_BIT: return "duty: defend";
		case DUTY_SUPPORT_BIT: return "duty: support";
		case DUTY_ATTACK_BIT: return "duty: attack";
		case DUTY_AUTOMATIC_BIT: return "duty: automatic";
		default: break;
	}
	return tactic_roleName(bit);
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

// Extends the current block label: ".text" or ".text[index]".
static void appendBlock(Walk *walk, const char *text) {
	size_t length = strlen(walk->block);
	(void)snprintf(walk->block + length, sizeof walk->block - length, ".%s", text);
}

static void appendIndexedBlock(Walk *walk, const char *text, int index) {
	size_t length = strlen(walk->block);
	(void)snprintf(walk->block + length, sizeof walk->block - length, ".%s[%d]", text, index);
}

// The role's name, or "role<id>" if it has none.
static void roleText(unsigned role, char *out, size_t size) {
	const char *name = tactic_roleName(role);
	if (name) {
		(void)snprintf(out, size, "%s", name);
	} else {
		(void)snprintf(out, size, "role%u", role);
	}
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
	const char *roleName = tactic_roleName(role);
	const char *positionName = tactic_positionName(position);
	char roleText[16];
	char positionText[16];
	(void)snprintf(roleText, sizeof roleText, "role%u", role);
	(void)snprintf(positionText, sizeof positionText, "pos%u", position);
	(void)snprintf(
		out, size, "%s@%s", roleName ? roleName : roleText, positionName ? positionName : positionText
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
			// Filler players the game makes for squads outside the loaded database
			// (db::ACTUAL_PLAYER with no database index) have no forename or surname.
			if (!database_personName(person, first->player, sizeof first->player)) {
				(void)snprintf(first->player, sizeof first->player, "(unnamed, UID %u)", uniqueId);
			}
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

// The tactic part a return address inside the tactic serialiser stands for, or NULL. `slot` gets
// the slot index for the slot parts (else -1).
static const char *tacticPart(uint32_t address, int *slot) {
	*slot = -1;
	if (address >= SITE_TACTIC_SLOTS_6920_FIRST && address <= SITE_TACTIC_SLOTS_6920_LAST) {
		*slot = (int)((address - SITE_TACTIC_SLOTS_6920_FIRST) / TACTIC_SLOT_CALL_STRIDE);
		return "slot_6920";
	}
	if (address >= SITE_TACTIC_FORMATION_FIRST && address <= SITE_TACTIC_FORMATION_LAST) {
		*slot = (int)((address - SITE_TACTIC_FORMATION_FIRST) / TACTIC_SLOT_CALL_STRIDE);
		return "formation";
	}
	switch (address) {
		case SITE_TACTIC_TAKERS: return "takers";
		case SITE_TACTIC_PART_4240: return "part_4240";
		case SITE_TACTIC_PLAN_0: return "plan[0]";
		case SITE_TACTIC_PLAN_1: return "plan[1]";
		case SITE_TACTIC_SET_PIECES: return "set_pieces";
		default: return NULL;
	}
}

static bool insideTactic(uint32_t address) {
	return address > SITE_TACTIC_START && address <= SITE_TACTIC_END;
}

// The lowest set bit of a role mask: the role ID (64 if none).
static unsigned lowestBit(uint64_t mask) {
	unsigned bit = 0;
	while (bit < 64 && !(mask >> bit & 1)) {
		++bit;
	}
	return bit;
}

// Names the fields of a tactic plan whose meaning is known (docs/PLAN.md Phase 15).
static void labelPlanField(Walk *walk, size_t i, const TraceRecord *record) {
	FieldLabel *label = &walk->labels[i];
	const char *block = walk->block;
	char role[32];
	switch (record->site) {
		case SITE_TACTIC_MENTALITY:
			walk->tacticSlot = -1;
			(void)snprintf(label->name, sizeof label->name, "%s.mentality", block);
			break;
		case SITE_TACTIC_TEAM_INSTRUCTIONS:
			label->flags = FLAGS_TEAM_INSTRUCTIONS;
			label->flagWord = (unsigned)(i - walk->siteRunStart);
			(void)snprintf(
				label->name, sizeof label->name, "%s.team_instructions[%u]", block, label->flagWord
			);
			break;
		case SITE_TACTIC_SLOT_POSITION:
			++walk->tacticSlot;
			label->flags = FLAGS_POSITION;
			(void)snprintf(
				label->name, sizeof label->name, "%s.slot[%d].position", block, walk->tacticSlot
			);
			break;
		case SITE_TACTIC_SLOT_ROLE:
			label->flags = FLAGS_ROLE;
			(void)snprintf(label->name, sizeof label->name, "%s.slot[%d].role", block, walk->tacticSlot);
			break;
		case SITE_TACTIC_INSTRUCTIONS_ROLE:
			walk->planRole = record->length == 8
												 ? lowestBit(memory_loadU64(walk->serialised->bytes.data + record->offset))
												 : 64;
			roleText(walk->planRole, role, sizeof role);
			(void)snprintf(
				label->name,
				sizeof label->name,
				"%s.slot[%d].player_instructions[%s]",
				block,
				walk->tacticSlot,
				role
			);
			break;
		case SITE_TACTIC_INSTRUCTIONS_BITS:
			label->flags = FLAGS_PLAYER_INSTRUCTIONS;
			label->flagWord = (unsigned)(i - walk->siteRunStart);
			roleText(walk->planRole, role, sizeof role);
			(void)snprintf(
				label->name,
				sizeof label->name,
				"%s.slot[%d].player_instructions[%s].bits[%u]",
				block,
				walk->tacticSlot,
				role,
				label->flagWord
			);
			break;
		default: break;
	}
}

// Names a field whose member holds a known database value. A player block already names its
// person_uid (SITE_PLAYER_UID), so the person's copy of it is left unnamed there.
static void labelMemberField(const Walk *walk, FieldLabel *label) {
	if (!label->member || label->name[0]) {
		return;
	}
	for (size_t k = 0; k < sizeof memberNames / sizeof memberNames[0]; ++k) {
		if (strcmp(label->member, memberNames[k].member) != 0) {
			continue;
		}
		if (strcmp(memberNames[k].name, "person_uid") == 0 && strstr(walk->block, ".player[")) {
			return;
		}
		(void)snprintf(label->name, sizeof label->name, "%s.%s", walk->block, memberNames[k].name);
		label->flags = memberNames[k].flags;
		return;
	}
}

// Names the person block's fields that have no member (written from locals): height and weight.
static void labelPersonField(const Walk *walk, FieldLabel *label, const TraceRecord *record) {
	const char *name = NULL;
	switch (record->site) {
		case SITE_PERSON_HEIGHT: name = "height"; break;
		case SITE_PERSON_WEIGHT: name = "weight"; break;
		default: return;
	}
	if (!label->name[0]) {
		(void)snprintf(label->name, sizeof label->name, "%s.%s", walk->block, name);
	}
}

// Extends the block label with ".tactic[.part]" if the record was written by the tactic
// serialiser: one of its return addresses is on the record's call path, or it wrote it directly.
static void appendTacticPart(Walk *walk, size_t index, const TraceRecord *record) {
	const char *path = walk->serialised->paths[record->path];
	bool inTactic = insideTactic(record->site);
	const char *part = NULL;
	int slot = -1;
	for (const char *token = path; *token;) {
		char *end = NULL;
		uint32_t address = (uint32_t)strtoul(token, &end, 16);
		if (end == token) {
			break;
		}
		if (insideTactic(address)) {
			inTactic = true;
			part = tacticPart(address, &slot);
		}
		token = *end == '>' ? end + 1 : end;
	}
	if (!inTactic) {
		return;
	}
	appendBlock(walk, "tactic");
	if (part && slot >= 0) {
		appendIndexedBlock(walk, part, slot);
	} else if (part) {
		appendBlock(walk, part);
	}
	if (part && strncmp(part, "plan", 4) == 0) {
		labelPlanField(walk, index, record);
	}
}

// The offset in a member "<prefix>0xNN" (e.g. "fn_45844b0+0x" for "fn_45844b0+0x1c").
static bool memberOffset(const char *member, const char *prefix, unsigned *offset) {
	size_t length = strlen(prefix);
	if (!member || strncmp(member, prefix, length) != 0) {
		return false;
	}
	char *end = NULL;
	unsigned long value = strtoul(member + length, &end, 16);
	if (end == member + length || *end) {
		return false;
	}
	*offset = (unsigned)value;
	return true;
}

// How many earlier records were written by record i's call site: the weather change's index.
static unsigned earlierWrites(const Walk *walk, size_t i) {
	uint32_t site = walk->serialised->records[i].site;
	unsigned count = 0;
	for (size_t j = 0; j < i; ++j) {
		count += walk->serialised->records[j].site == site;
	}
	return count;
}

// A field name built in a caller's buffer, and how to name its bits.
typedef struct BuiltName {
	char text[LABEL_LENGTH];
	FlagKind flags;
} BuiltName;

// A weather change's fields (match header): its time, probable rain level and temperature.
static const char *weatherFieldName(
	const Walk *walk,
	size_t i,
	const TraceRecord *record,
	const FieldLabel *label,
	BuiltName *out
) {
	unsigned offset = 0;
	const char *field = NULL;
	if (record->site == SITE_WEATHER_CHANGE_TIME) {
		field = "time";
	} else if (memberOffset(label->member, "fn_43cc5e0+0x", &offset) &&
						 (offset == 5 || offset == 6)) {
		field = offset == 5 ? "rain" : "temperature";
	} else {
		return NULL;
	}
	(void)snprintf(
		out->text, sizeof out->text, "weather.change[%u].%s", earlierWrites(walk, i), field
	);
	return out->text;
}

// Fields with a fixed name in the venue, fixture and fixture_extra blocks.
static const char *fixedVenueFieldName(const char *block, const TraceRecord *record) {
	bool venue = strcmp(block, "venue") == 0;
	if (venue || strcmp(block, "fixture") == 0) {
		switch (record->site) {
			case SITE_STADIUM_INDEX: return "stadium.index";
			case SITE_STADIUM_UID: return "stadium.uid";
			case SITE_TEXT: return record->caller == SITE_STADIUM_NAME_CALLER ? "stadium.name" : NULL;
			case SITE_FIXTURE_ATTENDANCE: return venue ? NULL : "attendance";
			case SITE_FIXTURE_AWAY_FANS: return venue ? NULL : "away_fans";
			default: break;
		}
	}
	if (strcmp(block, "fixture_extra") == 0) {
		switch (record->site) {
			case SITE_FIXTURE_EXTRA_HOME_FANS: return "home_fans";
			case SITE_FIXTURE_EXTRA_AWAY_FANS: return "away_fans";
			default: return NULL;
		}
	}
	if (!venue) {
		return NULL;
	}
	switch (record->site) {
		case SITE_PITCH_WIDTH: return "pitch.width";
		case SITE_PITCH_HALF_WIDTH: return "pitch.half_width";
		case SITE_PITCH_HALF_LENGTH: return "pitch.half_length";
		case SITE_PITCH_QUARTER_WIDTH: return "pitch.quarter_width";
		case SITE_PITCH_QUARTER_LENGTH: return "pitch.quarter_length";
		case SITE_VENUE_UID: return "uid";
		case SITE_SEAT_COLOUR_0: return "seat_colour[0]";
		case SITE_SEAT_COLOUR_1: return "seat_colour[1]";
		case SITE_STAND_0_CAPACITY: return "stand[0].capacity";
		case SITE_SECTION_SEATS_FIRST: return "section[0].seats";
		case SITE_SECTION_SEATS_LAST: return "section[31].seats";
		case SITE_OCCUPANCY_LAST: return "section[31].occupancy";
		case SITE_OCCUPANCY_FIRST:
			return record->caller == SITE_OCCUPANCY_CALLER ? "section[0].occupancy" : NULL;
		default: return NULL;
	}
}

// The venue's pitch landmarks: "pitch.<point>.<axis>".
static const char *pitchLandmarkName(const TraceRecord *record, BuiltName *out) {
	for (size_t k = 0; k < sizeof pitchLandmarkSites / sizeof pitchLandmarkSites[0]; ++k) {
		if (record->site == pitchLandmarkSites[k]) {
			(void)snprintf(
				out->text, sizeof out->text, "pitch.%s.%c", pitchLandmarkNames[k / 3], "xyz"[k % 3]
			);
			return out->text;
		}
	}
	return NULL;
}

// The venue's seating sections (4 stands x 8 slots) and its 8 stand records.
static const char *
venueArrayName(const TraceRecord *record, const FieldLabel *label, BuiltName *out) {
	unsigned offset = 0;
	if (memberOffset(label->member, "fn_43d0740+0x", &offset)) {
		if (record->caller != SITE_OCCUPANCY_CALLER) {
			return NULL;
		}
		(void)snprintf(out->text, sizeof out->text, "section[%u].occupancy", offset);
	} else if (memberOffset(label->member, "fn_43d0230+0x", &offset)) {
		if (offset >= 0x40 && offset < 0x60) {
			return NULL;
		}
		(void)snprintf(
			out->text,
			sizeof out->text,
			offset < 0x40 ? "section[%u].seats" : "section[%u].type",
			offset < 0x40 ? offset / 2 : offset - 0x60
		);
	} else if (memberOffset(label->member, "fn_45844b0+0x", &offset) && offset < 0x40) {
		// Stand records are 8 bytes: u32 capacity, u16, u8, u8 flags.
		if (offset % 8 != 0 && offset % 8 != 7) {
			return NULL;
		}
		out->flags = offset % 8 == 7 ? FLAGS_STAND : FLAGS_NONE;
		(void)snprintf(
			out->text,
			sizeof out->text,
			offset % 8 == 7 ? "stand[%u].flags" : "stand[%u].capacity",
			offset / 8
		);
	} else {
		return NULL;
	}
	return out->text;
}

// Names the weather fields in the match header, the venue's and the attendance in the fixture.
static void labelVenueField(Walk *walk, size_t i, const TraceRecord *record) {
	FieldLabel *label = &walk->labels[i];
	BuiltName built = {.flags = FLAGS_NONE};
	const char *name = NULL;
	if (strcmp(walk->block, "match") == 0) {
		name = weatherFieldName(walk, i, record, label, &built);
	} else {
		name = fixedVenueFieldName(walk->block, record);
		if (!name && strcmp(walk->block, "venue") == 0) {
			name = pitchLandmarkName(record, &built);
			name = name ? name : venueArrayName(record, label, &built);
		}
	}
	for (size_t k = 0; !name && k < sizeof venueMemberNames / sizeof venueMemberNames[0]; ++k) {
		if (label->member && strcmp(label->member, venueMemberNames[k].member) == 0) {
			name = venueMemberNames[k].name;
		}
	}
	if (name) {
		(void)snprintf(label->name, sizeof label->name, "%s.%s", walk->block, name);
		label->flags = built.flags;
	}
}

static void labelRecord(Walk *walk, size_t i) {
	const TraceRecord *record = &walk->serialised->records[i];
	if (i == 0 || walk->serialised->records[i - 1].site != record->site) {
		walk->siteRunStart = i;
	}
	uint32_t outer = 0;
	uint32_t next = 0;
	int depth = pathHead(walk->serialised->paths[record->path], &outer, &next);
	const char *part =
		depth ? lookup(matchParts, sizeof matchParts / sizeof matchParts[0], outer) : NULL;
	(void)snprintf(walk->block, sizeof walk->block, "%s", part ? part : "unknown");
	if (part && (strcmp(part, "home") == 0 || strcmp(part, "away") == 0)) {
		labelTeamField(walk, i, record, part, depth, next);
		appendTacticPart(walk, i, record);
	}
	FieldLabel *label = &walk->labels[i];
	label->member = siteMembers_find(record->site);
	labelVenueField(walk, i, record);
	labelMemberField(walk, label);
	labelPersonField(walk, label, record);
	if (record->site == SITE_STAFF_ATTRIBUTES && record->length == STAFF_ATTRIBUTE_COUNT) {
		// The writer names each byte "<name>.<attribute>".
		(void)snprintf(label->name, sizeof label->name, "%s.attributes", walk->block);
		label->staffAttributes = true;
	}
	if (strcmp(walk->block, walk->previousBlock) != 0 && label->name[0] == 0) {
		(void)snprintf(label->name, sizeof label->name, "%s", walk->block);
	}
	memcpy(walk->previousBlock, walk->block, sizeof walk->block);
}
