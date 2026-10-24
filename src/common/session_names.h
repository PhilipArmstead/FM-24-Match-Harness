// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Training session names as the game resolves them (sub_14390A730): training_sessions.jsb's names,
// plus the engine's own sessions and aliases. Matching ignores case, as the game's does.
bool session_id(const char *name, uint16_t *id);
// training_sessions.jsb's name for an ID, or NULL.
const char *session_name(uint16_t id);
