// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// File names made from people's names: lowercase ASCII letters joined by hyphens.
#pragma once

#include <stdbool.h>
#include <stddef.h>


// "Álex Moreno" -> "alex-moreno", "Tommi O'Reilly" -> "tommi-oreilly". Accented Latin letters
// (U+00C0..U+017F) lose their accents; apostrophes and full stops are dropped; anything else
// separates words. False if nothing is left (e.g. a name in another script) or `out` is too small.
bool slug_fromName(const char *utf8, char *out, size_t size);
