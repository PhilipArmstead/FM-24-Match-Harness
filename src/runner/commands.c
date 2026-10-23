// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "commands.h"

#include "client.h"
#include "files.h"
#include "match_config.h"
#include "paths.h"
#include "player_inject.h"
#include "protocol.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>


enum {
	EXIT_OK = 0,
	EXIT_FAILED = 1,
	EXIT_USAGE = 2,
	UTF8_PATH_LENGTH = HARNESS_PATH_LENGTH * 3,
	JOB_LENGTH = 4 * UTF8_PATH_LENGTH + 512,
	SUMMARY_SHOWN = 10, // how many individual results `run` prints
};

typedef struct Path {
	wchar_t wide[HARNESS_PATH_LENGTH];
	char utf8[UTF8_PATH_LENGTH];
} Path;

typedef struct Totals {
	uint64_t runs;
	uint64_t homeWins;
	uint64_t draws;
	uint64_t awayWins;
	uint64_t homeGoals;
	uint64_t awayGoals;
} Totals;

typedef struct RunOptions {
	const wchar_t *input;
	const wchar_t *seeds;  // default "keep"
	const wchar_t *output; // default: the input with a .csv extension
	unsigned long threads;
	const wchar_t *profile;
	bool detailed;
	bool humanControl;
	bool keepBuild;
} RunOptions;

static bool absolutePath(const wchar_t *argument, Path *out);
static bool jobInputPath(Path *out, const wchar_t *extension);
static void removeJobInput(const Path *path);
static int submitAndReport(const char *job);
static bool encodeConfig(const Path *json, ByteBuffer *bytes, uint32_t teams[2]);
static bool encodeToFile(const Path *json, const Path *output, size_t *size, uint32_t teams[2]);
static void addResult(Totals *totals, unsigned seed, unsigned home, unsigned away);
static bool parseCsvNumbers(const char *line, unsigned long *values, size_t count);
static int printSummary(const Path *csvPath);
static bool prepareInput(Path *input, uint32_t teams[2], bool *encoded);
static bool isSeedList(const wchar_t *text);
static bool parseRunOption(int argc, wchar_t **argv, int *i, RunOptions *options);
static bool placeRunArgument(const wchar_t *argument, RunOptions *options);
static bool parseRunArguments(int argc, wchar_t **argv, RunOptions *options);
static bool defaultOutput(const Path *input, Path *output);
static void printDetailedPaths(const Path *output);
static bool parseSeedValue(const wchar_t *value, char *seed, size_t size);
static bool parseFixtureOptions(int argc, wchar_t **argv, char *seed, size_t size, bool *full);
static bool parsePersonId(const wchar_t *text, char *out, size_t size);
static bool parsePlayerBlock(const wchar_t *text, char *out, size_t size);
static int fetchPlayer(const char *person, const Path *output, char *result, size_t resultSize);

int commands_run(int argc, wchar_t **argv) {
	return commands_runWithProgress(argc, argv, 0);
}

int commands_runWithProgress(int argc, wchar_t **argv, uintptr_t progressWindow) {
	RunOptions options;
	if (!parseRunArguments(argc, argv, &options)) {
		fprintf(
			stderr,
			"usage: fm24ctl run <input.bin|.json> [seeds] [output.csv] [--threads N]"
			" [--profile <samples.txt>] [--detailed] [--human-control] [--keep-build]\n"
		);
		return EXIT_USAGE;
	}
	Path input;
	Path output;
	Path profile = {0};
	char seeds[256];
	uint32_t teams[2] = {0};
	bool encoded = false;
	if (!absolutePath(options.input, &input) ||
			!(options.output ? absolutePath(options.output, &output) : defaultOutput(&input, &output)) ||
			(options.profile && !absolutePath(options.profile, &profile)) ||
			!paths_toUtf8(options.seeds, seeds, sizeof seeds) || !prepareInput(&input, teams, &encoded)) {
		return EXIT_FAILED;
	}
	printf("results: %s\n", output.utf8);
	char job[JOB_LENGTH];
	(void)snprintf(
		job,
		sizeof job,
		"command=run\ninput=%s\nseeds=%s\noutput=%s\nthreads=%lu\nprofile=%s\ndetailed=%d\n"
		"human_control=%d\nkeep_build=%d\nhome_team=%.0u\naway_team=%.0u\nprogress_window=%llu\n",
		input.utf8,
		seeds,
		output.utf8,
		options.threads,
		profile.utf8,
		options.detailed,
		options.humanControl,
		options.keepBuild,
		teams[0],
		teams[1],
		(unsigned long long)progressWindow
	);
	printf("job submitted; waiting...\n");
	char result[CLIENT_RESULT_LENGTH];
	bool submitted = client_submit(job, CLIENT_LONG_TIMEOUT_MS, result, sizeof result);
	if (encoded) {
		removeJobInput(&input);
	}
	if (!submitted) {
		return EXIT_FAILED;
	}
	printf("%s\n", result);
	if (!client_succeeded(result)) {
		return EXIT_FAILED;
	}
	if (options.detailed) {
		printDetailedPaths(&output);
	}
	return printSummary(&output);
}

