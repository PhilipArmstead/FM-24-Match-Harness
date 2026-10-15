// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fixture_data.h"

#include <wchar.h>


// JSON is read locally. Binary input is decoded by the running game's harness, never played.
bool fixtureLoad_open(const wchar_t *path, FixtureData *out, char *error, size_t errorSize);
