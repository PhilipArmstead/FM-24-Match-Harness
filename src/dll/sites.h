// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Serialiser call sites (return-address RVAs) that identify parts of a serialised match. Each
// call that GAME_MATCH__serialize makes writes one part of the match, each call
// MATCH_TEAM__serialize makes writes one part of a team; players and backroom staff are recognised
// by their first write.
#pragma once

#include <stdint.h>


// Return addresses inside GAME_MATCH__serialize.
enum MatchPart {
	SITE_PART_MATCH = 0x1AC76FB,
	SITE_PART_OFFICIAL_0 = 0x1AC7719,
	SITE_PART_OFFICIAL_1 = 0x1AC7730,
	SITE_PART_OFFICIAL_2 = 0x1AC7747,
	SITE_PART_OFFICIAL_3 = 0x1AC775E,
	SITE_PART_REFEREE = 0x1AC7775,
	SITE_PART_VENUE = 0x1AC778C,
	SITE_PART_FIXTURE = 0x1AC77B3,
	SITE_PART_FIXTURE_EXTRA = 0x1AC77C2,
	SITE_PART_HOME = 0x1AC77D5,
	SITE_PART_AWAY = 0x1AC77E8,
	SITE_PART_EXTRA_53952 = 0x1AC77FE,
	SITE_PART_EXTRA_1872 = 0x1AC781A,
};

// Return addresses inside MATCH_TEAM__serialize.
enum TeamPart {
	SITE_TEAM_BASE = 0x1B6F27C,
	SITE_TEAM_INFO = 0x1B6F333,
	SITE_TEAM_MANAGER = 0x1B6F34B,
	SITE_TEAM_STAFF_0 = 0x1B6F362,
	SITE_TEAM_STAFF_1 = 0x1B6F3A1,
	SITE_TEAM_STAFF_2 = 0x1B6F3E0,
	SITE_TEAM_PLAYER = 0x1B6F456,
	SITE_TEAM_EXTRA = 0x1B6F4B7,
	SITE_TEAM_BACKROOM = 0x1B6F51F,
};

// Single writes MATCH_TEAM__serialize makes itself (write call sites).
enum TeamField {
	SITE_TEAM_HAS_STAFF_1 = 0x1B6F38B,
	SITE_TEAM_HAS_STAFF_2 = 0x1B6F3CA,
	SITE_TEAM_FLAG_961 = 0x1B6F3FE,
	SITE_TEAM_PLAYER_COUNT = 0x1B6F417,
	SITE_TEAM_HAS_EXTRA = 0x1B6F48B,
	SITE_TEAM_BACKROOM_COUNT = 0x1B6F4E7,
};

// Write call sites inside player and backroom blocks.
enum PlayerField {
	SITE_PLAYER_FIRST = 0x1B3C719,      // first write of each player (MATCH_PLAYER__serialize)
	SITE_PLAYER_UID = 0x437495A,        // person unique ID (also other IDs; the first DB match wins)
	SITE_PLAYER_RATINGS = 0x1A808F5,    // 69 bytes: 15 positions (1-20) + 54 attributes (1-100)
	SITE_BACKROOM_FIRST = 0x1A83EEB,    // first write of each backroom/manager entry
	SITE_STAFF_ATTRIBUTES = 0x1A83FDF,  // MATCH_STAFF+0x60: 52 bytes of staff attributes
	SITE_PLAYER_ABILITY = 0x1A80A2B,    // MATCH_PLAYER_DATA+0x60: ability, caps every role rating
	SITE_PLAYER_LEADERSHIP = 0x1A80AD4, // MATCH_PLAYER_DATA+0x70: driven by leadership
	SITE_ROLE_RATING = 0x1A80FAC,       // role rating entries {u16 rating, u8 role, u8 position}:
	SITE_ROLE_ID = 0x1A80FC1,           //   written rating, role, position
	SITE_ROLE_POSITION = 0x1A80FD4,
};