int commands_getNextFixture(int argc, wchar_t **argv) {
	char seed[32] = "random";
	bool full = false;
	if (argc < 3 || !parseFixtureOptions(argc, argv, seed, sizeof seed, &full)) {
		fprintf(stderr, "usage: fm24ctl get-next-fixture <output.json|.bin> [--seed N] [--full]\n");
		return EXIT_USAGE;
	}
	Path output;
	if (!absolutePath(argv[2], &output)) {
		return EXIT_FAILED;
	}
	char job[JOB_LENGTH];
	(void)snprintf(
		job, sizeof job, "command=build_next\noutput=%s\nseed=%s\nfull=%d\n", output.utf8, seed, full
	);
	return submitAndReport(job);
}

int commands_replaceTeam(int argc, wchar_t **argv) {
	char result[CLIENT_RESULT_LENGTH];
	return commands_replaceTeamWithResult(argc, argv, result, sizeof result);
}

int commands_replaceTeamWithResult(int argc, wchar_t **argv, char *result, size_t resultSize) {
	bool home = argc > 1 && wcscmp(argv[1], L"replace-home") == 0;
	bool full = argc == 6 && wcscmp(argv[5], L"--full") == 0;
	if ((argc != 5 && !full) || !files_hasExtension(argv[2], L".json") ||
			!files_hasExtension(argv[4], L".json")) {
		(void)snprintf(result, resultSize, "Team replacement requires a .json fixture and output.");
		fprintf(
			stderr,
			"usage: fm24ctl replace-%s <input.json> <team ID or club name> <output.json> [--full]\n",
			home ? "home" : "away"
		);
		return EXIT_USAGE;
	}
	Path input;
	Path output;
	Path bin;
	char team[256];
	size_t size = 0;
	uint32_t teams[2] = {0};
	if (!paths_toUtf8(argv[3], team, sizeof team) || !team[0] || strpbrk(team, "\r\n") ||
			wcsspn(argv[3], L" \t") == wcslen(argv[3])) {
		(void)snprintf(
			result, resultSize, "Enter a team ID or club short name (up to 255 UTF-8 bytes)."
		);
		fprintf(stderr, "%s\n", result);
		return EXIT_FAILED;
	}
	(void)snprintf(result, resultSize, "Cannot prepare replacement; see the console for details.");
	if (!absolutePath(argv[2], &input) || !absolutePath(argv[4], &output) ||
			!jobInputPath(&bin, L".bin")) {
		return EXIT_FAILED;
	}
	if (!encodeToFile(&input, &bin, &size, teams)) {
		removeJobInput(&bin);
		return EXIT_FAILED;
	}
	char job[JOB_LENGTH];
	(void)snprintf(
		job,
		sizeof job,
		"command=replace_%s\ninput=%s\nteam=%s\nhome_team=%.0u\naway_team=%.0u\noutput=%s\nfull=%d\n",
		home ? "home" : "away",
		bin.utf8,
		team,
		teams[0],
		teams[1],
		output.utf8,
		full
	);
	(void)snprintf(
		result,
		resultSize,
		"Cannot replace team; FM24 must be running and idle. See the console and harness.log."
	);
	bool submitted = client_submit(job, CLIENT_SHORT_TIMEOUT_MS, result, resultSize);
	removeJobInput(&bin);
	if (!submitted) {
		return EXIT_FAILED;
	}
	printf("%s\n", result);
	return client_succeeded(result) ? EXIT_OK : EXIT_FAILED;
}

