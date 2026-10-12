// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "commands.h"

#include "client.h"
#include "files.h"
#include "match_config.h"
#include "paths.h"
#include "protocol.h"

#include <errno.h>
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

// A command-line path made absolute (the DLL runs with fm.exe's working directory).
static bool absolutePath(const wchar_t *argument, Path *out) {
	bool ok = paths_absolute(argument, out->wide, HARNESS_PATH_LENGTH) &&
						paths_toUtf8(out->wide, out->utf8, sizeof out->utf8);
	if (!ok) {
		fprintf(stderr, "bad path: %ls\n", argument);
	}
	return ok;
}

// A working file next to fm24ctl.exe.
static bool workingPath(const wchar_t *name, Path *out) {
	wchar_t directory[HARNESS_PATH_LENGTH];
	return paths_executableDirectory(directory, HARNESS_PATH_LENGTH) &&
				 paths_join(out->wide, HARNESS_PATH_LENGTH, directory, name) &&
				 paths_toUtf8(out->wide, out->utf8, sizeof out->utf8);
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

// Encodes a JSON config into match bytes; with `records`, also returns its write records.
static bool encodeConfig(const Path *json, ByteBuffer *bytes, ConfigRecords *records) {
	ByteBuffer text = {0};
	char error[256] = "";
	bool ok = files_read(json->wide, &text);
	if (!ok) {
		fprintf(stderr, "cannot read %s\n", json->utf8);
	} else if (!(ok = matchConfig_encode(
								 buffer_string(&text), text.length, bytes, records, error, sizeof error
							 ))) {
		fprintf(stderr, "%s: %s\n", json->utf8, error);
	}
	buffer_free(&text);
	return ok;
}

static bool encodeToFile(const Path *json, const Path *output, size_t *size) {
	ByteBuffer bytes = {0};
	bool ok = encodeConfig(json, &bytes, NULL);
	if (ok && !(ok = files_write(output->wide, bytes.data, bytes.length))) {
		fprintf(stderr, "cannot write %s\n", output->utf8);
	}
	*size = bytes.length;
	buffer_free(&bytes);
	return ok;
}

static bool writeRecords(const Path *path, const ConfigRecords *records) {
	ByteBuffer text = {0};
	bool ok = true;
	for (size_t i = 0; ok && i < records->count; ++i) {
		ok = buffer_appendFormat(&text, "%s %zu\n", records->items[i].site, records->items[i].length);
	}
	ok = ok && files_write(path->wide, text.data, text.length);
	buffer_free(&text);
	return ok;
}

// Recomputes what the game derives from a config's edited ratings: the config's bytes and its write
// records (one "site length" line each) go next to fm24ctl.exe, so the DLL can compare field by
// field.
static bool rebuildConfig(const Path *json, const Path *output, char *result, size_t resultSize) {
	Path bin;
	Path recordsPath;
	ByteBuffer bytes = {0};
	ConfigRecords records = {0};
	bool ok = workingPath(L"_rebuild_input.bin", &bin) &&
						workingPath(L"_rebuild_input.records", &recordsPath) &&
						encodeConfig(json, &bytes, &records);
	if (ok && !(ok = files_write(bin.wide, bytes.data, bytes.length) &&
									 writeRecords(&recordsPath, &records))) {
		fprintf(stderr, "cannot write the rebuild inputs\n");
	}
	buffer_free(&bytes);
	matchConfig_freeRecords(&records);
	if (!ok) {
		return false;
	}
	char job[JOB_LENGTH];
	(void)snprintf(
		job,
		sizeof job,
		"command=rebuild\ninput=%s\nrecords=%s\noutput=%s\n",
		bin.utf8,
		recordsPath.utf8,
		output->utf8
	);
	return client_submit(job, CLIENT_SHORT_TIMEOUT_MS, result, resultSize);
}

typedef struct Totals {
	unsigned runs;
	unsigned homeWins;
	unsigned draws;
	unsigned awayWins;
	unsigned homeGoals;
	unsigned awayGoals;
} Totals;

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
		char *end = NULL;
		errno = 0;
		values[i] = strtoul(position, &end, 10);
		if (errno != 0 || end == position || (i + 1 < count && *end != ',')) {
			return false;
		}
		position = end + 1;
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
	bool haveHeader = fgets(line, sizeof line, csv) != NULL;
	while (haveHeader && fgets(line, sizeof line, csv)) {
		unsigned long values[3] = {0};
		if (parseCsvNumbers(line, values, 3)) {
			addResult(&totals, (unsigned)values[0], (unsigned)values[1], (unsigned)values[2]);
		}
	}
	(void)fclose(csv);
	if (totals.runs == 0) {
		printf("0 runs completed; see harness.log\n");
		return EXIT_OK;
	}
	printf(
		"%u runs: home %u / draw %u / away %u, avg %.2f-%.2f\n",
		totals.runs,
		totals.homeWins,
		totals.draws,
		totals.awayWins,
		(double)totals.homeGoals / totals.runs,
		(double)totals.awayGoals / totals.runs
	);
	return EXIT_OK;
}

