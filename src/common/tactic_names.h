// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

enum {
	TACTIC_LEFT_BIT = 20,
	TACTIC_RIGHT_BIT = 21,
	TACTIC_DEFEND_BIT = 21,
	TACTIC_SUPPORT_BIT = 22,
	TACTIC_ATTACK_BIT = 23,
	TACTIC_AUTOMATIC_BIT = 34,
};

// IDs are bit indices, not mask values. NULL means the ID has not been identified.
const char *tactic_roleName(unsigned bit);
const char *tactic_positionName(unsigned bit);
