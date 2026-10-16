// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "job.h"

#include "log.h"
#include "protocol.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>


enum { MAX_SEED_RANGE = 10000000, LINE_LENGTH = 4096 };

static bool widen(const char *text, wchar_t *out, size_t size);
static bool appendSeed(Job *job, size_t *capacity, uint32_t seed);
static bool parseNumber(const char *text, char **end, unsigned long long *value);
static bool parseRange(const char *part, unsigned long long *first, unsigned long long *last);
static bool parseSeeds(Job *job, char *spec);
static bool parseCommand(Job *job, const char *value);
static bool copyText(const char *value, char *out, size_t size);
static bool parseLine(Job *job, char *key, char *value);
static bool isComplete(const Job *job);

bool job_read(const wchar_t *directory, Job *job) {
	*job = (Job){.threads = 1};
	wchar_t path[HARNESS_PATH_LENGTH];
	if (swprintf(path, HARNESS_PATH_LENGTH, L"%ls\\%ls", directory, HARNESS_JOB_FILE) < 0) {
		log_message("job file path is too long");
		return false;
	}
	FILE *file = _wfopen(path, L"rb");
	if (!file) {
		log_message("job file not found");
		return false;
	}
	char line[LINE_LENGTH];
	bool ok = true;
	while (ok && fgets(line, sizeof line, file)) {
		if (strlen(line) == sizeof line - 1 && line[sizeof line - 2] != '\n') {
			log_message("job line is too long");
			ok = false;
			break;
		}
		line[strcspn(line, "\r\n")] = 0;
		char *equals = strchr(line, '=');
		if (equals) {
			*equals = 0;
			ok = parseLine(job, line, equals + 1);
			if (!ok) {
				log_message("bad job line: %s=%s", line, equals + 1);
			}
		}
	}
	if (ferror(file)) {
		log_message("cannot read the job file");
		ok = false;
	}
	if (fclose(file) != 0) {
		log_message("cannot close the job file");
		ok = false;
	}
	if (ok && !isComplete(job)) {
		log_message("job is missing required keys");
		ok = false;
	}
	if (!ok) {
		job_free(job);
	}
	return ok;
}

void job_free(Job *job) {
	free(job->seeds);
	*job = (Job){0};
}

void job_writeResult(const wchar_t *directory, const char *format, ...) {
	wchar_t path[HARNESS_PATH_LENGTH];
	if (swprintf(path, HARNESS_PATH_LENGTH, L"%ls\\%ls", directory, HARNESS_RESULT_FILE) < 0) {
		log_message("job result path is too long");
		return;
	}
	FILE *file = _wfopen(path, L"wb");
	if (!file) {
		log_message("cannot open the job result file");
		return;
	}
	va_list args;
	va_start(args, format);
	bool ok = vfprintf(file, format, args) >= 0;
	va_end(args);
	if (fclose(file) != 0 || !ok) {
		log_message("cannot write the job result file");
	}
}

static bool widen(const char *text, wchar_t *out, size_t size) {
	return MultiByteToWideChar(CP_UTF8, 0, text, -1, out, (int)size) > 0;
}

static bool appendSeed(Job *job, size_t *capacity, uint32_t seed) {
	if (job->seedCount == *capacity) {
		size_t grown = *capacity ? *capacity * 2 : 64;
		uint32_t *seeds = realloc(job->seeds, grown * sizeof *seeds);
		if (!seeds) {
			return false;
		}
		job->seeds = seeds;
		*capacity = grown;
	}
	job->seeds[job->seedCount++] = seed;
	return true;
}

// A decimal number at `text`; `end` is left after it. False if there is none or it overflows.
static bool parseNumber(const char *text, char **end, unsigned long long *value) {
	if (*text < '0' || *text > '9') {
		return false;
	}
	errno = 0;
	*value = strtoull(text, end, 10);
	return errno == 0 && *end != text;
}

// "first" or "first-last".
static bool parseRange(const char *part, unsigned long long *first, unsigned long long *last) {
	char *end = NULL;
	if (!parseNumber(part, &end, first)) {
		return false;
	}
	*last = *first;
	if (*end == '-' && !parseNumber(end + 1, &end, last)) {
		return false;
	}
	return *end == 0;
}