int commands_listTeams(int argc, wchar_t **argv) {
	char result[CLIENT_RESULT_LENGTH];
	return commands_listTeamsWithResult(argc, argv, result, sizeof result);
}

int commands_listTeamsWithResult(int argc, wchar_t **argv, char *result, size_t resultSize) {
	if (argc != 3) {
		(void)snprintf(result, resultSize, "List teams requires an output file.");
		fprintf(stderr, "usage: fm24ctl list-teams <output.txt>\n");
		return EXIT_USAGE;
	}
	Path output;
	if (!absolutePath(argv[2], &output)) {
		(void)snprintf(result, resultSize, "Cannot resolve the team list path.");
		return EXIT_FAILED;
	}
	char job[JOB_LENGTH];
	(void)snprintf(job, sizeof job, "command=list_teams\noutput=%s\n", output.utf8);
	(void)snprintf(
		result, resultSize, "Cannot list teams; FM24 must be running and idle. See harness.log."
	);
	if (!client_submit(job, CLIENT_SHORT_TIMEOUT_MS, result, resultSize)) {
		return EXIT_FAILED;
	}
	printf("%s\n", result);
	return client_succeeded(result) ? EXIT_OK : EXIT_FAILED;
}

int commands_listPlayers(int argc, wchar_t **argv) {
	char result[CLIENT_RESULT_LENGTH];
	return commands_listPlayersWithResult(argc, argv, result, sizeof result);
}

int commands_listPlayersWithResult(int argc, wchar_t **argv, char *result, size_t resultSize) {
	if (argc != 3) {
		(void)snprintf(result, resultSize, "List players requires an output file.");
		fprintf(stderr, "usage: fm24ctl list-players <output.txt>\n");
		return EXIT_USAGE;
	}
	Path output;
	if (!absolutePath(argv[2], &output)) {
		(void)snprintf(result, resultSize, "Cannot resolve the player list path.");
		return EXIT_FAILED;
	}
	char job[JOB_LENGTH];
	(void)snprintf(job, sizeof job, "command=list_players\noutput=%s\n", output.utf8);
	(void)snprintf(
		result, resultSize, "Cannot list players; FM24 must be running and idle. See harness.log."
	);
	if (!client_submit(job, CLIENT_SHORT_TIMEOUT_MS, result, resultSize)) {
		return EXIT_FAILED;
	}
	printf("%s\n", result);
	return client_succeeded(result) ? EXIT_OK : EXIT_FAILED;
}

int commands_getPlayer(int argc, wchar_t **argv) {
	char person[16];
	Path output;
	if (argc != 4 || !parsePersonId(argv[2], person, sizeof person)) {
		fprintf(stderr, "usage: fm24ctl get-player <person unique ID> <output.json>\n");
		return EXIT_USAGE;
	}
	char result[CLIENT_RESULT_LENGTH];
	return absolutePath(argv[3], &output) ? fetchPlayer(person, &output, result, sizeof result)
																				: EXIT_FAILED;
}

