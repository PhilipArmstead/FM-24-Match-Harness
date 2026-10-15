// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "fixture_data.h"

#include <wchar.h>


enum { FIXTURE_EXPORT_FILENAME_LENGTH = 256 };

bool fixtureExport_filename(const FixtureData *data, uint64_t timestamp, wchar_t *out, size_t size);