// Write call sites of a player's tactical familiarity (MATCH_PLAYER_DATA+0x190, written by
// sub_141BF3340; docs/FIXTURE.md#tactical-familiarity): a version byte, the position/role/duty
// entries, then seven setting categories of {setting, amount} entries.
enum FamiliarityField {
	SITE_FAMILIARITY_VERSION = 0x1BF3372,        // u8, always 1
	SITE_FAMILIARITY_ROLE_COUNT = 0x1BF1829,     // u32: position/role/duty entries
	SITE_FAMILIARITY_ROLE_KEY = 0x1BF1879,       // u16: position, role and duty
	SITE_FAMILIARITY_ROLE_POSITIONS = 0x1BF18B1, // 11 bytes: position codes, 0xF0 unused
	SITE_FAMILIARITY_ROLE_AMOUNT = 0x1BF18DB,    // u32: 0-10000
	SITE_FAMILIARITY_COUNT = 0x1BF33D0,          // u32: a category's entries
	SITE_FAMILIARITY_SETTING = 0x1BF3400,        // u8: signed, the tactic setting (1-20)
	SITE_FAMILIARITY_AMOUNT = 0x1BF341D,         // u32: 0-10000
};

// Write call sites in the person block (sub_141A7FDC0) every player, staff member and official has.
// Players get their database values; others mostly a default.
enum PersonField {
	SITE_PERSON_HEIGHT = 0x1A8004E, // u32: float metres, from PLAYER+0x14E (u16 cm)
	SITE_PERSON_WEIGHT = 0x1A80064, // u8: kg, from PLAYER+0x14C
};

// Write call sites of an official's ratings (sub_141A267200), each a u8 1-20, matched against two
// referees' profiles (docs/FIXTURE.md#officials). The first, allowing flow, names the block.
// The officials' pressure rating is their personality.pressure.
enum OfficialField {
	SITE_OFFICIAL_ALLOWING_FLOW = 0x1A267222,
	SITE_OFFICIAL_DISCIPLINE = 0x1A267238,
	SITE_OFFICIAL_IMPORTANT_MATCHES = 0x1A26724E,
	SITE_OFFICIAL_REFEREEING = 0x1A267274,
	SITE_OFFICIAL_RUNNING_LINE = 0x1A26728A,
	SITE_OFFICIAL_TIME_KEEPING = 0x1A2672A0,
};

// Write call sites of a staff entry's match focus (MATCH_STAFF+0x08..+0x10, a byte vector): its
// length, then one focus code per byte (docs/FIXTURE.md#match-focus). Only managers have codes.
enum StaffField {
	SITE_STAFF_FOCUS_COUNT = 0x1A844DC, // u16
	SITE_STAFF_FOCUS_CODE = 0x1A84508,  // u8: 0-2 phase of play, 3-8 set piece
};

// Write call sites of the weather, venue and attendance fields that have no member of their own
// (docs/FIXTURE.md#officials-venue-and-weather). The rest are named by member (labels.c).
enum VenueField {
	SITE_WEATHER_CHANGE_TIME = 0x43CC67B,  // u32 per weather change, after its three bytes
	SITE_PITCH_WIDTH = 0x1A8EBA0,          // float metres; the venue's first field is the length
	SITE_PITCH_HALF_WIDTH = 0x1A8EBC2,     // float metres
	SITE_PITCH_HALF_LENGTH = 0x1A8EBE4,    // float metres
	SITE_PITCH_QUARTER_WIDTH = 0x1A8EC06,  // float metres
	SITE_PITCH_QUARTER_LENGTH = 0x1A8EC28, // float metres
	SITE_STADIUM_INDEX = 0x4374A8F,        // stadium record (venue and fixture): table index
	SITE_STADIUM_UID = 0x4374AB6,          //   the stadium's unique ID
	SITE_TEXT = 0x1939ED4,                 // every string; called from SITE_STADIUM_NAME_CALLER
	SITE_STADIUM_NAME_CALLER = 0x4374ADD,  //   for the stadium's name
	SITE_VENUE_UID = 0x456AC04,            // the stadium's unique ID again (VENUE+0x00)
	SITE_SEAT_COLOUR_0 = 0x456AB32,        // ABGR
	SITE_SEAT_COLOUR_1 = 0x456AB67,        // ABGR, a second colour
	SITE_STAND_0_CAPACITY = 0x45844CE,     // the first stand record's capacity
	SITE_OCCUPANCY_CALLER = 0x43D0262,     // the call writing the per-section occupancy
	SITE_OCCUPANCY_FIRST = 0x43D075E,      //   its section 0 (sections 1-30 have members)
	SITE_OCCUPANCY_LAST = 0x43D0262,       //   its section 31
	SITE_SECTION_SEATS_FIRST = 0x43D04D4,  // u16 per section: section 0
	SITE_SECTION_SEATS_LAST = 0x43D0724,   //   section 31
	SITE_FIXTURE_ATTENDANCE = 0x19BEF71,
	SITE_FIXTURE_AWAY_FANS = 0x19BEF92,
	SITE_FIXTURE_EXTRA_HOME_FANS = 0x11758B64,
	SITE_FIXTURE_EXTRA_AWAY_FANS = 0x11758B77,
};