int commands_develop(int argc, wchar_t **argv) {
	char person[16];
	Path output;
	char months[16];
	char seeds[256];
	// The DLL checks the month count's range and the seeds in full.
	bool valid = argc == 6 && argv[3][0] != 0 && wcsspn(argv[3], L"0123456789") == wcslen(argv[3]) &&
							 paths_toUtf8(argv[3], months, sizeof months) &&
							 parsePersonId(argv[2], person, sizeof person) && isSeedList(argv[4]) &&
							 wcscmp(argv[4], L"keep") != 0 && paths_toUtf8(argv[4], seeds, sizeof seeds) &&
							 files_hasExtension(argv[5], L".csv");
	if (!valid) {
		fprintf(stderr, "usage: fm24ctl develop <person unique ID> <months> <seeds> <output.csv>\n");
		return EXIT_USAGE;
	}
	if (!absolutePath(argv[5], &output)) {
		return EXIT_FAILED;
	}
	char job[JOB_LENGTH];
	(void)snprintf(
		job,
		sizeof job,
		"command=develop\nperson=%s\nmonths=%s\nseeds=%s\noutput=%s\n",
		person,
		months,
		seeds,
		output.utf8
	);
	char result[CLIENT_RESULT_LENGTH];
	(void)snprintf(
		result, sizeof result, "Cannot simulate; FM24 must be running and idle. See harness.log."
	);
	if (!client_submit(job, CLIENT_LONG_TIMEOUT_MS, result, sizeof result)) {
		return EXIT_FAILED;
	}
	printf("%s\n", result);
	return client_succeeded(result) ? EXIT_OK : EXIT_FAILED;
}

int commands_seedDevelopment(int argc, wchar_t **argv) {
	char person[16];
	char seed[16];
	char seeds[256] = "";
	Path output = {0};
	bool predict = argc == 6;
	// The DLL checks the seed's range and the prediction seeds in full.
	bool valid = (argc == 4 || predict) && parsePersonId(argv[2], person, sizeof person) &&
							 argv[3][0] != 0 && wcsspn(argv[3], L"0123456789") == wcslen(argv[3]) &&
							 paths_toUtf8(argv[3], seed, sizeof seed) &&
							 (!predict || (isSeedList(argv[4]) && wcscmp(argv[4], L"keep") != 0 &&
															paths_toUtf8(argv[4], seeds, sizeof seeds) &&
															files_hasExtension(argv[5], L".csv")));
	if (!valid) {
		fprintf(
			stderr, "usage: fm24ctl seed-development <person unique ID> <seed> [<seeds> <output.csv>]\n"
		);
		return EXIT_USAGE;
	}
	if (predict && !absolutePath(argv[5], &output)) {
		return EXIT_FAILED;
	}
	char job[JOB_LENGTH];
	int length =
		snprintf(job, sizeof job, "command=seed_development\nperson=%s\nseed=%s\n", person, seed);
	if (predict && length > 0 && (size_t)length < sizeof job) {
		(void)snprintf(
			job + length, sizeof job - (size_t)length, "seeds=%s\noutput=%s\n", seeds, output.utf8
		);
	}
	char result[CLIENT_RESULT_LENGTH];
	(void)snprintf(
		result, sizeof result, "Cannot set the seed; FM24 must be running and idle. See harness.log."
	);
	if (!client_submit(job, CLIENT_SHORT_TIMEOUT_MS, result, sizeof result)) {
		return EXIT_FAILED;
	}
	printf("%s\n", result);
	return client_succeeded(result) ? EXIT_OK : EXIT_FAILED;
}

int commands_injectPlayer(int argc, wchar_t **argv) {
	char result[CLIENT_RESULT_LENGTH];
	return commands_injectPlayerWithResult(argc, argv, result, sizeof result);
}

