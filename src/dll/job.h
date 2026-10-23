// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// Jobs submitted by the runner: a message of key=value lines, and a one-line result (protocol.h).
#pragma once

#include "protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>


typedef enum JobCommand {
	JOB_NONE,             // no "command=" line: the job is rejected
	JOB_RUN,              // play an input with a list of seeds, write a CSV
	JOB_BUILD_NEXT,       // build the human manager's next fixture to .bin or .json
	JOB_INSPECT,          // decode existing binary input to JSON without preparing or playing it
	JOB_REPLACE_AWAY,     // rebuild an input against another away team, keeping its other edits
	JOB_REPLACE_HOME,     // rebuild an input with another home team, keeping its other edits
	JOB_LIST_TEAMS,       // write every club's first team (unique ID and short name) to a text file
	JOB_LIST_PLAYERS,     // write every player (unique ID, names and club) to a text file
	JOB_GET_PLAYER,       // write a player's database values for a match config player block
	JOB_DEVELOP,          // simulate a player's monthly training and development, write a CSV
	JOB_SEED_DEVELOPMENT, // make the game's own next development of a player use a seed
} JobCommand;

enum { JOB_TEAM_LENGTH = 256 };

typedef struct Job {
	JobCommand command;
	wchar_t input[HARNESS_PATH_LENGTH];
	wchar_t output[HARNESS_PATH_LENGTH];
	wchar_t profile[HARNESS_PATH_LENGTH]; // run: optional sample file (see profiler.h)
	uint32_t *seeds;                      // run
	size_t seedCount;
	unsigned threads;         // run: worker threads (1..HARNESS_MAX_THREADS)
	uintptr_t progressWindow; // run: optional viewer window receiving HARNESS_PROGRESS_MESSAGE
	bool keepSeed;            // run: play the input's own seed once
	bool detailed;            // run: also write per-player and per-team statistics (match_stats.h)
	bool humanControl; // run: keep the input's "human in control" flags (no AI in-match changes)
	bool keepBuild;    // run: patch each seed into the input instead of rebuilding it per seed
	bool hasSeed;      // build_next: use `seed` instead of a clock value
	bool full;         // build_next, replace: also write the research keys in a JSON config
	uint32_t seed;
	char team[JOB_TEAM_LENGTH];     // replace: the new team (database_findTeam's spec)
	char teams[2][JOB_TEAM_LENGTH]; // run, replace: input's home/away overrides ("": the fixture's)
	uint32_t personId;              // get_player, develop: the player's unique ID (0: missing)
	unsigned months;                // develop: months to simulate per seed
} Job;

// Parses a job's key=value lines (modifying `text`); logs and returns false if it is malformed.
bool job_parse(char *text, Job *job);
void job_free(Job *job);

// Sets the job's one-line outcome (must start with "ok" or "error"); a later call replaces it.
void job_writeResult(const char *format, ...) __attribute__((format(gnu_printf, 1, 2)));
// The outcome set since the last call ("" if none), for the reply to the runner.
const char *job_takeResult(void);

// The job implementations (job_*.c); each reports through job_writeResult and/or the log.
void jobs_run(const Job *job);
void jobs_buildNext(const Job *job);
void jobs_inspect(const Job *job);
void jobs_replaceTeam(const Job *job);
void jobs_listTeams(const Job *job);
void jobs_listPlayers(const Job *job);
void jobs_getPlayer(const Job *job);
void jobs_develop(const Job *job);
void jobs_seedDevelopment(const Job *job);

// develop's simulation (job_develop.c) of one month for each seed, run on the calling thread with
// the player's development due now (no training weeks or ageing first), written as develop's CSV.
// The player's simulated values are put back afterwards. For seed_development's hook.
bool develop_runHere(
	uint8_t *player,
	const uint32_t *seeds,
	size_t seedCount,
	const wchar_t *output
);
