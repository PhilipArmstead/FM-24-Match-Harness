// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "client.h"
#include "embedded.h"
#include "files.h"
#include "test.h"
#include "tests.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>


static bool readConfig(const wchar_t *path, ByteBuffer *out);
static bool writeConfig(const wchar_t *path, const void *bytes, size_t length);
static bool submitJob(const char *job, unsigned timeout, char *result, size_t resultSize);

#define commands_defaultThreads runnerTest_defaultThreads
#define commands_run runnerTest_run
#define commands_runWithProgress runnerTest_runWithProgress
#define commands_getNextFixture runnerTest_getNextFixture
#define commands_replaceTeam runnerTest_replaceTeam
#define commands_replaceTeamWithResult runnerTest_replaceTeamWithResult
#define commands_listTeams runnerTest_listTeams
#define commands_listTeamsWithResult runnerTest_listTeamsWithResult
#define commands_encode runnerTest_encode
#define commands_getPlayer runnerTest_getPlayer
#define commands_injectPlayer runnerTest_injectPlayer
#define commands_injectPlayerWithResult runnerTest_injectPlayerWithResult
#define commands_probe runnerTest_probe
#define files_read readConfig
#define files_write writeConfig
#define client_submit submitJob
#include "../src/runner/commands.c" // NOLINT(bugprone-suspicious-include): exercise private parsers.
#undef files_read
#undef files_write
#undef client_submit

#include "../src/runner/client.c" // NOLINT(bugprone-suspicious-include): check result replies.
#include "../src/runner/probe.c"  // NOLINT(bugprone-suspicious-include): test in-memory PE images.


static unsigned configReads;
static ByteBuffer writtenConfig;
static char submittedJob[HARNESS_JOB_LENGTH];

static bool submitJob(const char *job, unsigned timeout, char *result, size_t resultSize) {
	ASSERT_BOOL_EQ(timeout == CLIENT_SHORT_TIMEOUT_MS, true);
	(void)snprintf(submittedJob, sizeof submittedJob, "%s", job);
	(void)snprintf(result, resultSize, "ok");
	return true;
}

static bool readConfig(const wchar_t *path, ByteBuffer *out) {
	(void)path;
	++configReads;
	static const char json[] = "{\"format\":\"fm24-match-config/"
														 "1\",\"fields\":[{\"u8\":42}],\"home_team\":678,\"away_team\":677}";
	return buffer_append(out, json, sizeof json - 1);
}

static bool writeConfig(const wchar_t *path, const void *bytes, size_t length) {
	(void)path;
	return buffer_append(&writtenConfig, bytes, length);
}

bool inject_harness(void) {
	return false;
}

unsigned long inject_findGame(void) {
	return 0;
}

TEST(encodes_input_and_metadata_in_one_read) {
	Path input = {.wide = L"input.json", .utf8 = "input.json"};
	Path output = {.wide = L"output.bin", .utf8 = "output.bin"};
	uint32_t teams[2] = {0};
	size_t size = 0;
	configReads = 0;
	ASSERT_BOOL_EQ(encodeToFile(&input, &output, &size, teams), true);
	ASSERT_INT_EQ(configReads, 1);
	ASSERT_BOOL_EQ(size == 1 && writtenConfig.length == 1 && writtenConfig.data[0] == 42, true);
	ASSERT_BOOL_EQ(teams[0] == 678 && teams[1] == 677, true);
	buffer_free(&writtenConfig);
}

TEST(replaces_either_team_and_preserves_both_overrides) {
	for (unsigned side = 0; side < 2; ++side) {
		wchar_t *arguments[] = {L"fm24ctl.exe",
			side ? L"replace-away" : L"replace-home",
			L"input.json",
			L"Norwich City",
			L"output.json",
			L"--full",
			NULL};
		configReads = 0;
		char result[CLIENT_RESULT_LENGTH];
		ASSERT_INT_EQ(commands_replaceTeamWithResult(6, arguments, result, sizeof result), EXIT_OK);
		ASSERT_INT_EQ(configReads, 1);
		ASSERT_BOOL_EQ(
			strstr(submittedJob, side ? "command=replace_away\n" : "command=replace_home\n") != NULL, true
		);
		ASSERT_BOOL_EQ(
			strstr(submittedJob, "\nteam=Norwich City\nhome_team=678\naway_team=677\n") != NULL, true
		);
		ASSERT_BOOL_EQ(strstr(submittedJob, "\nfull=1\n") != NULL, true);
		buffer_free(&writtenConfig);
	}
}