int commands_injectPlayerWithResult(int argc, wchar_t **argv, char *result, size_t resultSize) {
	bool allowDuplicate = argc == 7 && wcscmp(argv[6], L"--allow-duplicate") == 0;
	char block[64];
	char person[16] = "";
	bool fromFile = argc >= 5 && files_hasExtension(argv[4], L".json");
	if ((argc != 6 && !allowDuplicate) || !files_hasExtension(argv[2], L".json") ||
			!files_hasExtension(argv[5], L".json") || !parsePlayerBlock(argv[3], block, sizeof block) ||
			(!fromFile && !parsePersonId(argv[4], person, sizeof person))) {
		(void)snprintf(result, resultSize, "Player injection requires a .json fixture and output.");
		fprintf(
			stderr,
			"usage: fm24ctl inject-player <input.json> <home|away>.player[N]"
			" <person unique ID|player.json> <output.json> [--allow-duplicate]\n"
		);
		return EXIT_USAGE;
	}
	Path input;
	Path output;
	Path player;
	(void)snprintf(result, resultSize, "Cannot prepare the injection; see the console for details.");
	if (!absolutePath(argv[2], &input) || !absolutePath(argv[5], &output) ||
			!(fromFile ? absolutePath(argv[4], &player) : jobInputPath(&player, L".json"))) {
		return EXIT_FAILED;
	}
	if (!fromFile) {
		int fetched = fetchPlayer(person, &player, result, resultSize);
		if (fetched != EXIT_OK) {
			removeJobInput(&player);
			return fetched;
		}
	}
	ByteBuffer config = {0};
	ByteBuffer values = {0};
	ByteBuffer injected = {0};
	PlayerInjection injection;
	char error[512] = "";
	int status = EXIT_FAILED;
	if (!files_read(input.wide, &config)) {
		(void)snprintf(result, resultSize, "cannot read %s", input.utf8);
	} else if (!files_read(player.wide, &values)) {
		(void)snprintf(result, resultSize, "cannot read %s", player.utf8);
	} else if (!playerInject_apply(
							 buffer_string(&config),
							 config.length,
							 buffer_string(&values),
							 values.length,
							 block,
							 allowDuplicate,
							 &injected,
							 &injection,
							 error,
							 sizeof error
						 )) {
		(void)snprintf(result, resultSize, "error: %s", error);
	} else if (!files_write(output.wide, injected.data, injected.length)) {
		(void)snprintf(result, resultSize, "cannot write %s", output.utf8);
	} else {
		(void)snprintf(
			result,
			resultSize,
			"ok: %zu values (and %zu unique ID copies) of %s replaced; wrote %s",
			injection.replaced,
			injection.uidCopies,
			block,
			output.utf8
		);
		status = EXIT_OK;
	}
	if (status != EXIT_OK) {
		fprintf(stderr, "%s\n", result);
	} else {
		printf("%s\n", result);
		if (injection.missing) {
			printf("not in the block, left out: %s\n", injection.missingNames);
		}
	}
	if (!fromFile) {
		removeJobInput(&player);
	}
	buffer_free(&config);
	buffer_free(&values);
	buffer_free(&injected);
	return status;
}

int commands_encode(int argc, wchar_t **argv) {
	if (argc != 4) {
		fprintf(stderr, "usage: fm24ctl encode <input.json> <output.bin>\n");
		return EXIT_USAGE;
	}
	Path input;
	Path output;
	size_t size = 0;
	if (!absolutePath(argv[2], &input) || !absolutePath(argv[3], &output) ||
			!encodeToFile(&input, &output, &size, NULL)) {
		return EXIT_FAILED;
	}
	printf("wrote %zu bytes\n", size);
	return EXIT_OK;
}

// A command-line path made absolute (the DLL runs with fm.exe's working directory).
static bool absolutePath(const wchar_t *argument, Path *out) {
	bool ok = paths_absolute(argument, out->wide, HARNESS_PATH_LENGTH) &&
						paths_toUtf8(out->wide, out->utf8, sizeof out->utf8) && !strpbrk(out->utf8, "\r\n");
	if (!ok) {
		fprintf(stderr, "bad path: %ls\n", argument);
	}
	return ok;
}

// A new file next to fm24ctl.exe for one job's encoded input (or output): jobs wait in line for the
// DLL, so each needs its own (several runners, or a viewer's simulation and replacement, may be
// queued).
static bool jobInputPath(Path *out, const wchar_t *extension) {
	static volatile LONG count;
	wchar_t name[64];
	wchar_t directory[HARNESS_PATH_LENGTH];
	(void)swprintf(
		name,
		sizeof name / sizeof name[0],
		L"_job_input-%lu-%ld%ls",
		GetCurrentProcessId(),
		InterlockedIncrement(&count),
		extension
	);
	bool ok = paths_executableDirectory(directory, HARNESS_PATH_LENGTH) &&
						paths_join(out->wide, HARNESS_PATH_LENGTH, directory, name) &&
						paths_toUtf8(out->wide, out->utf8, sizeof out->utf8);
	if (!ok) {
		fprintf(stderr, "cannot resolve the working file %ls\n", name);
	}
	return ok;
}

