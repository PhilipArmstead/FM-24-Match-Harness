// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// The runner's commands; each returns the process exit code (0 ok, 1 failed, 2 usage).
#pragma once

#include <wchar.h>


int commands_run(int argc, wchar_t **argv);
int commands_getNextFixture(int argc, wchar_t **argv);
int commands_export(int argc, wchar_t **argv);
int commands_encode(int argc, wchar_t **argv);
int commands_rebuild(int argc, wchar_t **argv);
int commands_sensitivity(int argc, wchar_t **argv);