TEST(parses_player_ids_and_blocks) {
	char text[64];
	static const wchar_t *const invalidIds[] = {L"", L"0", L"-1", L" 1", L"1x", L"4294967296"};
	for (size_t i = 0; i < sizeof invalidIds / sizeof invalidIds[0]; ++i) {
		ASSERT_BOOL_EQ(parsePersonId(invalidIds[i], text, sizeof text), false);
	}
	ASSERT_BOOL_EQ(parsePersonId(L"2000040347", text, sizeof text), true);
	ASSERT_INT_EQ(strcmp(text, "2000040347"), 0);
	static const wchar_t *const invalidBlocks[] = {
		L"home.player[]",
		L"home.player[1",
		L"home.player[x]",
		L"home.player[1].ratings",
		L"both.player[1]",
		L"home.staff[1]",
	};
	for (size_t i = 0; i < sizeof invalidBlocks / sizeof invalidBlocks[0]; ++i) {
		ASSERT_BOOL_EQ(parsePlayerBlock(invalidBlocks[i], text, sizeof text), false);
	}
	ASSERT_BOOL_EQ(parsePlayerBlock(L"away.player[17]", text, sizeof text), true);
	ASSERT_INT_EQ(strcmp(text, "away.player[17]"), 0);
	ASSERT_BOOL_EQ(parsePlayerBlock(L"home.player[3]", text, 14), false);
}

TEST(rejects_malformed_cli_numbers) {
	static const wchar_t *const invalid[] = {
		L"",
		L"-1",
		L"+1",
		L" 1",
		L"1 ",
		L"1x",
		L"4294967296",
		L"999999999999999999999999999",
	};
	char seed[32];
	for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		ASSERT_BOOL_EQ(parseSeedValue(invalid[i], seed, sizeof seed), false);
	}
	ASSERT_BOOL_EQ(parseSeedValue(L"4294967295", seed, sizeof seed), true);
	ASSERT_INT_EQ(strcmp(seed, "4294967295"), 0);
	static wchar_t *const threads[] = {L"0", L"-1", L"+1", L" 1", L"17", L"99999999999999999999"};
	for (size_t i = 0; i < sizeof threads / sizeof threads[0]; ++i) {
		wchar_t *arguments[] = {L"--threads", threads[i]};
		int index = 0;
		RunOptions options = {0};
		ASSERT_BOOL_EQ(parseRunOption(2, arguments, &index, &options), false);
	}
	unsigned long values[3];
	ASSERT_BOOL_EQ(parseCsvNumbers("4294967295,1,2,100,1,123,5.5\n", values, 3), true);
	ASSERT_BOOL_EQ(values[0] == UINT32_MAX && values[1] == 1 && values[2] == 2, true);
	static const char *const invalidRows[] = {
		"-1,1,2",
		"+1,1,2",
		" 1,1,2",
		"1,2,3junk",
		"1,2,",
		"4294967296,1,2",
		"1,2x,3",
		"1,2,+3",
		"1,2, 3",
	};
	for (size_t i = 0; i < sizeof invalidRows / sizeof invalidRows[0]; ++i) {
		ASSERT_BOOL_EQ(parseCsvNumbers(invalidRows[i], values, 3), false);
	}
	Totals totals = {.runs = SUMMARY_SHOWN, .homeGoals = UINT32_MAX, .awayGoals = UINT32_MAX};
	addResult(&totals, 42, 2, 1);
	ASSERT_BOOL_EQ(totals.homeGoals == (uint64_t)UINT32_MAX + 2, true);
	ASSERT_BOOL_EQ(totals.awayGoals == (uint64_t)UINT32_MAX + 1, true);
}

static void checkResult(const void *data, size_t length, bool expected) {
	char result[32] = "";
	memcpy(result, data, length);
	ASSERT_BOOL_EQ(validResult(result, length), expected);
	ASSERT_BOOL_EQ(result[0] != 0, expected);
}

TEST(rejects_missing_or_incomplete_results) {
	ASSERT_BOOL_EQ(client_succeeded("ok"), true);
	ASSERT_BOOL_EQ(client_succeeded("ok: complete"), true);
	ASSERT_BOOL_EQ(client_succeeded("okay"), false);
	ASSERT_BOOL_EQ(client_succeeded("okerror"), false);
	ASSERT_BOOL_EQ(client_succeeded("o"), false);
	ASSERT_BOOL_EQ(client_succeeded(""), false);
	checkResult("ok: done", 8, true);
	checkResult("ok: done\n", 9, true);
	checkResult("ok: done\r\n", 10, true);
	checkResult("ok: done\nerror", 14, false);
	checkResult("", 0, false);
	checkResult("ok\0error", 8, false);
}