// Return addresses inside the tactic serialiser (sub_141A896D0, a ~7 KB object). Every manager and
// backroom entry carries one; the managers' are the teams' real tactics.
// Parts whose meaning is unknown are named by their member offset.
enum TacticPart {
	SITE_TACTIC_TAKERS = 0x1A896E9,           // sub_141C27F40: captains and set-piece takers
	SITE_TACTIC_PART_4240 = 0x1A89700,        // sub_141B91E20
	SITE_TACTIC_PLAN_0 = 0x1A8971A,           // sub_141BFFCD0: the active tactic
	SITE_TACTIC_PLAN_1 = 0x1A89734,           // sub_141BFFCD0 again: a second tactic
	SITE_TACTIC_SET_PIECES = 0x1A8974B,       // sub_141BA2390: SET_PIECE_ROUTINE_TEMPLATE objects
	SITE_TACTIC_SLOTS_6920_FIRST = 0x1A897B4, // 4 x sub_1419A6D50 (+6920..+6932)
	SITE_TACTIC_SLOTS_6920_LAST = 0x1A897F9,
	SITE_TACTIC_FORMATION_FIRST = 0x1A89810, // 25 x sub_1419D5650 (+6816..+6916): slot positions
	SITE_TACTIC_FORMATION_LAST = 0x1A89A4F,
	SITE_TACTIC_START = 0x1A896D0, // the serialiser itself (function start)
	SITE_TACTIC_END = 0x1A89B23,   // and end: direct writes lie in between
};

// Fields of a tactic plan (sub_141BFFCD0), found by changing one setting at a time in the game.
// Position masks hold one bit per position code (labels.c) plus a side
// bit (20 left, 21 right); role masks one bit per role ID plus a duty bit (21 defend, 22 support,
// 23 attack, 34 automatic).
enum TacticField {
	SITE_TACTIC_MENTALITY = 0x11EE0E68, // u8: 1 very defensive .. 7 very attacking
	SITE_TACTIC_TEAM_INSTRUCTIONS =
		0x11EEA6E2, // 3 x u32 bit set (word:bit 0:11 run at defence, 2:5 get stuck in)
	SITE_TACTIC_SLOT_POSITION = 0x1191B4E4,     // u32 position mask of a formation slot
	SITE_TACTIC_SLOT_ROLE = 0x1191B4FC,         // u64 role mask of that slot
	SITE_TACTIC_INSTRUCTIONS_ROLE = 0x11916B75, // u64 role bit keying a set of player instructions
	SITE_TACTIC_INSTRUCTIONS_BITS = 0x1A0FD22,  // 2 x u32 bit set: that role's player instructions
};

// The same fields' sites in the Epic Games build, whose .arch code differs (read from an Epic
// `--full` export). The instruction bits are written from .sdata, so their site is shared.
enum EpicTacticField {
	SITE_EPIC_TACTIC_MENTALITY = 0x11EB98E0,
	SITE_EPIC_TACTIC_TEAM_INSTRUCTIONS = 0x11EC2932,
	SITE_EPIC_TACTIC_SLOT_POSITION = 0x1198BD07,
	SITE_EPIC_TACTIC_SLOT_ROLE = 0x1198BD1F,
	SITE_EPIC_TACTIC_INSTRUCTIONS_ROLE = 0x119895E5,
};