// Turns a .json config into the .bin the DLL plays (rebuilding first with --recompute).
static bool prepareInput(Path *input, bool recompute) {
	bool isJson = files_hasExtension(input->wide, L".json");
	if (!isJson) {
		if (recompute) {
			fprintf(stderr, "--recompute needs a .json config\n");
		}
		return !recompute;
	}
	Path bin;
	char result[CLIENT_RESULT_LENGTH];
	size_t size = 0;
	if (!workingPath(L"_job_input.bin", &bin)) {
		return false;
	}
	if (recompute) {
		if (!rebuildConfig(input, &bin, result, sizeof result)) {
			return false;
		}
		printf("%s\n", result);
		if (!client_succeeded(result)) {
			return false;
		}
	} else if (!encodeToFile(input, &bin, &size)) {
		return false;
	}
	*input = bin;
	return true;
}

typedef struct RunOptions {
	const wchar_t *input;
	const wchar_t *seeds;  // default "keep"
	const wchar_t *output; // default: the input with a .csv extension
	bool recompute;
	unsigned long threads;
	const wchar_t *profile;
} RunOptions;

// One worker per logical processor, less one left for the game itself.
static unsigned long defaultThreads(void) {
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
	if (wcscmp(name, L"--recompute") == 0) {
		options->recompute = true;
	} else if (wcscmp(name, L"--profile") == 0 && hasValue) {
		options->profile = argv[++*i];
	} else if (wcscmp(name, L"--threads") == 0 && hasValue) {
		wchar_t *end = NULL;
		options->threads = wcstoul(argv[++*i], &end, 10);
		if (*end != 0 || options->threads < 1 || options->threads > HARNESS_MAX_THREADS) {
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
	*options = (RunOptions){.threads = defaultThreads()};
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
		return false;
	}
	wcscat(path, L".csv");
	return absolutePath(path, output);
}

int commands_run(int argc, wchar_t **argv) {
	RunOptions options;
	if (!parseRunArguments(argc, argv, &options)) {
		fprintf(
			stderr,
			"usage: fm24ctl run <input.bin|.json> [seeds] [output.csv] [--recompute]"
			" [--threads N] [--profile <samples.txt>]\n"
		);
		return EXIT_USAGE;
	}
	Path input;
	Path output;
	Path profile = {0};
	char seeds[256];
	if (!absolutePath(options.input, &input) ||
			!(options.output ? absolutePath(options.output, &output) : defaultOutput(&input, &output)) ||
			(options.profile && !absolutePath(options.profile, &profile)) ||
			!paths_toUtf8(options.seeds, seeds, sizeof seeds) ||
			!prepareInput(&input, options.recompute)) {
		return EXIT_FAILED;
	}
	printf("results: %s\n", output.utf8);
	char job[JOB_LENGTH];
	(void)snprintf(
		job,
		sizeof job,
		"command=run\ninput=%s\nseeds=%s\noutput=%s\nthreads=%lu\nprofile=%s\n",
		input.utf8,
		seeds,
		output.utf8,
		options.threads,
		profile.utf8
	);
	printf("job submitted; waiting...\n");
	if (!client_submit(job, CLIENT_LONG_TIMEOUT_MS, NULL, 0)) {
		return EXIT_FAILED;
	}
	return printSummary(&output);
}

// get-next-fixture's options after the output path: only `--seed N` (a decimal u32).
static bool parseFixtureOptions(int argc, wchar_t **argv, char *seed, size_t size) {
	for (int i = 3; i < argc; ++i) {
		if (wcscmp(argv[i], L"--seed") != 0 || i + 1 >= argc) {
			fprintf(stderr, "unknown option or missing value: %ls\n", argv[i]);
			return false;
		}
		const wchar_t *value = argv[++i];
		wchar_t *end = NULL;
		errno = 0;
		unsigned long long number = wcstoull(value, &end, 10);
		if (errno != 0 || end == value || *end != 0 || value[0] == L'-' || number > UINT32_MAX) {
			fprintf(stderr, "--seed must be a number from 0 to %u\n", UINT32_MAX);
			return false;
		}
		(void)snprintf(seed, size, "%llu", number);
	}
	return true;
}

int commands_getNextFixture(int argc, wchar_t **argv) {
	char seed[32] = "random";
	if (argc < 3 || !parseFixtureOptions(argc, argv, seed, sizeof seed)) {
		fprintf(stderr, "usage: fm24ctl get-next-fixture <output.json|.bin> [--seed N]\n");
		return EXIT_USAGE;
	}
	Path output;
	if (!absolutePath(argv[2], &output)) {
		return EXIT_FAILED;
	}
	char job[JOB_LENGTH];
	(void)snprintf(job, sizeof job, "command=build_next\noutput=%s\nseed=%s\n", output.utf8, seed);
	return submitAndReport(job);
}

int commands_export(int argc, wchar_t **argv) {
	if (argc < 4) {
		fprintf(stderr, "usage: fm24ctl export <input.bin> <output.json>\n");
		return EXIT_USAGE;
	}
	Path input;
	Path output;
	if (!absolutePath(argv[2], &input) || !absolutePath(argv[3], &output)) {
		return EXIT_FAILED;
	}
	char job[JOB_LENGTH];
	(void)snprintf(job, sizeof job, "command=export\ninput=%s\noutput=%s\n", input.utf8, output.utf8);
	return submitAndReport(job);
}

int commands_encode(int argc, wchar_t **argv) {
	if (argc < 4) {
		fprintf(stderr, "usage: fm24ctl encode <input.json> <output.bin>\n");
		return EXIT_USAGE;
	}
	Path input;
	Path output;
	size_t size = 0;
	if (!absolutePath(argv[2], &input) || !absolutePath(argv[3], &output) ||
			!encodeToFile(&input, &output, &size)) {
		return EXIT_FAILED;
	}
	printf("wrote %zu bytes\n", size);
	return EXIT_OK;
}

int commands_rebuild(int argc, wchar_t **argv) {
	if (argc < 4) {
		fprintf(stderr, "usage: fm24ctl rebuild <input.json> <output.json|.bin>\n");
		return EXIT_USAGE;
	}
	Path input;
	Path output;
	char result[CLIENT_RESULT_LENGTH];
	if (!absolutePath(argv[2], &input) || !absolutePath(argv[3], &output) ||
			!rebuildConfig(&input, &output, result, sizeof result)) {
		return EXIT_FAILED;
	}
	printf("%s\n", result);
	return client_succeeded(result) ? EXIT_OK : EXIT_FAILED;
}