// "42", "1-1000" or "1,5,10-20".
static bool parseSeeds(Job *job, char *spec) {
	free(job->seeds);
	job->seeds = NULL;
	job->seedCount = 0;
	job->keepSeed = strcmp(spec, "keep") == 0;
	if (job->keepSeed) {
		return true;
	}
	size_t capacity = 0;
	for (char *part = spec; part && *part;) {
		char *comma = strchr(part, ',');
		if (comma) {
			if (!comma[1]) {
				return false;
			}
			*comma = 0;
		}
		unsigned long long first = 0;
		unsigned long long last = 0;
		if (!parseRange(part, &first, &last) || last < first || last - first > MAX_SEED_RANGE ||
				last > UINT32_MAX) {
			return false;
		}
		for (unsigned long long seed = first; seed <= last; ++seed) {
			if (!appendSeed(job, &capacity, (uint32_t)seed)) {
				return false;
			}
		}
		part = comma ? comma + 1 : NULL;
	}
	return job->seedCount > 0;
}

static bool parseCommand(Job *job, const char *value) {
	static const struct {
		const char *name;
		JobCommand command;
	} commands[] = {
		{"run", JOB_RUN},
		{"build_next", JOB_BUILD_NEXT},
		{"inspect", JOB_INSPECT},
		{"replace_away", JOB_REPLACE_AWAY},
	};
	for (size_t i = 0; i < sizeof commands / sizeof commands[0]; ++i) {
		if (strcmp(value, commands[i].name) == 0) {
			job->command = commands[i].command;
			return true;
		}
	}
	return false;
}

static bool copyText(const char *value, char *out, size_t size) {
	size_t length = strlen(value);
	if (length >= size) {
		return false;
	}
	memcpy(out, value, length + 1);
	return true;
}

static bool parseLine(Job *job, char *key, char *value) {
	if (strcmp(key, "command") == 0) {
		return parseCommand(job, value);
	}
	if (strcmp(key, "team") == 0) {
		return copyText(value, job->team, sizeof job->team);
	}
	if (strcmp(key, "away_team") == 0) {
		return copyText(value, job->awayTeam, sizeof job->awayTeam);
	}
	if (strcmp(key, "input") == 0) {
		return widen(value, job->input, HARNESS_PATH_LENGTH);
	}
	if (strcmp(key, "output") == 0) {
		return widen(value, job->output, HARNESS_PATH_LENGTH);
	}
	if (strcmp(key, "profile") == 0) {
		return widen(value, job->profile, HARNESS_PATH_LENGTH);
	}
	if (strcmp(key, "seeds") == 0) {
		return parseSeeds(job, value);
	}
	if (strcmp(key, "seed") == 0) {
		if (strcmp(value, "random") == 0) {
			job->hasSeed = false;
			job->seed = 0;
			return true;
		}
		char *end = NULL;
		unsigned long long seed = 0;
		if (!parseNumber(value, &end, &seed) || *end != 0 || seed > UINT32_MAX) {
			return false;
		}
		job->hasSeed = true;
		job->seed = (uint32_t)seed;
		return true;
	}
	if (strcmp(key, "full") == 0) {
		job->full = strcmp(value, "1") == 0;
		return true;
	}
	if (strcmp(key, "keep_build") == 0) {
		job->keepBuild = strcmp(value, "1") == 0;
		return true;
	}
	if (strcmp(key, "human_control") == 0) {
		job->humanControl = strcmp(value, "1") == 0;
		return true;
	}
	if (strcmp(key, "detailed") == 0) {
		job->detailed = strcmp(value, "1") == 0;
		return true;
	}
	if (strcmp(key, "threads") == 0) {
		char *end = NULL;
		unsigned long long threads = 0;
		if (!parseNumber(value, &end, &threads) || *end != 0 || threads < 1 ||
				threads > HARNESS_MAX_THREADS) {
			return false;
		}
		job->threads = (unsigned)threads;
		return true;
	}
	if (strcmp(key, "progress_window") == 0) {
		char *end = NULL;
		unsigned long long window = 0;
		if (!parseNumber(value, &end, &window) || *end != 0 || window > UINTPTR_MAX) {
			return false;
		}
		job->progressWindow = (uintptr_t)window;
		return true;
	}
	return true; // unknown keys are ignored
}

static bool isComplete(const Job *job) {
	bool hasOutput = job->output[0] != 0;
	bool hasInput = job->input[0] != 0;
	switch (job->command) {
		case JOB_NONE: return false;
		case JOB_RUN: return hasInput && hasOutput && (job->keepSeed || job->seedCount > 0);
		case JOB_BUILD_NEXT: return hasOutput;
		case JOB_INSPECT: return hasInput && hasOutput;
		case JOB_REPLACE_AWAY: return hasInput && hasOutput && job->team[0] != 0;
	}
	return false;
}
