// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// fm24ctl: drives the harness DLL inside the running fm.exe, injecting it when needed.
//
//   fm24ctl run <input.bin|.json> [seeds] [output.csv] [--recompute]
//       [--threads N] [--profile <samples.txt>]
//       seeds: "keep" (the input's own seed), "42", "1-1000", or "1,5,10-20"
//       (default "keep"); output defaults to the input with a .csv extension
//   fm24ctl get-next-fixture <output.json|.bin> [--seed N]
//   fm24ctl export <input.bin> <output.json>
//   fm24ctl encode <input.json> <output.bin>
//   fm24ctl rebuild <input.json> <output.json|.bin> [--as-built]
//       recompute values derived from edited ratings (--as-built: the game's own build with the
//       config's ratings, other edits ignored)
//   fm24ctl sensitivity <output.csv> [--player NAME|UID] [--values 1-20] [--seed N]
//       which derived values change when each rating of the next fixture's players changes
//
// fm24ctl.exe, the DLL, the job file and the result file all live in the same directory.

#include "commands.h"

#include <stdio.h>
#include <wchar.h>


static const char usage[] =
	"usage:\n"
	"  fm24ctl run <input.bin|.json> [seeds] [output.csv] [--recompute]\n"
	"      [--threads N] [--profile <samples.txt>]\n"
	"  fm24ctl get-next-fixture <output.json|.bin> [--seed N]\n"
	"  fm24ctl export <input.bin> <output.json>\n"
	"  fm24ctl encode <input.json> <output.bin>\n"
	"  fm24ctl rebuild <input.json> <output.json|.bin> [--as-built]\n"
	"  fm24ctl sensitivity <output.csv> [--player NAME|UID] [--values 1-20] [--seed N]\n";

int wmain(int argc, wchar_t **argv);

int wmain(int argc, wchar_t **argv) {
	const wchar_t *command = argc >= 2 ? argv[1] : L"";
	static const struct {
		const wchar_t *name;
		int (*run)(int argc, wchar_t **argv);
	} commands[] = {
		{L"run", commands_run},
		{L"get-next-fixture", commands_getNextFixture},
		{L"export", commands_export},
		{L"encode", commands_encode},
		{L"rebuild", commands_rebuild},
		{L"sensitivity", commands_sensitivity},
	};
	for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
		if (wcscmp(command, commands[i].name) == 0) {
			return commands[i].run(argc, argv);
		}
	}
	fputs(usage, stderr);
	return 2;
}
