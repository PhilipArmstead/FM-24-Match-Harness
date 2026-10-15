// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tactic_names.h"

#include <stddef.h>


const char *tactic_roleName(unsigned bit) {
	static const char *const names[] = {
		"goalkeeper",
		"central_defender",
		"full_back",
		"wing_back",
		"defensive_midfielder",
		"central_midfielder",
		"wide_midfielder",
		"winger",
		NULL,
		"attacking_midfielder",
		"deep_lying_forward",
		"advanced_forward",
		"sweeper_keeper",
		NULL,
		"libero",
		"deep_lying_playmaker",
		"box_to_box_midfielder",
		"advanced_playmaker",
		"target_man",
		"poacher",
		"complete_forward",
		NULL,
		NULL,
		NULL,
		"ball_playing_defender",
		NULL,
		NULL,
		"inside_forward",
		"ball_winning_midfielder",
		"no_nonsense_centre_back",
		"defensive_winger",
		"pressing_forward",
		"trequartista",
		"anchor_man",
		NULL,
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
	return bit < sizeof names / sizeof names[0] ? names[bit] : NULL;
}

const char *tactic_positionName(unsigned bit) {
	static const char *const names[] = {
		"GK",
		NULL,
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
	return bit < sizeof names / sizeof names[0] ? names[bit] : NULL;
}
