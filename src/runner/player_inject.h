// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Puts a player's database values ("fm24-player/1", from `fm24ctl get-player`) into one player
// block of a match config, in place of that player's own. Only the values of the block's named
// fields are changed (and its "player" annotation), so the config keeps its structure: the block's
// derived values (ability, role ratings) stay those of the player it replaces.
#pragma once

#include "buffer.h"

#include <stdbool.h>
#include <stddef.h>


typedef struct PlayerInjection {
	size_t replaced;  // named values changed or confirmed
	size_t uidCopies; // unnamed copies of the block's unique ID replaced along with person_uid
	size_t missing;   // player file fields the block does not name (e.g. person_uid of a filler)
	char missingNames[256]; // the first of them, comma-separated
} PlayerInjection;

// `block` names the player block ("home.player[3]"). Fails (with a message in `error`) if the block
// is missing, a value's type differs, or (unless `allowDuplicate`) another player block already has
// the player's unique ID. `out` receives the config with every other byte unchanged.
bool playerInject_apply(
	const char *config,
	size_t configLength,
	const char *player,
	size_t playerLength,
	const char *block,
	bool allowDuplicate,
	ByteBuffer *out,
	PlayerInjection *result,
	char *error,
	size_t errorSize
);
