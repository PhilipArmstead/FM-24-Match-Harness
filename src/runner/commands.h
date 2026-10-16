// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// The runner's commands; each returns the process exit code (0 ok, 1 failed, 2 usage).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>


unsigned long commands_defaultThreads(void);
int commands_run(int argc, wchar_t **argv);
int commands_runWithProgress(int argc, wchar_t **argv, uintptr_t progressWindow);
int commands_getNextFixture(int argc, wchar_t **argv);
int commands_encode(int argc, wchar_t **argv);
int commands_replaceTeam(int argc, wchar_t **argv);
// As above, also returning a failure message for the viewer.
int commands_replaceTeamWithResult(int argc, wchar_t **argv, char *result, size_t resultSize);
int commands_probe(int argc, wchar_t **argv); // probe.c