static void removeJobInput(const Path *path) {
	if (!DeleteFileW(path->wide) && GetLastError() != ERROR_FILE_NOT_FOUND) {
		fprintf(stderr, "cannot remove %s (Windows error %lu)\n", path->utf8, GetLastError());
	}
}

// Submits a job that reports one result line, prints it, and turns it into an exit code.
static int submitAndReport(const char *job) {
	char result[CLIENT_RESULT_LENGTH];
	if (!client_submit(job, CLIENT_SHORT_TIMEOUT_MS, result, sizeof result)) {
		return EXIT_FAILED;
	}
	printf("%s\n", result);
	return client_succeeded(result) ? EXIT_OK : EXIT_FAILED;
}

// Encodes a JSON config into match bytes.
static bool encodeConfig(const Path *json, ByteBuffer *bytes, uint32_t teams[2]) {
	ByteBuffer text = {0};
	char error[256] = "";
	bool ok = files_read(json->wide, &text);
	if (!ok) {
		fprintf(stderr, "cannot read %s\n", json->utf8);
	} else if (!(ok = matchConfig_encode(
								 buffer_string(&text), text.length, bytes, teams, error, sizeof error
							 ))) {
		fprintf(stderr, "%s: %s\n", json->utf8, error);
	}
	buffer_free(&text);
	return ok;
}

static bool encodeToFile(const Path *json, const Path *output, size_t *size, uint32_t teams[2]) {
	ByteBuffer bytes = {0};
	bool ok = encodeConfig(json, &bytes, teams);
	if (ok && !(ok = files_write(output->wide, bytes.data, bytes.length))) {
		fprintf(stderr, "cannot write %s\n", output->utf8);
	}
	*size = bytes.length;
	buffer_free(&bytes);
	return ok;
}

static void addResult(Totals *totals, unsigned seed, unsigned home, unsigned away) {
	++totals->runs;
	totals->homeGoals += home;
	totals->awayGoals += away;
	totals->homeWins += home > away;
	totals->draws += home == away;
	totals->awayWins += home < away;
	if (totals->runs <= SUMMARY_SHOWN) {
		printf("  seed %u: %u-%u\n", seed, home, away);
	}
}

// The first `count` comma-separated decimal numbers of a CSV line.
static bool parseCsvNumbers(const char *line, unsigned long *values, size_t count) {
	const char *position = line;
	for (size_t i = 0; i < count; ++i) {
		if (*position < '0' || *position > '9') {
			return false;
		}
		char *end = NULL;
		errno = 0;
		values[i] = strtoul(position, &end, 10);
		if (errno != 0 || values[i] > UINT32_MAX ||
				(i + 1 < count ? *end != ',' : *end && !strchr(",\r\n", *end))) {
			return false;
		}
		position = *end == ',' ? end + 1 : end;
	}
	return true;
}

// Prints the first results and the win/draw/loss totals of a results CSV.
static int printSummary(const Path *csvPath) {
	FILE *csv = _wfopen(csvPath->wide, L"rb");
	if (!csv) {
		fprintf(stderr, "no output written; see harness.log\n");
		return EXIT_FAILED;
	}
	char line[256];
	Totals totals = {0};
	bool ok =
		fgets(line, sizeof line, csv) != NULL && strncmp(line, "seed,home_goals,away_goals,", 27) == 0;
	while (ok && fgets(line, sizeof line, csv)) {
		unsigned long values[3] = {0};
		ok = parseCsvNumbers(line, values, 3);
		if (ok) {
			addResult(&totals, (unsigned)values[0], (unsigned)values[1], (unsigned)values[2]);
		}
	}
	ok = !ferror(csv) && ok;
	ok = fclose(csv) == 0 && ok;
	if (!ok) {
		fprintf(stderr, "cannot read valid results from %s\n", csvPath->utf8);
		return EXIT_FAILED;
	}
	if (totals.runs == 0) {
		printf("0 runs completed; see harness.log\n");
		return EXIT_FAILED;
	}
	printf(
		"%" PRIu64 " runs: home %" PRIu64 " / draw %" PRIu64 " / away %" PRIu64 ", avg %.2f-%.2f\n",
		totals.runs,
		totals.homeWins,
		totals.draws,
		totals.awayWins,
		(double)totals.homeGoals / (double)totals.runs,
		(double)totals.awayGoals / (double)totals.runs
	);
	return EXIT_OK;
}

