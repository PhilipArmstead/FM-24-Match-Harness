// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "session_names.h"

#include <stddef.h>
#include <string.h>


// The first entry for an ID is its name; later ones are aliases.
static const struct {
	uint16_t id;
	const char *name;
} sessions[] = {
	{0, "Team Overall"},
	{1, "Team Outfield"},
	{2, "Team Attacking"},
	{3, "Team Possession"},
	{4, "Team Defending"},
	{5, "Team Tactical"},
	{6, "Team Physical"},
	{7, "Physical Endurance"},
	{7, "Team Endurance"},
	{8, "Physical Resistance"},
	{8, "Team Resistance"},
	{9, "Physical Quickness"},
	{9, "Team Quickness"},
	{10, "Physical Recovery"},
	{10, "Team Recovery"},
	{11, "Team Rest"},
	{12, "Team Travel"},
	{13, "Team Match/Rest"},
	{14, "Team Match/Match Practice"},
	{15, "Amateur N/A"},
	{16, "Team Goalkeeping"},
	{100, "Match Prep Match Tactics"},
	{101, "Match Prep Match Practice"},
	{101, "Match Practice"},
	{102, "Match Prep Match Review Analysis"},
	{103, "Match Focus"},
	{200, "Unit Attacking Wings"},
	{201, "Unit Attacking Patient"},
	{202, "Unit Attacking Direct"},
	{203, "Unit Defending Engaged"},
	{204, "Unit Defending Unengaged"},
	{205, "Unit Defending Wide"},
	{206, "Unit Tactical Attacking Shadow Play"},
	{207, "Unit Tactical Defensive Shadow Play"},
	{208, "Unit Technical Chance Conversion"},
	{209, "Unit Technical Chance Creation"},
	{210, "Unit Technical Ball Retention"},
	{211, "Unit Technical Ball Distribution"},
	{212, "Unit Technical Transition - Press"},
	{213, "Unit Technical Transition - Restrict"},
	{214, "Unit Technical Ground Defence"},
	{214, "Unit Technical Ground Defense"},
	{215, "Unit Technical Aerial Defence"},
	{215, "Unit Technical Aerial Defense"},
	{216, "Unit Goalkeeping Shot Stopping"},
	{217, "Unit Goalkeeping Handling"},
	{218, "Unit Goalkeeping One On Ones"},
	{219, "Unit Goalkeeping Distribution"},
	{220, "Unit Attacking Overlap"},
	{221, "Unit Defending From the Front"},
	{222, "Unit Technical Play From the Back"},
	{300, "Set Pieces Penalty Taking"},
	{301, "Set Pieces Routines"},
	{400, "Extra Curricular Community Outreach"},
	{400, "Community Outreach"},
	{401, "Extra Curricular Team Bonding"},
	{401, "Team Bonding"},
};

enum { SESSION_COUNT = sizeof sessions / sizeof sessions[0] };

static bool sameIgnoringCase(const char *a, const char *b) {
	for (; *a && *b; ++a, ++b) {
		char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a;
		char y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
		if (x != y) {
			return false;
		}
	}
	return *a == *b;
}

bool session_id(const char *name, uint16_t *id) {
	for (size_t i = 0; i < SESSION_COUNT; ++i) {
		if (sameIgnoringCase(name, sessions[i].name)) {
			*id = sessions[i].id;
			return true;
		}
	}
	return false;
}

const char *session_name(uint16_t id) {
	for (size_t i = 0; i < SESSION_COUNT; ++i) {
		if (sessions[i].id == id) {
			return sessions[i].name;
		}
	}
	return NULL;
}
