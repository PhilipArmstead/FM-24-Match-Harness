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
	SITE_PLAYER_FIRST = 0x1B3C719,   // first write of each player (MATCH_PLAYER__serialize)
	SITE_PLAYER_UID = 0x437495A,     // person unique ID (also other IDs; the first DB match wins)
	SITE_PLAYER_RATINGS = 0x1A808F5, // 69 bytes: 15 positions (1-20) + 54 attributes (1-100)
	SITE_BACKROOM_FIRST = 0x1A83EEB, // first write of each backroom/manager entry
};