// Turns a .json config into the .bin the DLL plays, and reads its team overrides; a .bin is played
// as it is.
static bool prepareInput(Path *input, uint32_t teams[2], bool *encoded) {
	teams[0] = 0;
	teams[1] = 0;
	*encoded = false;
	if (!files_hasExtension(input->wide, L".json")) {
		return true;
	}
	Path bin;
	size_t size = 0;
	if (!jobInputPath(&bin, L".bin")) {
		return false;
	}
	if (!encodeToFile(input, &bin, &size, teams)) {
		removeJobInput(&bin);
		return false;
	}
	*input = bin;
	*encoded = true;
	return true;
}

// One worker per logical processor, less one left for the game itself.
unsigned long commands_defaultThreads(void) {
	unsigned long processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
	unsigned long threads = processors > 1 ? processors - 1 : 1;
	return threads < HARNESS_MAX_THREADS ? threads : HARNESS_MAX_THREADS;
}

// "keep", or only digits, commas and dashes ("42", "1-1000", "1,5,10-20"). The DLL checks the rest.
static bool isSeedList(const wchar_t *text) {
	if (wcscmp(text, L"keep") == 0) {
		return true;
	}
	return text[0] != 0 && wcsspn(text, L"0123456789,-") == wcslen(text);
}

// `--name [value]` at argv[*i]; advances *i past a value. False (with a message) if invalid.
static bool parseRunOption(int argc, wchar_t **argv, int *i, RunOptions *options) {
	const wchar_t *name = argv[*i];
	bool hasValue = *i + 1 < argc;
	if (wcscmp(name, L"--profile") == 0 && hasValue) {
		options->profile = argv[++*i];
	} else if (wcscmp(name, L"--detailed") == 0) {
		options->detailed = true;
	} else if (wcscmp(name, L"--human-control") == 0) {
		options->humanControl = true;
	} else if (wcscmp(name, L"--keep-build") == 0) {
		options->keepBuild = true;
	} else if (wcscmp(name, L"--threads") == 0 && hasValue) {
		wchar_t *end = NULL;
		errno = 0;
		options->threads = wcstoul(argv[++*i], &end, 10);
		if (errno || argv[*i][0] < L'0' || argv[*i][0] > L'9' || *end != 0 || options->threads < 1 ||
				options->threads > HARNESS_MAX_THREADS) {
			fprintf(stderr, "--threads must be 1-%d\n", HARNESS_MAX_THREADS);
			return false;
		}
	} else {
		fprintf(stderr, "unknown option: %ls\n", name);
		return false;
	}
	return true;
}

// The input, then optionally a seed list and/or an output file (told apart by isSeedList).
static bool placeRunArgument(const wchar_t *argument, RunOptions *options) {
	if (!options->input) {
		options->input = argument;
	} else if (!options->seeds && !options->output && isSeedList(argument)) {
		options->seeds = argument;
	} else if (!options->output) {
		options->output = argument;
	} else {
		fprintf(stderr, "unexpected argument: %ls\n", argument);
		return false;
	}
	return true;
}

// Everything after `run`; false (with a message) if something is missing or invalid.
static bool parseRunArguments(int argc, wchar_t **argv, RunOptions *options) {
	*options = (RunOptions){.threads = commands_defaultThreads()};
	for (int i = 2; i < argc; ++i) {
		bool ok = wcsncmp(argv[i], L"--", 2) == 0 ? parseRunOption(argc, argv, &i, options)
																							: placeRunArgument(argv[i], options);
		if (!ok) {
			return false;
		}
	}
	if (!options->seeds) {
		options->seeds = L"keep";
	}
	return options->input != NULL;
}

