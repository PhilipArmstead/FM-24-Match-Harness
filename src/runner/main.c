// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// fm24ctl: drives the harness DLL inside the running fm.exe, injecting it when needed.
//
//   fm24ctl
//       exports the next fixture to the OS temp directory and opens the viewer
//   fm24ctl run <input.bin|.json> [seeds] [output.csv] [--threads N]
//       [--profile <samples.txt>] [--detailed] [--human-control] [--keep-build]
//       seeds: "keep" (the input's own seed), "42", "1-1000", or "1,5,10-20"
//       (default "keep"); output defaults to the input with a .csv extension
//   fm24ctl get-next-fixture <output.json|.bin> [--seed N] [--full]
//   fm24ctl encode <input.json> <output.bin>
//   fm24ctl replace-away <input.json> <team ID or club name> <output.json> [--full]
//   fm24ctl replace-home <input.json> <team ID or club name> <output.json> [--full]
//       the input rebuilt against another away team, keeping its other edits
//   fm24ctl get-player <person unique ID> <output.json>
//       a player's database values, as a config's player block has them
//   fm24ctl get-development <person unique ID> [output.json]
//       a player's development inputs, as an editable "fm24-development/1" file
//   fm24ctl develop <person unique ID|development.json> <months> <seeds> <output.csv>
//       [--per-seed-breakdown]
//       the player's monthly training and development, simulated in the game (reload afterwards),
//       averaged over the seeds unless broken down; a development file's values are applied first
//       and put back afterwards
//   fm24ctl seed-development <person unique ID> <seed> [<seeds> <output.csv>]
//       the game's own next development of that player draws from `seed`, as develop's does;
//       with seeds, develop's simulation of that development is run first, at the same moment
//   fm24ctl inject-player <input.json> <home|away>.player[N] <person unique ID|player.json>
//       <output.json> [--allow-duplicate]
//       the input with that player block's database values replaced by another player's
//   fm24ctl probe [path to fm.exe]
//       where another build of fm.exe has the code the harness uses (writes probe.txt)
//
// fm24ctl.exe, the DLL, the job file and the result file all live in the same directory.

#include "commands.h"
#include "files.h"
#include "fixture_window.h"
#include "protocol.h"

#include <stdio.h>
#include <wchar.h>
#include <windows.h>


static const char usage[] =
	"usage:\n"
	"  fm24ctl  (export and view the next fixture)\n"
	"  fm24ctl <fixture.bin|.json>  (open the native fixture viewer)\n"
	"  fm24ctl run <input.bin|.json> [seeds] [output.csv] [--threads N]\n"
	"      [--profile <samples.txt>] [--detailed] [--human-control] [--keep-build]\n"
	"  fm24ctl get-next-fixture <output.json|.bin> [--seed N] [--full]\n"
	"  fm24ctl encode <input.json> <output.bin>\n"
	"  fm24ctl replace-away <input.json> <team ID or club name> <output.json> [--full]\n"
	"  fm24ctl replace-home <input.json> <team ID or club name> <output.json> [--full]\n"
	"  fm24ctl list-teams <output.txt>\n"
	"  fm24ctl list-players <output.txt>\n"
	"  fm24ctl get-player <person unique ID> <output.json>\n"
	"  fm24ctl get-development <person unique ID> [output.json]\n"
	"  fm24ctl develop <person unique ID|development.json> <months> <seeds> <output.csv>\n"
	"      [--per-seed-breakdown]\n"
	"  fm24ctl seed-development <person unique ID> <seed> [<seeds> <output.csv>]\n"
	"  fm24ctl inject-player <input.json> <home|away>.player[N]\n"
	"      <person unique ID|player.json> <output.json> [--allow-duplicate]\n"
	"  fm24ctl probe [path to fm.exe]\n";

int wmain(int argc, wchar_t **argv);

int wmain(int argc, wchar_t **argv) {
	if (argc == 1) {
		wchar_t path[HARNESS_PATH_LENGTH];
		static const wchar_t filename[] = L"temp-fixture.json";
		size_t filenameLength = sizeof filename / sizeof filename[0];
		DWORD length = GetTempPathW(HARNESS_PATH_LENGTH, path);
		if (!length) {
			fprintf(stderr, "cannot resolve temporary directory (Windows error %lu)\n", GetLastError());
			return 1;
		}
		if (length > HARNESS_PATH_LENGTH - filenameLength) {
			fputs("temporary fixture path is too long\n", stderr);
			return 1;
		}
		wmemcpy(path + length, filename, filenameLength);
		wchar_t *exportArgs[] = {argv[0], L"get-next-fixture", path, NULL};
		int result = commands_getNextFixture(3, exportArgs);
		return result ? result : fixtureWindow_open(path, true);
	}
	const wchar_t *command = argc >= 2 ? argv[1] : L"";
	if (argc == 2 &&
			(files_hasExtension(command, L".bin") || files_hasExtension(command, L".json"))) {
		return fixtureWindow_open(command, false);
	}
	static const struct {
		const wchar_t *name;
		int (*run)(int argc, wchar_t **argv);
	} commands[] = {
		{L"run", commands_run},
		{L"get-next-fixture", commands_getNextFixture},
		{L"encode", commands_encode},
		{L"replace-away", commands_replaceTeam},
		{L"replace-home", commands_replaceTeam},
		{L"list-teams", commands_listTeams},
		{L"list-players", commands_listPlayers},
		{L"get-player", commands_getPlayer},
		{L"develop", commands_develop},
		{L"get-development", commands_getDevelopment},
		{L"seed-development", commands_seedDevelopment},
		{L"inject-player", commands_injectPlayer},
		{L"probe", commands_probe},
	};
	for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
		if (wcscmp(command, commands[i].name) == 0) {
			return commands[i].run(argc, argv);
		}
	}
	fputs(usage, stderr);
	return 2;
}
