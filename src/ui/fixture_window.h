// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdbool.h>
#include <wchar.h>


// Opens the viewer, or asynchronously loads into its existing window without losing the old
// fixture on failure. Returns 0 on success (or when a load starts); errors appear in the viewer.
int fixtureWindow_open(const wchar_t *path, bool temporary);