// The input path with its extension replaced by .csv ("next.json" -> "next.csv").
static bool defaultOutput(const Path *input, Path *output) {
	wchar_t path[HARNESS_PATH_LENGTH];
	wcscpy(path, input->wide);
	wchar_t *name = wcsrchr(path, L'\\');
	wchar_t *dot = wcsrchr(name ? name : path, L'.');
	if (dot) {
		*dot = 0;
	}
	if (wcslen(path) + wcslen(L".csv") >= HARNESS_PATH_LENGTH) {
		fprintf(stderr, "the default results path is too long\n");
		return false;
	}
	wcscat(path, L".csv");
	return absolutePath(path, output);
}

// The files the DLL writes for --detailed: the output path without ".csv", plus "-players.csv"
// and "-teams.csv" (see match_stats.h).
static void printDetailedPaths(const Path *output) {
	size_t length = strlen(output->utf8);
	size_t stem =
		length > 4 && _stricmp(output->utf8 + length - 4, ".csv") == 0 ? length - 4 : length;
	printf("player statistics: %.*s-players.csv\n", (int)stem, output->utf8);
	printf("team statistics: %.*s-teams.csv\n", (int)stem, output->utf8);
}

// get-next-fixture's options after the output path: `--seed N` (a decimal u32) and `--full`.
// A `--seed` value: a decimal u32, copied to `seed` as text. Prints why and returns false if not.
static bool parseSeedValue(const wchar_t *value, char *seed, size_t size) {
	wchar_t *end = NULL;
	errno = 0;
	unsigned long long number = wcstoull(value, &end, 10);
	if (errno != 0 || value[0] < L'0' || value[0] > L'9' || *end != 0 || number > UINT32_MAX) {
		fprintf(stderr, "--seed must be a number from 0 to %u\n", UINT32_MAX);
		return false;
	}
	(void)snprintf(seed, size, "%llu", number);
	return true;
}

static bool parseFixtureOptions(int argc, wchar_t **argv, char *seed, size_t size, bool *full) {
	for (int i = 3; i < argc; ++i) {
		if (wcscmp(argv[i], L"--full") == 0) {
			*full = true;
			continue;
		}
		if (wcscmp(argv[i], L"--seed") != 0 || i + 1 >= argc) {
			fprintf(stderr, "unknown option or missing value: %ls\n", argv[i]);
			return false;
		}
		if (!parseSeedValue(argv[++i], seed, size)) {
			return false;
		}
	}
	return true;
}

// A person's unique ID: a nonzero decimal u32, copied to `out` as text.
static bool parsePersonId(const wchar_t *text, char *out, size_t size) {
	wchar_t *end = NULL;
	errno = 0;
	unsigned long long id = wcstoull(text, &end, 10);
	if (errno != 0 || text[0] < L'0' || text[0] > L'9' || *end != 0 || id == 0 || id > UINT32_MAX) {
		return false;
	}
	(void)snprintf(out, size, "%llu", id);
	return true;
}

// "home.player[N]" or "away.player[N]", copied to `out`.
static bool parsePlayerBlock(const wchar_t *text, char *out, size_t size) {
	size_t length = wcslen(text);
	bool side = wcsncmp(text, L"home.player[", 12) == 0 || wcsncmp(text, L"away.player[", 12) == 0;
	if (!side || length < 14 || length >= size || text[length - 1] != L']' ||
			wcsspn(text + 12, L"0123456789") != length - 13) {
		return false;
	}
	for (size_t i = 0; i <= length; ++i) {
		out[i] = (char)text[i];
	}
	return true;
}

// Has the DLL write a player's database values ("fm24-player/1") to `output`; `result` receives
// its result line.
static int fetchPlayer(const char *person, const Path *output, char *result, size_t resultSize) {
	char job[JOB_LENGTH];
	(void)snprintf(
		job, sizeof job, "command=get_player\nperson=%s\noutput=%s\n", person, output->utf8
	);
	(void)snprintf(
		result, resultSize, "Cannot fetch the player; FM24 must be running and idle. See harness.log."
	);
	if (!client_submit(job, CLIENT_SHORT_TIMEOUT_MS, result, resultSize)) {
		return EXIT_FAILED;
	}
	printf("%s\n", result);
	return client_succeeded(result) ? EXIT_OK : EXIT_FAILED;
}
