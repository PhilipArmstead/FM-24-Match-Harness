// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/dll/job.c" // NOLINT(bugprone-suspicious-include): test internal job parsing.
#include "test.h"
#include "tests.h"


void log_message(const char *format, ...) {
	va_list arguments;
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\n', stderr);
}

TEST(parses_progress_window) {
	Job job = {0};
	ASSERT_BOOL_EQ(job.progressWindow == 0, true);
	ASSERT_BOOL_EQ(parseLine(&job, "progress_window", "0"), true);
	ASSERT_BOOL_EQ(job.progressWindow == 0, true);
	ASSERT_BOOL_EQ(parseLine(&job, "progress_window", "123456"), true);
	ASSERT_BOOL_EQ(job.progressWindow == 123456, true);
	char maximum[32];
	(void)snprintf(maximum, sizeof maximum, "%llu", (unsigned long long)UINTPTR_MAX);
	ASSERT_BOOL_EQ(parseLine(&job, "progress_window", maximum), true);
	ASSERT_BOOL_EQ(job.progressWindow == UINTPTR_MAX, true);
	char invalid[][32] = {
		"", "-1", " -1", "+1", " 1", "1 ", "1x", "1\ncommand=run", "18446744073709551616"};
	for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		ASSERT_BOOL_EQ(parseLine(&job, "progress_window", invalid[i]), false);
		ASSERT_BOOL_EQ(job.progressWindow == UINTPTR_MAX, true);
	}
	job_free(&job);
	ASSERT_BOOL_EQ(job.progressWindow == 0, true);
}

TEST(replaces_repeated_seed_lists) {
	Job job = {0};
	char first[] = "1-3,4294967295";
	char second[] = "8-80";
	char keep[] = "keep";
	ASSERT_BOOL_EQ(parseLine(&job, "seeds", first), true);
	ASSERT_BOOL_EQ(job.seedCount == 4, true);
	ASSERT_BOOL_EQ(job.seeds[3] == UINT32_MAX, true);
	ASSERT_BOOL_EQ(parseLine(&job, "seeds", second), true);
	ASSERT_BOOL_EQ(job.seedCount == 73, true);
	if (job.seedCount == 73) {
		ASSERT_BOOL_EQ(job.seeds[0] == 8 && job.seeds[72] == 80, true);
	}
	ASSERT_BOOL_EQ(parseLine(&job, "seeds", keep), true);
	ASSERT_BOOL_EQ(job.keepSeed, true);
	ASSERT_BOOL_EQ(job.seeds == NULL && job.seedCount == 0, true);
	job_free(&job);
}

TEST(rejects_invalid_seeds) {
	char invalid[][32] = {
		"", ",1", "1,", "1,,2", "-1", "+1", " 1", "1 ", "2-1", "1--2", "4294967296", "0-10000001"};
	for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		Job job = {0};
		ASSERT_BOOL_EQ(parseLine(&job, "seeds", invalid[i]), false);
		job_free(&job);
	}
	Job job = {0};
	ASSERT_BOOL_EQ(parseLine(&job, "seed", "4294967295"), true);
	ASSERT_BOOL_EQ(job.hasSeed && job.seed == UINT32_MAX, true);
	char invalidNumbers[][32] = {"", "-1", "+1", " 1", "1x", "4294967296", "18446744073709551616"};
	for (size_t i = 0; i < sizeof invalidNumbers / sizeof invalidNumbers[0]; ++i) {
		ASSERT_BOOL_EQ(parseLine(&job, "seed", invalidNumbers[i]), false);
		ASSERT_BOOL_EQ(job.hasSeed && job.seed == UINT32_MAX, true);
	}
	ASSERT_BOOL_EQ(parseLine(&job, "seed", "random"), true);
	ASSERT_BOOL_EQ(job.hasSeed || job.seed != 0, false);
}