TEST(validates_pe_header_and_section_bounds) {
	uint8_t bytes[512] = {'M', 'Z'};
	uint32_t offset = 64;
	memcpy(bytes + DOS_HEADER_LFANEW, &offset, sizeof offset);
	IMAGE_NT_HEADERS64 nt = {
		.Signature = IMAGE_NT_SIGNATURE,
		.FileHeader =
			{
				.Machine = IMAGE_FILE_MACHINE_AMD64,
				.NumberOfSections = 1,
				.SizeOfOptionalHeader = sizeof nt.OptionalHeader,
			},
		.OptionalHeader = {.Magic = PE32_PLUS_MAGIC, .SizeOfImage = 0x2000},
	};
	IMAGE_SECTION_HEADER section = {
		.VirtualAddress = 0x1000,
		.Misc.VirtualSize = 16,
		.PointerToRawData = 400,
		.SizeOfRawData = 16,
		.Characteristics = IMAGE_SCN_MEM_EXECUTE,
	};
	memcpy(bytes + offset, &nt, sizeof nt);
	memcpy(bytes + offset + sizeof nt, &section, sizeof section);
	Image image = {.view = bytes};
	for (size_t length = 0; length < 416; ++length) {
		image.size = length;
		ASSERT_BOOL_EQ(parseHeaders(&image), false);
	}
	image.size = sizeof bytes;
	ASSERT_BOOL_EQ(parseHeaders(&image), true);
	ASSERT_BOOL_EQ(parseHeaders(&image), true);
	ASSERT_BOOL_EQ(image.sectionCount == 1, true);
	ASSERT_BOOL_EQ(imageBytes(&image, 0x1000, 16) == bytes + 400, true);
	ASSERT_BOOL_EQ(imageBytes(&image, 0x1001, 16) == NULL, true);
	offset = UINT32_MAX;
	memcpy(bytes + DOS_HEADER_LFANEW, &offset, sizeof offset);
	ASSERT_BOOL_EQ(parseHeaders(&image), false);
	offset = 64;
	memcpy(bytes + DOS_HEADER_LFANEW, &offset, sizeof offset);
	section.SizeOfRawData = UINT32_MAX;
	memcpy(bytes + offset + sizeof nt, &section, sizeof section);
	ASSERT_BOOL_EQ(parseHeaders(&image), false);
	nt.FileHeader.SizeOfOptionalHeader = 1;
	memcpy(bytes + offset, &nt, sizeof nt);
	ASSERT_BOOL_EQ(parseHeaders(&image), false);
}

TEST(scans_only_selected_anchors_and_ranks_shifts) {
	Probe *probe = calloc(1, sizeof *probe);
	ASSERT_BOOL_EQ(probe != NULL, true);
	if (!probe) {
		return;
	}
	probe->matches = calloc(probeFunctionCount, sizeof *probe->matches);
	probe->shifts = calloc(probeFunctionCount, sizeof *probe->shifts);
	bool allocated = probe->matches && probe->shifts && probeFunctionCount >= 3;
	ASSERT_BOOL_EQ(allocated, true);
	if (allocated) {
		const ProbeFunction *function = &probeFunctions[0];
		probe->image.sectionCount = 1;
		probe->image.sections[0] = (Section){
			.rva = function->rva,
			.raw = probePatternBytes + function->byteOffset,
			.rawSize = function->size,
			.characteristics = IMAGE_SCN_MEM_EXECUTE,
		};
		ASSERT_BOOL_EQ(buildAnchors(probe), true);
		scan(probe, false);
		chooseAnchors(probe);
		size_t selected = 0;
		for (size_t bucket = 0; bucket < sizeof probe->buckets / sizeof probe->buckets[0]; ++bucket) {
			for (int32_t a = probe->buckets[bucket]; a >= 0; a = probe->anchors[a].next) {
				ASSERT_BOOL_EQ(probe->anchors[a].used, true);
				++selected;
			}
		}
		ASSERT_BOOL_EQ(selected > 0 && selected <= ANCHORS_PER_FUNCTION * probeFunctionCount, true);
		scan(probe, true);
		settle(&probe->matches[0]);
		ASSERT_INT_EQ(probe->matches[0].status, FOUND);
		ASSERT_BOOL_EQ(probe->matches[0].rva == function->rva, true);
		memset(probe->matches, 0, probeFunctionCount * sizeof *probe->matches);
		for (size_t i = 0; i < 3; ++i) {
			probe->matches[i] = (Match){
				.status = FOUND,
				.rva = probeFunctions[i].rva + (i < 2 ? 0x100U : 0x200U),
			};
		}
		rankShifts(probe);
		ASSERT_BOOL_EQ(commonShift(probe, 0) == 0x100, true);
		ASSERT_BOOL_EQ(commonShift(probe, 1) == 0x200, true);
		ASSERT_BOOL_EQ(commonShift(probe, 2) == INT64_MIN, true);
		probe->matches[2] = (Match){
			.status = AMBIGUOUS,
			.tieCount = 2,
			.ties = {probeFunctions[2].rva + 0x200, probeFunctions[2].rva + 0x100},
		};
		resolveTies(probe);
		ASSERT_BOOL_EQ(probe->matches[2].status == FOUND && probe->matches[2].byShift, true);
		ASSERT_BOOL_EQ(probe->matches[2].rva == probeFunctions[2].rva + 0x100, true);
	}
	free(probe->anchors);
	free(probe->matches);
	free(probe->shifts);
	free(probe);
}

