// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// The runner's commands; each returns the process exit code (0 ok, 1 failed, 2 usage).
#pragma once

#include <wchar.h>


unsigned long commands_defaultThreads(void);
int commands_run(int argc, wchar_t **argv);
int commands_getNextFixture(int argc, wchar_t **argv);
int commands_encode(int argc, wchar_t **argv);
int commands_replaceAway(int argc, wchar_t **argv);
int commands_probe(int argc, wchar_t **argv); // probe.c