TEST(parses_job_messages) {
	Job job;
	char run[] = "command=run\r\ninput=C:/in.bin\r\nseeds=1-3\r\noutput=C:/out.csv";
	ASSERT_BOOL_EQ(job_parse(run, &job), true);
	ASSERT_BOOL_EQ(job.command == JOB_RUN && job.seedCount == 3, true);
	ASSERT_INT_EQ(wcscmp(job.input, L"C:/in.bin"), 0);
	ASSERT_INT_EQ(wcscmp(job.output, L"C:/out.csv"), 0);
	job_free(&job);
	char build[] = "command=build_next\noutput=C:/next.json\n\n";
	ASSERT_BOOL_EQ(job_parse(build, &job), true);
	ASSERT_BOOL_EQ(job.command == JOB_BUILD_NEXT, true);
	job_free(&job);
	char replaceHome[] =
		"command=replace_home\ninput=C:/in.bin\noutput=C:/out.json\nteam=Norwich City\n"
		"home_team=678\naway_team=677\n";
	ASSERT_BOOL_EQ(job_parse(replaceHome, &job), true);
	ASSERT_BOOL_EQ(job.command == JOB_REPLACE_HOME, true);
	ASSERT_INT_EQ(strcmp(job.team, "Norwich City"), 0);
	ASSERT_INT_EQ(strcmp(job.teams[0], "678"), 0);
	ASSERT_INT_EQ(strcmp(job.teams[1], "677"), 0);
	job_free(&job);
	char replaceAway[] = "command=replace_away\ninput=C:/in.bin\noutput=C:/out.json\nteam=677\n";
	ASSERT_BOOL_EQ(job_parse(replaceAway, &job), true);
	ASSERT_BOOL_EQ(job.command == JOB_REPLACE_AWAY, true);
	job_free(&job);
	char missingTeam[] = "command=replace_home\ninput=C:/in.bin\noutput=C:/out.json\n";
	ASSERT_BOOL_EQ(job_parse(missingTeam, &job), false);
	char teams[] = "command=list_teams\noutput=C:/teams.txt\n";
	ASSERT_BOOL_EQ(job_parse(teams, &job), true);
	ASSERT_BOOL_EQ(job.command == JOB_LIST_TEAMS, true);
	ASSERT_INT_EQ(wcscmp(job.output, L"C:/teams.txt"), 0);
	job_free(&job);
	char noTeamsOutput[] = "command=list_teams\n";
	ASSERT_BOOL_EQ(job_parse(noTeamsOutput, &job), false);
	char players[] = "command=list_players\noutput=C:/players.txt\n";
	ASSERT_BOOL_EQ(job_parse(players, &job), true);
	ASSERT_BOOL_EQ(job.command == JOB_LIST_PLAYERS, true);
	ASSERT_INT_EQ(wcscmp(job.output, L"C:/players.txt"), 0);
	job_free(&job);
	char noPlayersOutput[] = "command=list_players\n";
	ASSERT_BOOL_EQ(job_parse(noPlayersOutput, &job), false);
	char develop[] = "command=develop\nperson=2000147523\nmonths=24\nseeds=1-10\noutput=C:/g.csv\n";
	ASSERT_BOOL_EQ(job_parse(develop, &job), true);
	ASSERT_BOOL_EQ(job.command == JOB_DEVELOP && job.personId == 2000147523, true);
	ASSERT_BOOL_EQ(job.months == 24 && job.seedCount == 10, true);
	job_free(&job);
	char seedDevelopment[] = "command=seed_development\nperson=2000147523\nseed=0\n";
	ASSERT_BOOL_EQ(job_parse(seedDevelopment, &job), true);
	ASSERT_BOOL_EQ(job.command == JOB_SEED_DEVELOPMENT && job.hasSeed && job.seed == 0, true);
	job_free(&job);
	char withValues[] =
		"command=develop\nperson=1\nmonths=1\nseeds=1\noutput=C:/g.csv\nvalues=height:b400\n";
	ASSERT_BOOL_EQ(job_parse(withValues, &job), true);
	ASSERT_BOOL_EQ(job.values != NULL && strcmp(job.values, "height:b400") == 0, true);
	job_free(&job);
	ASSERT_BOOL_EQ(job.values == NULL, true);
	char getDevelopment[] = "command=get_development\nperson=1\noutput=C:/d.json\n";
	ASSERT_BOOL_EQ(job_parse(getDevelopment, &job), true);
	ASSERT_BOOL_EQ(job.command == JOB_GET_DEVELOPMENT, true);
	job_free(&job);
	char getDevelopmentNoOutput[] = "command=get_development\nperson=1\n";
	ASSERT_BOOL_EQ(job_parse(getDevelopmentNoOutput, &job), false);
	char noSeed[] = "command=seed_development\nperson=2000147523\n";
	ASSERT_BOOL_EQ(job_parse(noSeed, &job), false);
	char noPerson[] = "command=seed_development\nseed=7\n";
	ASSERT_BOOL_EQ(job_parse(noPerson, &job), false);
	char predicted[] = "command=seed_development\nperson=1\nseed=7\nseeds=1-20\noutput=C:/p.csv\n";
	ASSERT_BOOL_EQ(job_parse(predicted, &job), true);
	ASSERT_BOOL_EQ(job.seedCount == 20 && job.output[0] != 0, true);
	job_free(&job);
	char seedsWithoutOutput[] = "command=seed_development\nperson=1\nseed=7\nseeds=1-20\n";
	ASSERT_BOOL_EQ(job_parse(seedsWithoutOutput, &job), false);
	char outputWithoutSeeds[] = "command=seed_development\nperson=1\nseed=7\noutput=C:/p.csv\n";
	ASSERT_BOOL_EQ(job_parse(outputWithoutSeeds, &job), false);
	char noMonths[] = "command=develop\nperson=1\nseeds=1\noutput=C:/g.csv\n";
	ASSERT_BOOL_EQ(job_parse(noMonths, &job), false);
	char noSeeds[] = "command=develop\nperson=1\nmonths=1\noutput=C:/g.csv\n";
	ASSERT_BOOL_EQ(job_parse(noSeeds, &job), false);
	char invalidMonths[][16] = {"0", "601", "-1", "1x", ""};
	for (size_t i = 0; i < sizeof invalidMonths / sizeof invalidMonths[0]; ++i) {
		ASSERT_BOOL_EQ(parseLine(&job, "months", invalidMonths[i]), false);
	}
	ASSERT_BOOL_EQ(parseLine(&job, "months", "600"), true);
	ASSERT_BOOL_EQ(job.months == 600, true);
	job_free(&job);
	char incomplete[] = "command=run\ninput=C:/in.bin\nseeds=1\n";
	ASSERT_BOOL_EQ(job_parse(incomplete, &job), false);
	char bad[] = "command=inspect\ninput=C:/in.bin\noutput=C:/out.json\nseeds=x\n";
	ASSERT_BOOL_EQ(job_parse(bad, &job), false);
	char empty[] = "";
	ASSERT_BOOL_EQ(job_parse(empty, &job), false);
	job_writeResult("ok: %d", 1);
	ASSERT_INT_EQ(strcmp(job_takeResult(), "ok: 1"), 0);
	ASSERT_INT_EQ(strcmp(job_takeResult(), ""), 0);
}

void registerJobTests(void) {
	test_run(
		"job parses progress window handles and rejects invalid values", test_parses_progress_window
	);
	test_run(
		"job replaces repeated seed lists without retaining old seeds",
		test_replaces_repeated_seed_lists
	);
	test_run("job rejects malformed and overflowing seeds", test_rejects_invalid_seeds);
	test_run("job parses job messages and keeps one result", test_parses_job_messages);
}