TEST(preserves_jump_candidate_overflow) {
	uint8_t bytes[(MAX_CANDIDATES + 1) * JMP_REL32_LENGTH] = {0};
	for (size_t i = 0; i < sizeof bytes; i += JMP_REL32_LENGTH) {
		bytes[i] = JMP_REL32;
		int32_t displacement = (int32_t)(0x2000 - (0x1000 + i + JMP_REL32_LENGTH));
		memcpy(bytes + i + 1, &displacement, sizeof displacement);
	}
	Image image = {.sectionCount = 1};
	image.sections[0] = (Section){
		.rva = 0x1000,
		.raw = bytes,
		.rawSize = sizeof bytes,
		.characteristics = IMAGE_SCN_MEM_EXECUTE,
	};
	Match match = {0};
	collectJumpsTo(&image, 0x2000, &match);
	ASSERT_BOOL_EQ(match.tieCount == MAX_CANDIDATES && match.tooMany, true);
	settle(&match);
	ASSERT_INT_EQ(match.status, AMBIGUOUS);
	image.sections[0].rawSize = 4;
	match = (Match){0};
	collectJumpsTo(&image, 0x2000, &match);
	ASSERT_INT_EQ(match.tieCount, 0);
}

TEST(extracts_the_dll_only_when_it_changed) {
	static const char first[] = "first build";
	static const char second[] = "second build";
	wchar_t path[MAX_PATH];
	DWORD length = GetTempPathW(MAX_PATH, path);
	ASSERT_BOOL_EQ(length > 0 && length < MAX_PATH - 40, true);
	(void)swprintf(
		path + length, MAX_PATH - length, L"fm24-embedded-test-%lu.dll", GetCurrentProcessId()
	);
	ByteBuffer contents = {0};
	ASSERT_BOOL_EQ(embedded_write(path, first, sizeof first), true);
	ASSERT_BOOL_EQ(files_read(path, &contents) && contents.length == sizeof first, true);
	// A read-only file can't be replaced, like a DLL that fm.exe has loaded.
	ASSERT_BOOL_EQ(SetFileAttributesW(path, FILE_ATTRIBUTE_READONLY) != 0, true);
	ASSERT_BOOL_EQ(embedded_write(path, first, sizeof first), true);
	ASSERT_BOOL_EQ(embedded_write(path, second, sizeof second), false);
	ASSERT_BOOL_EQ(SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL) != 0, true);
	ASSERT_BOOL_EQ(embedded_write(path, second, sizeof second), true);
	ASSERT_BOOL_EQ(
		files_read(path, &contents) && contents.length == sizeof second &&
			memcmp(contents.data, second, sizeof second) == 0,
		true
	);
	buffer_free(&contents);
	(void)DeleteFileW(path);
}

void registerRunnerTests(void) {
	test_run(
		"runner extracts the DLL only when it changed", test_extracts_the_dll_only_when_it_changed
	);
	test_run(
		"runner replaces either team and preserves both overrides",
		test_replaces_either_team_and_preserves_both_overrides
	);
	test_run(
		"runner encodes input and metadata in one read", test_encodes_input_and_metadata_in_one_read
	);
	test_run("runner rejects malformed CLI numbers", test_rejects_malformed_cli_numbers);
	test_run("runner parses player IDs and blocks", test_parses_player_ids_and_blocks);
	test_run(
		"runner rejects missing or incomplete results", test_rejects_missing_or_incomplete_results
	);
	test_run(
		"probe validates PE header and section bounds", test_validates_pe_header_and_section_bounds
	);
	test_run(
		"probe scans selected anchors and ranks shifts",
		test_scans_only_selected_anchors_and_ranks_shifts
	);
	test_run("probe preserves jump candidate overflow", test_preserves_jump_candidate_overflow);
}
