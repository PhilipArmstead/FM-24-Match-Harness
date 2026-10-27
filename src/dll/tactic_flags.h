// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Names of the tactic instruction bits, found by changing one setting at a time in the game.
// The table is generated (tactic_flags.c) from
// tools/fields/tactic_bits.json.
#pragma once

#include <stddef.h>


// The setting team_instructions[word] bit `bit` stands for, or NULL if unknown.
const char *tacticFlags_teamInstruction(unsigned word, unsigned bit);

// The setting player_instructions[...].bits[word] bit `bit` stands for, or NULL if unknown.
const char *tacticFlags_playerInstruction(unsigned word, unsigned bit);
