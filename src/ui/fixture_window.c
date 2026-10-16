// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixture_window.h"

#include "client.h"
#include "commands.h"
#include "constants.h"
#include "fixture_data.h"
#include "fixture_export.h"
#include "fixture_load.h"
#include "paths.h"
#include "protocol.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <windows.h>

// Common controls require the Win32 types above.
#include <commctrl.h>
#include <richedit.h>
#include <shobjidl.h>


enum { WM_FIXTURE_LAYOUT = WM_APP + 1, SIMULATION_TIMER = 1, REPLACE_TIMER = 2 };

enum { AWAY_ICON, AWAY_TEAM, AWAY_CONFIRM, AWAY_CANCEL, AWAY_ERROR, AWAY_CONTROL_COUNT };

enum {
	SIM_THREADS_LABEL,
	SIM_THREADS,
	SIM_SPINNER,
	SIM_SEEDS_LABEL,
	SIM_SEEDS,
	SIM_DETAILED,
	SIM_SUBMIT,
	SIM_STATUS,
	SIM_SUMMARY,
	SIM_CONTROL_COUNT,
};

typedef struct SimulationJob {
	wchar_t input[HARNESS_PATH_LENGTH];
	wchar_t seeds[256];
	wchar_t threads[16];
	uintptr_t progressWindow;
	bool detailed;
} SimulationJob;

typedef struct ReplaceAwayJob {
	wchar_t input[HARNESS_PATH_LENGTH];
	wchar_t output[HARNESS_PATH_LENGTH];
	wchar_t team[256];
	char error[CLIENT_RESULT_LENGTH];
	FixtureData *data;
} ReplaceAwayJob;

typedef struct WindowState {
	FixtureData *data;
	const wchar_t *path;
	const wchar_t *filename;
	wchar_t savedFilename[FIXTURE_EXPORT_FILENAME_LENGTH];
	HWND metadata;
	HWND tabs;
	HWND progress;
	HWND headings[2];
	wchar_t headingText[2][2 * FIXTURE_TEXT + 128];
	HWND notes[2];
	HWND pitches[2];
	HWND lists[2];
	HWND simulation[SIM_CONTROL_COUNT];
	HWND away[AWAY_CONTROL_COUNT];
	HANDLE simulationThread;
	HANDLE replacementThread;
	ITaskbarList3 *taskbar;
	UINT taskbarCreated;
	UINT progressCompleted;
	UINT progressTotal;
	SimulationJob simulationJob;
	ReplaceAwayJob replacementJob;
	wchar_t replacementPath[HARNESS_PATH_LENGTH];
	unsigned long threadLimit;
	HFONT font;
	HFONT headingFont;
	HBITMAP editIcon;
	COLORREF editIconColor;
	COLORREF editIconBackground;
	int editIconSize;
	UINT dpi;
	int metadataHeight;
	bool layoutPending;
	bool failed;
	bool temporary;
	bool missing;
	bool editingAway;
} WindowState;

static const wchar_t windowClass[] = L"FM24MatchHarness";
static const wchar_t pitchClass[] = L"FM24FixturePitch";
static const wchar_t unsavedPrefix[] = L"This fixture object is not saved. ";
static const wchar_t exportLink[] = L"Click here";
static const wchar_t missingFixture[] = L"Fixture cannot be found; restart the app";
static const wchar_t *const columns[] = {
	L"Slot",
	L"Player",
	L"Position",
	L"Role",
	L"Duty",
	L"Morale",
	L"Condition",
	L"Sharpness",
	L"Fatigue",
};
static const int columnWidths[] = {38, 210, 65, 185, 75, 65, 85, 90, 70};

static int scaled(const WindowState *state, int value) {
	return MulDiv(value, (int)state->dpi, 96);
}

static bool widen(const char *text, wchar_t *out, size_t size) {
	return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out, (int)size) > 0;
}

static void showError(HWND window, const wchar_t *message) {
	MessageBoxW(window, message, L"FM24 Match Harness", MB_OK | MB_ICONERROR);
}

static void layout(HWND window, WindowState *state);

static void releaseTaskbar(WindowState *state) {
	if (state->taskbar) {
		state->taskbar->lpVtbl->Release(state->taskbar);
		state->taskbar = NULL;
	}
}

static void updateTaskbarProgress(HWND window, WindowState *state) {
	if (!state->taskbar) {
		return;
	}
	TBPFLAG progress = !state->simulationThread ? TBPF_NOPROGRESS
										 : state->progressTotal   ? TBPF_NORMAL
																							: TBPF_INDETERMINATE;
	HRESULT result = state->taskbar->lpVtbl->SetProgressState(state->taskbar, window, progress);
	if (SUCCEEDED(result) && progress == TBPF_NORMAL) {
		result = state->taskbar->lpVtbl->SetProgressValue(
			state->taskbar, window, state->progressCompleted, state->progressTotal
		);
	}
	if (FAILED(result)) {
		fprintf(
			stderr,
			"fixture viewer: cannot update taskbar progress (HRESULT 0x%08lx)\n",
			(unsigned long)result
		);
		releaseTaskbar(state);
	}
}

static void
updateSimulationProgress(HWND window, WindowState *state, WPARAM wParam, LPARAM lParam) {
	UINT completed = (UINT)wParam;
	UINT homeWins = (UINT)((uint64_t)wParam >> 32);
	UINT total = (UINT)lParam;
	UINT draws = (UINT)((uint64_t)lParam >> 32);
	if (!state->simulationThread || total == 0 || total > INT_MAX || completed > total ||
			homeWins > completed || draws > completed - homeWins) {
		return;
	}
	UINT awayWins = completed - homeWins - draws;
	double percent = completed ? 100.0 / completed : 0;
	wchar_t summary[256];
	(void)swprintf(
		summary,
		256,
		L"n = %u: home %u (%.1f%%) / draw %u (%.1f%%) / away %u (%.1f%%)",
		completed,
		homeWins,
		homeWins * percent,
		draws,
		draws * percent,
		awayWins,
		awayWins * percent
	);
	SetWindowTextW(state->simulation[SIM_SUMMARY], summary);
	state->progressCompleted = completed;
	state->progressTotal = total;
	SendMessageW(state->progress, PBM_SETRANGE32, 0, total);
	SendMessageW(state->progress, PBM_SETPOS, completed, 0);
	updateTaskbarProgress(window, state);
}

static HRESULT initializeTaskbar(WindowState *state) {
	HRESULT result = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	if (FAILED(result)) {
		fprintf(
			stderr,
			"fixture viewer: cannot initialise COM for taskbar progress (HRESULT 0x%08lx)\n",
			(unsigned long)result
		);
		return result;
	}
	state->taskbarCreated = RegisterWindowMessageW(L"TaskbarButtonCreated");
	if (!state->taskbarCreated) {
		fprintf(
			stderr,
			"fixture viewer: cannot register taskbar notification (Windows error %lu)\n",
			GetLastError()
		);
	}
	return result;
}

static void createTaskbar(HWND window, WindowState *state) {
	releaseTaskbar(state);
	void *taskbar = NULL;
	HRESULT result =
		CoCreateInstance(&CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, &IID_ITaskbarList3, &taskbar);
	if (SUCCEEDED(result)) {
		state->taskbar = taskbar;
		result = state->taskbar->lpVtbl->HrInit(state->taskbar);
	}
	if (FAILED(result)) {
		fprintf(
			stderr,
			"fixture viewer: cannot initialise taskbar progress (HRESULT 0x%08lx)\n",
			(unsigned long)result
		);
		releaseTaskbar(state);
		return;
	}
	updateTaskbarProgress(window, state);
}

static DWORD WINAPI simulateFixture(LPVOID parameter) {
	SimulationJob *job = parameter;
	wchar_t *arguments[] = {
		L"fm24ctl.exe", L"run", job->input, job->seeds, L"--threads", job->threads, NULL, NULL};
	int count = 6;
	if (job->detailed) {
		arguments[count++] = L"--detailed";
	}
	return (DWORD)commands_runWithProgress(count, arguments, job->progressWindow);
}

static void startSimulation(HWND window, WindowState *state) {
	if (state->simulationThread || state->replacementThread) {
		return;
	}
	SimulationJob *job = &state->simulationJob;
	wchar_t *end = NULL;
	errno = 0;
	int length = GetWindowTextW(state->simulation[SIM_THREADS], job->threads, 16);
	unsigned long threads = wcstoul(job->threads, &end, 10);
	if (length == 0 || GetWindowTextLengthW(state->simulation[SIM_THREADS]) >= 16 || errno || *end ||
			wcsspn(job->threads, L"0123456789") != (size_t)length || threads < 1 ||
			threads > state->threadLimit) {
		wchar_t message[128];
		(void)swprintf(
			message, 128, L"Number of threads must be between 1 and %lu.", state->threadLimit
		);
		showError(window, message);
		SetFocus(state->simulation[SIM_THREADS]);
		return;
	}
	length = GetWindowTextW(state->simulation[SIM_SEEDS], job->seeds, 256);
	if (length == 0 || GetWindowTextLengthW(state->simulation[SIM_SEEDS]) >= 256 ||
			(wcscmp(job->seeds, L"keep") != 0 && wcsspn(job->seeds, L"0123456789,-") != (size_t)length)) {
		showError(
			window,
			L"Enter a seed pattern such as 1, 42, 1-1000, 1,3,5 or 1,4,7-70; "
			L"or keep to use the fixture's seed."
		);
		SetFocus(state->simulation[SIM_SEEDS]);
		return;
	}
	const wchar_t *input = state->savedFilename[0] ? state->savedFilename : state->path;
	if (!paths_absolute(input, job->input, HARNESS_PATH_LENGTH)) {
		showError(window, L"Cannot resolve the fixture path.");
		return;
	}
	job->detailed = SendMessageW(state->simulation[SIM_DETAILED], BM_GETCHECK, 0, 0) == BST_CHECKED;
	job->progressWindow = (uintptr_t)window;
	if (!SetTimer(window, SIMULATION_TIMER, 200, NULL)) {
		showError(window, L"Cannot monitor the simulation.");
		return;
	}
	state->simulationThread = CreateThread(NULL, 0, simulateFixture, job, 0, NULL);
	if (!state->simulationThread) {
		KillTimer(window, SIMULATION_TIMER);
		showError(window, L"Cannot start the simulation worker.");
		return;
	}
	state->progressCompleted = 0;
	state->progressTotal = 0;
	SetWindowTextW(
		state->simulation[SIM_SUMMARY], L"n = 0: home 0 (0.0%) / draw 0 (0.0%) / away 0 (0.0%)"
	);
	updateTaskbarProgress(window, state);
	for (unsigned i = 0; i < SIM_STATUS; ++i) {
		EnableWindow(state->simulation[i], FALSE);
	}
	EnableWindow(state->away[AWAY_ICON], FALSE);
	EnableWindow(state->away[AWAY_CONFIRM], FALSE);
	SendMessageW(state->progress, PBM_SETRANGE32, 0, 1);
	SendMessageW(state->progress, PBM_SETPOS, 0, 0);
	ShowWindow(state->progress, SW_SHOW);
	layout(window, state);
	SetFocus(state->tabs);
	SetWindowTextW(
		state->simulation[SIM_STATUS],
		L"Simulating fixture... Please wait before closing "
		L"the window."
	);
}

static void checkSimulation(HWND window, WindowState *state) {
	if (!state->simulationThread) {
		return;
	}
	DWORD wait = WaitForSingleObject(state->simulationThread, 0);
	if (wait == WAIT_TIMEOUT) {
		return;
	}
	DWORD result = 1;
	if (wait != WAIT_OBJECT_0 || !GetExitCodeThread(state->simulationThread, &result)) {
		KillTimer(window, SIMULATION_TIMER);
		showError(window, L"Cannot read the simulation result.");
		state->failed = true;
		DestroyWindow(window);
		return;
	}
	KillTimer(window, SIMULATION_TIMER);
	// The worker can finish after this timer was dequeued, leaving the final snapshot pending.
	MSG progress;
	while (PeekMessageW(
		&progress, window, HARNESS_PROGRESS_MESSAGE, HARNESS_PROGRESS_MESSAGE, PM_REMOVE
	)) {
		updateSimulationProgress(window, state, progress.wParam, progress.lParam);
	}
	CloseHandle(state->simulationThread);
	state->simulationThread = NULL;
	updateTaskbarProgress(window, state);
	ShowWindow(state->progress, SW_HIDE);
	layout(window, state);
	for (unsigned i = 0; i < SIM_STATUS; ++i) {
		EnableWindow(state->simulation[i], TRUE);
	}
	EnableWindow(state->away[AWAY_ICON], TRUE);
	EnableWindow(state->away[AWAY_CONFIRM], TRUE);
	const wchar_t *message =
		result == 0
			? L"Simulation complete. Results were saved beside the fixture; see the console for paths."
			: L"Simulation failed. Check the console and harness.log for details.";
	SetWindowTextW(state->simulation[SIM_STATUS], message);
	if (result != 0) {
		showError(window, message);
	}
}

static void selectTab(WindowState *state) {
	bool simulation = TabCtrl_GetCurSel(state->tabs) == 1;
	for (unsigned i = 0; i < 2; ++i) {
		ShowWindow(state->headings[i], simulation ? SW_HIDE : SW_SHOW);
		ShowWindow(state->notes[i], simulation ? SW_HIDE : SW_SHOW);
		ShowWindow(state->pitches[i], simulation ? SW_HIDE : SW_SHOW);
		ShowWindow(state->lists[i], simulation ? SW_HIDE : SW_SHOW);
	}
	for (unsigned i = 0; i < SIM_CONTROL_COUNT; ++i) {
		ShowWindow(state->simulation[i], simulation ? SW_SHOW : SW_HIDE);
	}
	for (unsigned i = 0; i < AWAY_CONTROL_COUNT; ++i) {
		bool visible = !simulation && (i == AWAY_ICON ? !state->editingAway : state->editingAway) &&
									 (i != AWAY_ERROR || GetWindowTextLengthW(state->away[i]) > 0);
		ShowWindow(state->away[i], visible ? SW_SHOW : SW_HIDE);
	}
}

static bool setText(HWND window, const char *text) {
	wchar_t wide[8192];
	return widen(text, wide, sizeof wide / sizeof wide[0]) && SetWindowTextW(window, wide);
}

static bool formatMetadata(HWND metadata, LONG start, LONG end, DWORD effects, COLORREF color) {
	CHARRANGE range = {.cpMin = start, .cpMax = end};
	SendMessageW(metadata, EM_EXSETSEL, 0, (LPARAM)&range);
	CHARFORMAT2W format = {.cbSize = sizeof format,
		.dwMask = CFM_BOLD | CFM_COLOR | CFM_UNDERLINE | CFM_LINK,
		.dwEffects = effects,
		.crTextColor = color};
	return SendMessageW(metadata, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&format) != 0;
}

static bool setMetadata(WindowState *state) {
	static const wchar_t filenamePrefix[] = L"Viewing fixture: ";
	static const char note[] =
		"NOTE: Condition/sharpness are percentages. Fatigue is signed jadedness (0 = fresh), not a "
		"percentage.";
	const FixtureData *data = state->data;
	const char *competition = strcmp(data->matchType, "Friendly") == 0 ? "Friendly"
														: data->competition[0]                   ? data->competition
																																		 : "Unknown competition";
	char text[8192];
	(void)snprintf(
		text,
		sizeof text,
		"%s @ %s, %s\r\n"
		"Weather: Unknown (serialized weather fields have not been mapped)\r\n"
		"Referee: %s\r\n\r\n%s",
		competition,
		data->venue[0] ? data->venue : "Unknown",
		data->when[0] ? data->when : "Unknown (no export date annotation)",
		data->officials[4][0] ? data->officials[4] : "Not stored/unassigned",
		note
	);
	wchar_t wide[8192];
	int length;
	if (state->temporary) {
		length = state->missing
							 ? swprintf(wide, 8192, L"%ls\r\n", missingFixture)
							 : swprintf(wide, 8192, L"%ls%ls to export it\r\n", unsavedPrefix, exportLink);
	} else {
		length = swprintf(wide, 8192, L"%ls%ls\r\n", filenamePrefix, state->filename);
	}
	if (length < 0 || !widen(text, wide + length, 8192 - (size_t)length) ||
			!SetWindowTextW(state->metadata, wide)) {
		return false;
	}
	COLORREF normal = GetSysColor(COLOR_WINDOWTEXT);
	if (!formatMetadata(state->metadata, 0, -1, 0, normal)) {
		return false;
	}
	bool ok;
	if (state->temporary) {
		LONG warningLength = (LONG)wcslen(state->missing ? missingFixture : unsavedPrefix);
		ok = formatMetadata(state->metadata, 0, warningLength, CFE_BOLD, RGB(255, 0, 0));
		if (ok && !state->missing) {
			ok = formatMetadata(
				state->metadata,
				warningLength,
				warningLength + (LONG)wcslen(exportLink),
				CFE_LINK | CFE_UNDERLINE,
				RGB_BLUE
			);
		}
	} else {
		LONG start = (LONG)(sizeof filenamePrefix / sizeof filenamePrefix[0] - 1);
		ok = formatMetadata(
			state->metadata, start, start + (LONG)wcslen(state->filename), CFE_BOLD, normal
		);
	}
	GETTEXTLENGTHEX textLength = {.flags = GTL_NUMCHARS | GTL_PRECISE, .codepage = 1200};
	LONG noteStart = (LONG)SendMessageW(state->metadata, EM_GETTEXTLENGTHEX, (WPARAM)&textLength, 0) -
									 (LONG)(sizeof note - 1);
	if (ok && noteStart >= 0) {
		ok = formatMetadata(state->metadata, noteStart, noteStart + 5, CFE_BOLD, normal);
	} else {
		ok = false;
	}
	SendMessageW(state->metadata, EM_SETSEL, 0, 0);
	return ok;
}

static bool fileMissing(const wchar_t *path) {
	if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
		return false;
	}
	DWORD error = GetLastError();
	return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

static void exportFixture(HWND window, WindowState *state) {
	if (state->replacementThread) {
		showError(window, L"Please wait for the away team replacement to finish before exporting.");
		return;
	}
	if (!state->temporary || state->missing) {
		return;
	}
	if (fileMissing(state->path)) {
		state->missing = true;
	} else {
		wchar_t filename[FIXTURE_EXPORT_FILENAME_LENGTH];
		time_t timestamp = time(NULL);
		if (timestamp == (time_t)-1) {
			fputs("fixture viewer: cannot read current time\n", stderr);
			showError(window, L"Cannot read current time.");
			return;
		}
		if (!fixtureExport_filename(
					state->data, (uint64_t)timestamp, filename, FIXTURE_EXPORT_FILENAME_LENGTH
				)) {
			fputs("fixture viewer: export filename is too long\n", stderr);
			showError(window, L"Fixture export filename is too long.");
			return;
		}
		if (CopyFileW(state->path, filename, TRUE)) {
			wcscpy(state->savedFilename, filename);
			state->filename = state->savedFilename;
			state->temporary = false;
		} else {
			DWORD error = GetLastError();
			if (fileMissing(state->path)) {
				state->missing = true;
			} else {
				wchar_t message[256];
				(void)swprintf(
					message,
					256,
					L"Cannot export fixture (Windows error %lu). No file was overwritten.",
					error
				);
				fprintf(stderr, "fixture viewer: cannot export fixture (Windows error %lu)\n", error);
				showError(window, message);
				return;
			}
		}
	}
	if (state->missing) {
		fputs("fixture viewer: Fixture cannot be found; restart the app\n", stderr);
	}
	if (!setMetadata(state)) {
		showError(window, L"Cannot update fixture summary.");
		state->failed = true;
		DestroyWindow(window);
		return;
	}
	SendMessageW(state->metadata, EM_REQUESTRESIZE, 0, 0);
}

static bool setHeadings(WindowState *state) {
	for (unsigned side = 0; side < 2; ++side) {
		const FixtureTeam *team = &state->data->teams[side];
		char text[2 * FIXTURE_TEXT + 128];
		(void)snprintf(
			text,
			sizeof text,
			"%s: %s - %s [%s]",
			side ? "Away" : "Home",
			team->name,
			team->tactic[0] ? team->tactic : "Unnamed tactic",
			team->formation[0] ? team->formation : "Formation unknown"
		);
		if (side == 1 && state->editingAway) {
			(void)snprintf(text, sizeof text, "Away: ");
		}
		if (!widen(text, state->headingText[side], sizeof state->headingText[side] / sizeof(wchar_t))) {
			return false;
		}
		if (!setText(
					state->notes[side],
					team->lineupKnown ? "Stored starting XI; pitch numbers are tactic slots."
														: "AI/unknown control: assignments unmapped. Squad listed below; AI "
															"may replace this tactic."
				)) {
			return false;
		}
	}
	return true;
}

static void percentage(FixtureNumber number, char *out, size_t size) {
	if (!number.known) {
		(void)snprintf(out, size, "-");
	} else if (number.value > 10000) {
		(void)snprintf(out, size, "Invalid: %llu", (unsigned long long)number.value);
	} else {
		(void)snprintf(out, size, "%.2f%%", (double)number.value / 100.0);
	}
}

static void playerCells(const FixturePlayer *player, char cells[9][FIXTURE_TEXT]) {
	if (!player) {
		return;
	}
	percentage(player->condition, cells[6], FIXTURE_TEXT);
	percentage(player->sharpness[0], cells[7], FIXTURE_TEXT);
	if (player->sharpness[0].known && player->sharpness[1].known &&
			player->sharpness[0].value != player->sharpness[1].value) {
		if (player->sharpness[0].value > 10000 || player->sharpness[1].value > 10000) {
			(void)snprintf(cells[7], FIXTURE_TEXT, "Invalid sharpness");
		} else {
			(void)snprintf(
				cells[7],
				FIXTURE_TEXT,
				"%.2f / %.2f%%",
				(double)player->sharpness[0].value / 100.0,
				(double)player->sharpness[1].value / 100.0
			);
		}
	}
	if (player->fatigue.known) {
		int value = (int)player->fatigue.value;
		(void)snprintf(cells[8], FIXTURE_TEXT, "%d", value >= 32768 ? value - 65536 : value);
	}
	if (player->morale.known) {
		(void)snprintf(cells[5], FIXTURE_TEXT, "%llu", (unsigned long long)player->morale.value);
	}
}

static void roleTitle(const char *name, char *out, size_t size) {
	bool capital = true;
	size_t i = 0;
	for (; name[i] && i + 1 < size; ++i) {
		char c = name[i];
		out[i] = (char)(c == '_' ? ' ' : capital && c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
		capital = c == '_';
	}
	out[i] = 0;
}

static bool insertListRow(HWND list, unsigned row, char cells[9][FIXTURE_TEXT]) {
	for (unsigned cell = 0; cell < 9; ++cell) {
		wchar_t wide[FIXTURE_TEXT];
		if (!widen(cells[cell], wide, FIXTURE_TEXT)) {
			return false;
		}
		LVITEMW item = {.mask = LVIF_TEXT, .iItem = (int)row, .iSubItem = (int)cell, .pszText = wide};
		bool ok = cell == 0 ? SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item) != -1
												: SendMessageW(list, LVM_SETITEMTEXTW, row, (LPARAM)&item) != FALSE;
		if (!ok) {
			return false;
		}
	}
	return true;
}

static bool fillList(WindowState *state, unsigned side) {
	HWND list = state->lists[side];
	const FixtureTeam *team = &state->data->teams[side];
	SendMessageW(list, WM_SETREDRAW, FALSE, 0);
	ListView_DeleteAllItems(list);
	unsigned count = (unsigned)team->playerCount.value + (team->lineupKnown ? 0 : FIXTURE_SLOTS);
	bool ok = true;
	for (unsigned i = 0; i < count && ok; ++i) {
		char cells[9][FIXTURE_TEXT] = {"", "", "-", "-", "-", "-", "-", "-", "-"};
		const FixturePlayer *player = NULL;
		if (team->lineupKnown) {
			player = &team->players[i];
		} else if (i >= FIXTURE_SLOTS) {
			player = &team->players[i - FIXTURE_SLOTS];
		}
		(void)snprintf(
			cells[1], FIXTURE_TEXT, "%s", player && player->name[0] ? player->name : "<Unknown>"
		);
		if (i < FIXTURE_SLOTS) {
			(void)snprintf(cells[0], FIXTURE_TEXT, "%u", i);
			const FixtureSlot *slot = &team->slots[i];
			if (slot->position.known) {
				fixtureData_position(slot->position.value, cells[2], FIXTURE_TEXT);
			}
			if (slot->role.known) {
				roleTitle(fixtureData_role(slot->role.value), cells[3], FIXTURE_TEXT);
				(void)snprintf(cells[4], FIXTURE_TEXT, "%s", fixtureData_duty(slot->role.value));
			}
		} else {
			(void)snprintf(cells[2], FIXTURE_TEXT, "%s", team->lineupKnown ? "Sub" : "");
			cells[3][0] = 0;
			cells[4][0] = 0;
		}
		playerCells(player, cells);
		ok = insertListRow(list, i, cells);
	}
	SendMessageW(list, WM_SETREDRAW, TRUE, 0);
	InvalidateRect(list, NULL, TRUE);
	return ok;
}

static void removeReplacement(const wchar_t *path) {
	if (path[0] && !DeleteFileW(path)) {
		fprintf(
			stderr, "cannot remove replacement fixture %ls (Windows error %lu)\n", path, GetLastError()
		);
	}
}

static void awayError(HWND window, WindowState *state, const char *error) {
	fprintf(stderr, "fixture viewer: %s\n", error);
	if (!setText(state->away[AWAY_ERROR], error)) {
		SetWindowTextW(state->away[AWAY_ERROR], L"Cannot display replacement error (invalid UTF-8).");
	}
	ShowWindow(state->away[AWAY_ERROR], TabCtrl_GetCurSel(state->tabs) == 0 ? SW_SHOW : SW_HIDE);
	layout(window, state);
}

static void editAway(HWND window, WindowState *state, bool editing) {
	if (state->replacementThread || (editing && state->simulationThread)) {
		return;
	}
	if (editing && !setText(state->away[AWAY_TEAM], state->data->teams[1].name)) {
		showError(window, L"Cannot display the away team name.");
		return;
	}
	state->editingAway = editing;
	SetWindowTextW(state->away[AWAY_ERROR], L"");
	if (!setHeadings(state)) {
		showError(window, L"Cannot update team headings.");
		state->failed = true;
		DestroyWindow(window);
		return;
	}
	selectTab(state);
	layout(window, state);
	SetFocus(state->away[editing ? AWAY_TEAM : AWAY_ICON]);
	if (editing) {
		SendMessageW(state->away[AWAY_TEAM], EM_SETSEL, 0, -1);
	}
}

static DWORD WINAPI replaceAway(LPVOID parameter) {
	ReplaceAwayJob *job = parameter;
	wchar_t directory[HARNESS_PATH_LENGTH];
	wchar_t temporary[HARNESS_PATH_LENGTH];
	DWORD length = GetTempPathW(HARNESS_PATH_LENGTH, directory);
	if (!length || length >= HARNESS_PATH_LENGTH ||
			!GetTempFileNameW(directory, L"fma", 0, temporary)) {
		(void)snprintf(
			job->error,
			sizeof job->error,
			"Cannot create a replacement fixture (Windows error %lu).",
			GetLastError()
		);
		return 1;
	}
	int count = swprintf(job->output, HARNESS_PATH_LENGTH, L"%ls.json", temporary);
	if (count < 0 || count >= HARNESS_PATH_LENGTH || !MoveFileW(temporary, job->output)) {
		(void)snprintf(
			job->error,
			sizeof job->error,
			"Cannot create replacement JSON (Windows error %lu).",
			GetLastError()
		);
		removeReplacement(temporary);
		job->output[0] = 0;
		return 1;
	}
	wchar_t *arguments[] = {
		L"fm24ctl.exe", L"replace-away", job->input, job->team, job->output, NULL};
	int result = commands_replaceAwayWithResult(5, arguments, job->error, sizeof job->error);
	if (result == 0 && !fixtureLoad_open(job->output, job->data, job->error, sizeof job->error)) {
		result = 1;
	}
	return (DWORD)result;
}

static void startReplacement(HWND window, WindowState *state) {
	if (!state->editingAway || state->simulationThread || state->replacementThread) {
		return;
	}
	ReplaceAwayJob *job = &state->replacementJob;
	int length = GetWindowTextW(state->away[AWAY_TEAM], job->team, 256);
	char team[256];
	if (!length || GetWindowTextLengthW(state->away[AWAY_TEAM]) >= 256 ||
			wcsspn(job->team, L" \t") == (size_t)length || wcspbrk(job->team, L"\r\n") ||
			!paths_toUtf8(job->team, team, sizeof team)) {
		awayError(window, state, "Enter a team ID or club short name (up to 255 UTF-8 bytes).");
		SetFocus(state->away[AWAY_TEAM]);
		return;
	}
	const wchar_t *input = state->savedFilename[0] ? state->savedFilename : state->path;
	if (!paths_absolute(input, job->input, HARNESS_PATH_LENGTH)) {
		awayError(window, state, "Cannot resolve the fixture path.");
		return;
	}
	job->data = malloc(sizeof *job->data);
	if (!job->data) {
		awayError(window, state, "Out of memory loading the replacement fixture.");
		return;
	}
	job->output[0] = 0;
	job->error[0] = 0;
	if (!SetTimer(window, REPLACE_TIMER, 200, NULL)) {
		free(job->data);
		job->data = NULL;
		awayError(window, state, "Cannot monitor the away team replacement.");
		return;
	}
	state->replacementThread = CreateThread(NULL, 0, replaceAway, job, 0, NULL);
	if (!state->replacementThread) {
		KillTimer(window, REPLACE_TIMER);
		free(job->data);
		job->data = NULL;
		awayError(window, state, "Cannot start the away team replacement.");
		return;
	}
	SetWindowTextW(state->away[AWAY_ERROR], L"");
	ShowWindow(state->away[AWAY_ERROR], SW_HIDE);
	for (unsigned i = AWAY_TEAM; i <= AWAY_CANCEL; ++i) {
		EnableWindow(state->away[i], FALSE);
	}
	EnableWindow(state->simulation[SIM_SUBMIT], FALSE);
	SetFocus(state->tabs);
}

static void checkReplacement(HWND window, WindowState *state) {
	if (!state->replacementThread) {
		return;
	}
	DWORD wait = WaitForSingleObject(state->replacementThread, 0);
	if (wait == WAIT_TIMEOUT) {
		return;
	}
	DWORD result = 1;
	if (wait != WAIT_OBJECT_0 || !GetExitCodeThread(state->replacementThread, &result)) {
		KillTimer(window, REPLACE_TIMER);
		showError(window, L"Cannot read the away team replacement result.");
		state->failed = true;
		DestroyWindow(window);
		return;
	}
	KillTimer(window, REPLACE_TIMER);
	CloseHandle(state->replacementThread);
	state->replacementThread = NULL;
	for (unsigned i = AWAY_TEAM; i <= AWAY_CANCEL; ++i) {
		EnableWindow(state->away[i], TRUE);
	}
	EnableWindow(state->simulation[SIM_SUBMIT], TRUE);
	ReplaceAwayJob *job = &state->replacementJob;
	if (result == 0) {
		removeReplacement(state->replacementPath);
		wcscpy(state->replacementPath, job->output);
		job->output[0] = 0;
		state->path = state->replacementPath;
		state->savedFilename[0] = 0;
		state->temporary = true;
		state->missing = false;
		*state->data = *job->data;
		state->editingAway = false;
		if (!setMetadata(state) || !setHeadings(state) || !fillList(state, 0) || !fillList(state, 1)) {
			showError(window, L"Cannot display the replacement fixture.");
			state->failed = true;
			DestroyWindow(window);
		} else {
			for (unsigned side = 0; side < 2; ++side) {
				InvalidateRect(state->pitches[side], NULL, TRUE);
			}
			selectTab(state);
			layout(window, state);
			SetFocus(TabCtrl_GetCurSel(state->tabs) == 0 ? state->away[AWAY_ICON] : state->tabs);
		}
	} else {
		awayError(window, state, job->error);
		if (TabCtrl_GetCurSel(state->tabs) == 0) {
			SetFocus(state->away[AWAY_TEAM]);
		}
	}
	removeReplacement(job->output);
	job->output[0] = 0;
	free(job->data);
	job->data = NULL;
}

static int CALLBACK
fontFound(const LOGFONTW *font, const TEXTMETRICW *metrics, DWORD type, LPARAM found) {
	(void)font;
	(void)metrics;
	(void)type;
	*(bool *)found = true; // NOLINT(performance-no-int-to-ptr): LPARAM carries the caller's pointer.
	return 0;
}

static bool setFonts(WindowState *state) {
	HDC dc = GetDC(state->metadata);
	if (!dc) {
		return false;
	}
	LOGFONTW family = {.lfCharSet = DEFAULT_CHARSET, .lfFaceName = L"Calibri"};
	bool calibri = false;
	EnumFontFamiliesExW(dc, &family, fontFound, (LPARAM)&calibri, 0);
	ReleaseDC(state->metadata, dc);
	HFONT font = CreateFontW(
		-MulDiv(10, (int)state->dpi, 72),
		0,
		0,
		0,
		FW_NORMAL,
		FALSE,
		FALSE,
		FALSE,
		DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY,
		DEFAULT_PITCH,
		L"Segoe UI"
	);
	HFONT heading = CreateFontW(
		-MulDiv(13, (int)state->dpi, 72),
		0,
		0,
		0,
		FW_SEMIBOLD,
		FALSE,
		FALSE,
		FALSE,
		DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY,
		DEFAULT_PITCH,
		calibri ? L"Calibri" : L"Segoe UI"
	);
	if (!font || !heading) {
		if (font) {
			DeleteObject(font);
		}
		if (heading) {
			DeleteObject(heading);
		}
		return false;
	}
	SendMessageW(state->metadata, WM_SETFONT, (WPARAM)font, TRUE);
	SendMessageW(state->tabs, WM_SETFONT, (WPARAM)font, TRUE);
	for (unsigned i = 0; i < SIM_CONTROL_COUNT; ++i) {
		SendMessageW(state->simulation[i], WM_SETFONT, (WPARAM)font, TRUE);
	}
	for (unsigned i = 0; i < AWAY_CONTROL_COUNT; ++i) {
		SendMessageW(state->away[i], WM_SETFONT, (WPARAM)font, TRUE);
	}
	for (unsigned i = 0; i < 2; ++i) {
		SendMessageW(state->headings[i], WM_SETFONT, (WPARAM)heading, TRUE);
		SendMessageW(state->notes[i], WM_SETFONT, (WPARAM)font, TRUE);
		SendMessageW(state->lists[i], WM_SETFONT, (WPARAM)font, TRUE);
		SendMessageW(state->pitches[i], WM_SETFONT, (WPARAM)font, TRUE);
		for (unsigned column = 0; column < 9; ++column) {
			ListView_SetColumnWidth(state->lists[i], (int)column, scaled(state, columnWidths[column]));
		}
	}
	if (state->font) {
		DeleteObject(state->font);
		DeleteObject(state->headingFont);
	}
	state->font = font;
	state->headingFont = heading;
	return true;
}

static void layout(HWND window, WindowState *state) {
	RECT bounds;
	GetClientRect(window, &bounds);
	int margin = scaled(state, 12);
	int width = bounds.right - 2 * margin;
	int progressHeight = state->simulationThread ? scaled(state, 12) : 0;
	MoveWindow(
		state->progress, 0, bounds.bottom - scaled(state, 12), bounds.right, scaled(state, 12), TRUE
	);
	MoveWindow(state->metadata, margin, margin, width, state->metadataHeight, FALSE);
	SendMessageW(state->metadata, EM_REQUESTRESIZE, 0, 0);
	MoveWindow(state->metadata, margin, margin, width, state->metadataHeight, TRUE);
	int top = margin + state->metadataHeight + margin;
	MoveWindow(state->tabs, margin, top, width, bounds.bottom - top - margin - progressHeight, TRUE);
	RECT page;
	GetClientRect(state->tabs, &page);
	TabCtrl_AdjustRect(state->tabs, FALSE, &page);
	MapWindowPoints(state->tabs, window, (POINT *)&page, 2);
	int left = page.left + margin;
	top = page.top + margin;
	width = page.right - left - margin;
	static const struct {
		int x, y, width, height;
	} positions[SIM_CONTROL_COUNT] = {
		{0, 4, 180, 24},
		{192, 0, 58, 28},
		{250, 0, 20, 28},
		{0, 46, 100, 24},
		{192, 42, 220, 30},
		{0, 92, 220, 28},
		{0, 136, 180, 34},
		{0, 238, 620, 80},
		{0, 186, 620, 44},
	};
	for (unsigned i = 0; i < SIM_CONTROL_COUNT; ++i) {
		MoveWindow(
			state->simulation[i],
			left + scaled(state, positions[i].x),
			top + scaled(state, positions[i].y),
			scaled(state, positions[i].width),
			scaled(state, positions[i].height),
			TRUE
		);
	}
	int panelHeight = (page.bottom - top - 2 * margin) / 2;
	for (unsigned i = 0; i < 2; ++i) {
		int y = top + (int)i * (panelHeight + margin);
		int header = scaled(state, 27);
		int note = scaled(state, 22);
		int headingWidth = width;
		if (i == 1) {
			HDC dc = GetDC(window);
			HGDIOBJ oldFont = SelectObject(dc, state->headingFont);
			SIZE extent = {0};
			GetTextExtentPoint32W(dc, state->headingText[i], (int)wcslen(state->headingText[i]), &extent);
			SelectObject(dc, oldFont);
			headingWidth = extent.cx + scaled(state, 4);
			if (headingWidth > width - header) {
				headingWidth = width - header;
			}
			MoveWindow(
				state->away[AWAY_ICON],
				left + headingWidth,
				y + (extent.cy - header) / 2,
				header,
				header,
				TRUE
			);
			if (state->editingAway) {
				int x = left + headingWidth;
				int gap = scaled(state, 6);
				int inputWidth = scaled(state, 190);
				int buttonWidth = scaled(state, 70);
				MoveWindow(state->away[AWAY_TEAM], x, y, inputWidth, header, TRUE);
				x += inputWidth + gap;
				MoveWindow(state->away[AWAY_CONFIRM], x, y, buttonWidth, header, TRUE);
				x += buttonWidth + gap;
				MoveWindow(state->away[AWAY_CANCEL], x, y, buttonWidth, header, TRUE);
				x += buttonWidth + gap;
				int errorWidth = left + width - x;
				int errorHeight = header - scaled(state, 4);
				SelectObject(dc, state->font);
				wchar_t text[2 * FIXTURE_TEXT + 128];
				GetWindowTextW(state->away[AWAY_ERROR], text, 2 * FIXTURE_TEXT + 128);
				RECT errorBounds = {0, 0, errorWidth - GetSystemMetricsForDpi(SM_CXVSCROLL, state->dpi), 0};
				DrawTextW(dc, text, -1, &errorBounds, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
				SelectObject(dc, oldFont);
				if (errorBounds.bottom > errorHeight) {
					errorHeight = errorBounds.bottom;
					if (errorHeight > scaled(state, 60)) {
						errorHeight = scaled(state, 60);
					}
					header = errorHeight + scaled(state, 4);
				}
				MoveWindow(state->away[AWAY_ERROR], x, y + scaled(state, 4), errorWidth, errorHeight, TRUE);
				ShowScrollBar(state->away[AWAY_ERROR], SB_VERT, errorBounds.bottom > errorHeight);
			}
			ReleaseDC(window, dc);
		}
		MoveWindow(state->headings[i], left, y, headingWidth, scaled(state, 27), TRUE);
		// Ellipsis controls can alter their text; restore the full heading after resizing.
		if (!SetWindowTextW(state->headings[i], state->headingText[i])) {
			showError(window, L"Cannot update team headings.");
			state->failed = true;
			DestroyWindow(window);
			return;
		}
		MoveWindow(state->notes[i], left, y + header, width, note, TRUE);
		int height = panelHeight - header - note;
		int pitchWidth = scaled(state, 220);
		MoveWindow(state->pitches[i], left, y + header + note, pitchWidth, height, TRUE);
		int offset = pitchWidth + margin;
		MoveWindow(state->lists[i], left + offset, y + header + note, width - offset, height, TRUE);
	}
}

static bool createAwayControls(HWND window, WindowState *state) {
	static const struct {
		const wchar_t *className;
		const wchar_t *text;
		DWORD style;
		DWORD extended;
	} controls[AWAY_CONTROL_COUNT] = {
		{L"BUTTON", L"Replace away team", WS_TABSTOP | BS_OWNERDRAW, 0},
		{L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE},
		{L"BUTTON", L"Confirm", WS_TABSTOP | BS_PUSHBUTTON, 0},
		{L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, 0},
		{L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 0},
	};
	for (unsigned i = 0; i < AWAY_CONTROL_COUNT; ++i) {
		state->away[i] = CreateWindowExW(
			controls[i].extended,
			controls[i].className,
			controls[i].text,
			WS_CHILD | controls[i].style,
			0,
			0,
			0,
			0,
			window,
			NULL,
			GetModuleHandleW(NULL),
			NULL
		);
		if (!state->away[i]) {
			return false;
		}
	}
	SendMessageW(state->away[AWAY_TEAM], EM_SETLIMITTEXT, 255, 0);
	SendMessageW(
		state->away[AWAY_TEAM], EM_SETCUEBANNER, TRUE, (LPARAM)L"Team ID or club short name"
	);
	return true;
}

static bool createSimulationControls(HWND window, WindowState *state) {
	static const struct {
		const wchar_t *className;
		const wchar_t *text;
		DWORD style;
		DWORD extended;
	} controls[SIM_CONTROL_COUNT] = {
		{L"STATIC", L"Number of threads:", 0, 0},
		{L"EDIT", L"", WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE},
		{UPDOWN_CLASSW, L"", UDS_SETBUDDYINT | UDS_ARROWKEYS | UDS_NOTHOUSANDS, 0},
		{L"STATIC", L"Seed pattern:", 0, 0},
		{L"EDIT", L"keep", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE},
		{L"BUTTON", L"Generate detailed reports?", WS_TABSTOP | BS_AUTOCHECKBOX, 0},
		{L"BUTTON", L"Simulate fixture", WS_TABSTOP | BS_PUSHBUTTON, 0},
		{L"STATIC", L"Results are saved beside the fixture, using its filename with .csv.", 0, 0},
		{L"STATIC", L"", 0, 0},
	};
	for (unsigned i = 0; i < SIM_CONTROL_COUNT; ++i) {
		state->simulation[i] = CreateWindowExW(
			controls[i].extended,
			controls[i].className,
			controls[i].text,
			WS_CHILD | controls[i].style,
			0,
			0,
			0,
			0,
			window,
			NULL,
			GetModuleHandleW(NULL),
			NULL
		);
		if (!state->simulation[i]) {
			return false;
		}
	}
	state->threadLimit = commands_defaultThreads();
	wchar_t label[64];
	(void)swprintf(label, 64, L"Number of threads (1-%lu):", state->threadLimit);
	if (!SetWindowTextW(state->simulation[SIM_THREADS_LABEL], label)) {
		return false;
	}
	SendMessageW(state->simulation[SIM_THREADS], EM_SETLIMITTEXT, 15, 0);
	SendMessageW(state->simulation[SIM_SEEDS], EM_SETLIMITTEXT, 255, 0);
	SendMessageW(state->simulation[SIM_SEEDS], EM_SETCUEBANNER, TRUE, (LPARAM)L"e.g. 1,4,7-70");
	SendMessageW(
		state->simulation[SIM_SPINNER], UDM_SETBUDDY, (WPARAM)state->simulation[SIM_THREADS], 0
	);
	SendMessageW(state->simulation[SIM_SPINNER], UDM_SETRANGE32, 1, (LPARAM)state->threadLimit);
	SendMessageW(state->simulation[SIM_SPINNER], UDM_SETPOS32, 0, (LPARAM)state->threadLimit);
	return true;
}

static bool createControls(HWND window, WindowState *state) {
	HINSTANCE instance = GetModuleHandleW(NULL);
	state->metadata = CreateWindowExW(
		WS_EX_CLIENTEDGE,
		MSFTEDIT_CLASS,
		L"",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY,
		0,
		0,
		0,
		0,
		window,
		NULL,
		instance,
		NULL
	);
	state->tabs = CreateWindowExW(
		0,
		WC_TABCONTROLW,
		L"",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS,
		0,
		0,
		0,
		0,
		window,
		NULL,
		instance,
		NULL
	);
	state->progress = CreateWindowExW(
		0,
		PROGRESS_CLASSW,
		L"Simulation progress",
		WS_CHILD | PBS_SMOOTH,
		0,
		0,
		0,
		0,
		window,
		NULL,
		instance,
		NULL
	);
	if (!state->metadata || !state->tabs || !state->progress) {
		return false;
	}
	SendMessageW(state->metadata, EM_SETLIMITTEXT, 8192, 0);
	SendMessageW(state->metadata, EM_SETEVENTMASK, 0, ENM_REQUESTRESIZE | ENM_LINK);
	TCITEMW tab = {.mask = TCIF_TEXT, .pszText = L"Squads"};
	if (SendMessageW(state->tabs, TCM_INSERTITEMW, 0, (LPARAM)&tab) == -1) {
		return false;
	}
	tab.pszText = L"Simulation";
	if (SendMessageW(state->tabs, TCM_INSERTITEMW, 1, (LPARAM)&tab) == -1) {
		return false;
	}
	for (unsigned i = 0; i < 2; ++i) {
		state->headings[i] = CreateWindowExW(
			0,
			L"STATIC",
			L"",
			WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS,
			0,
			0,
			0,
			0,
			window,
			NULL,
			instance,
			NULL
		);
		state->notes[i] = CreateWindowExW(
			0,
			L"STATIC",
			L"",
			WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS,
			0,
			0,
			0,
			0,
			window,
			NULL,
			instance,
			NULL
		);
		state->pitches[i] = CreateWindowExW(
			0,
			pitchClass,
			L"Stored formation",
			WS_CHILD | WS_VISIBLE,
			0,
			0,
			0,
			0,
			window,
			NULL,
			instance,
			&state->data->teams[i]
		);
		state->lists[i] = CreateWindowExW(
			WS_EX_CLIENTEDGE,
			WC_LISTVIEWW,
			i ? L"Away team" : L"Home team",
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
			0,
			0,
			0,
			0,
			window,
			NULL,
			instance,
			NULL
		);
		if (!state->headings[i] || !state->notes[i] || !state->pitches[i] || !state->lists[i]) {
			return false;
		}
		ListView_SetExtendedListViewStyle(
			state->lists[i],
			LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP | LVS_EX_GRIDLINES
		);
		for (unsigned column = 0; column < 9; ++column) {
			LVCOLUMNW definition = {.mask = LVCF_TEXT | LVCF_WIDTH,
				.pszText = (wchar_t *)columns[column],
				.cx = scaled(state, columnWidths[column])};
			if (SendMessageW(state->lists[i], LVM_INSERTCOLUMNW, column, (LPARAM)&definition) == -1) {
				return false;
			}
		}
	}
	bool ok = createAwayControls(window, state) && createSimulationControls(window, state) &&
						setFonts(state) && setMetadata(state) && setHeadings(state) && fillList(state, 0) &&
						fillList(state, 1);
	if (ok) {
		selectTab(state);
	}
	return ok;
}

static void pitchPoint(uint64_t mask, int *x, int *y) {
	uint64_t position = mask & 0xFFFF;
	*x = 50;
	*y = position == 1      ? 91
			 : position <= 16   ? 75
			 : position <= 128  ? 59
			 : position <= 1024 ? 45
			 : position <= 8192 ? 30
													: 12;
	if (position == 4 || position == 32 || position == 256 || position == 2048) {
		*x = 85;
	} else if (position == 8 || position == 64 || position == 512 || position == 4096) {
		*x = 15;
	} else if (mask & (UINT64_C(1) << 20)) {
		*x = 35;
	} else if (mask & (UINT64_C(1) << 21)) {
		*x = 65;
	}
}

static void drawPitch(HWND window, HDC dc, const FixtureTeam *team) {
	RECT bounds;
	GetClientRect(window, &bounds);
	SetDCBrushColor(dc, RGB(32, 100, 65));
	FillRect(dc, &bounds, (HBRUSH)GetStockObject(DC_BRUSH));
	SetDCPenColor(dc, RGB(220, 236, 224));
	HGDIOBJ oldPen = SelectObject(dc, GetStockObject(DC_PEN));
	HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
	Rectangle(dc, 7, 7, bounds.right - 7, bounds.bottom - 7);
	MoveToEx(dc, 7, bounds.bottom / 2, NULL);
	LineTo(dc, bounds.right - 7, bounds.bottom / 2);
	int radius = bounds.right / 9;
	Ellipse(
		dc,
		bounds.right / 2 - radius,
		bounds.bottom / 2 - radius,
		bounds.right / 2 + radius,
		bounds.bottom / 2 + radius
	);
	Rectangle(dc, bounds.right / 4, 7, bounds.right * 3 / 4, bounds.bottom / 6);
	Rectangle(dc, bounds.right / 4, bounds.bottom * 5 / 6, bounds.right * 3 / 4, bounds.bottom - 7);
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(dc, RGB(255, 255, 255));
	for (unsigned i = 0; i < FIXTURE_SLOTS; ++i) {
		if (!team->slots[i].position.known) {
			continue;
		}
		char position[16];
		fixtureData_position(team->slots[i].position.value, position, sizeof position);
		if (strcmp(position, "Unknown") == 0) {
			continue;
		}
		int x;
		int y;
		pitchPoint(team->slots[i].position.value, &x, &y);
		x = bounds.right * x / 100;
		y = bounds.bottom * y / 100;
		wchar_t label[32];
		(void)swprintf(label, 32, L"%u", i);
		int height = bounds.bottom / 12;
		if (height < 14) {
			height = 14;
		}
		RECT text = {x - 22, y - height / 2, x + 22, y + height / 2};
		Ellipse(dc, x - height / 2, y - height / 2, x + height / 2, y + height / 2);
		DrawTextW(dc, label, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
	}
	SelectObject(dc, oldBrush);
	SelectObject(dc, oldPen);
}

// Win32 messages and window storage carry pointers through pointer-sized integers.
// NOLINTBEGIN(performance-no-int-to-ptr)
static LRESULT CALLBACK pitchProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	if (message == WM_NCCREATE) {
		const CREATESTRUCTW *create = (const CREATESTRUCTW *)lParam;
		SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
	}
	if (message == WM_SETFONT) {
		SetWindowLongPtrW(window, 0, (LONG_PTR)wParam);
		if (lParam) {
			InvalidateRect(window, NULL, TRUE);
		}
		return 0;
	}
	if (message == WM_PAINT) {
		PAINTSTRUCT paint;
		HDC dc = BeginPaint(window, &paint);
		const FixtureTeam *team = (const FixtureTeam *)GetWindowLongPtrW(window, GWLP_USERDATA);
		if (team) {
			HFONT font = (HFONT)GetWindowLongPtrW(window, 0);
			HGDIOBJ oldFont = font ? SelectObject(dc, font) : NULL;
			drawPitch(window, dc, team);
			if (oldFont) {
				SelectObject(dc, oldFont);
			}
		}
		EndPaint(window, &paint);
		return 0;
	}
	return DefWindowProcW(window, message, wParam, lParam);
}

static LRESULT metadataNotification(HWND window, WindowState *state, const NMHDR *notification) {
	if (notification->hwndFrom != state->metadata) {
		return 0;
	}
	if (notification->code == EN_LINK) {
		const ENLINK *link = (const ENLINK *)notification;
		if (link->msg == WM_LBUTTONUP) {
			exportFixture(window, state);
			return 1;
		}
		return 0;
	}
	if (notification->code != EN_REQUESTRESIZE) {
		return 0;
	}
	const REQRESIZE *resize = (const REQRESIZE *)notification;
	int height =
		resize->rc.bottom - resize->rc.top + 2 * GetSystemMetricsForDpi(SM_CYEDGE, state->dpi);
	if (height == state->metadataHeight) {
		return 0;
	}
	state->metadataHeight = height;
	// Reflow can finish during painting, after layout has already positioned the summary.
	if (!state->layoutPending) {
		state->layoutPending = true;
		if (!PostMessageW(window, WM_FIXTURE_LAYOUT, 0, 0)) {
			showError(window, L"Cannot schedule summary layout.");
			state->failed = true;
			DestroyWindow(window);
		}
	}
	return 0;
}

static bool drawEditPath(HDC dc, int size, COLORREF color) {
	// assets/edit.svg in 1/1000 viewBox units; SVG arcs are cubic GDI curves.
	static const POINT points[] = {
		{12965, 5462},
		{7986, 5470},
		{4952, 5476},
		{2496, 7936},
		{2496, 10970},
		{2496, 24000},
		{2495, 25459},
		{3074, 26859},
		{4106, 27890},
		{5137, 28922},
		{6537, 29501},
		{7996, 29500},
		{20999, 29500},
		{24037, 29500},
		{26499, 27038},
		{26499, 24000},
		{26499, 18996},
		{26499, 18168},
		{25827, 17496},
		{24999, 17496},
		{24171, 17496},
		{23499, 18168},
		{23499, 18996},
		{23499, 24000},
		{23499, 25381},
		{22380, 26500},
		{20999, 26500},
		{7996, 26500},
		{6615, 26500},
		{5496, 25381},
		{5496, 24000},
		{5496, 10970},
		{5496, 9591},
		{6613, 8472},
		{7992, 8470},
		{12971, 8462},
		{13778, 8431},
		{14416, 7767},
		{14414, 6959},
		{14413, 6151},
		{13772, 5490},
		{12965, 5462},
		{20046, 6411},
		{13201, 13257},
		{13064, 13394},
		{12970, 13568},
		{12930, 13758},
		{11849, 18910},
		{11780, 19239},
		{11881, 19581},
		{12117, 19819},
		{12354, 20058},
		{12694, 20162},
		{13024, 20096},
		{18218, 19058},
		{18411, 19019},
		{18589, 18924},
		{18729, 18784},
		{25574, 11939},
		{21461, 4997},
		{26988, 10525},
		{28100, 9414},
		{29625, 7887},
		{29625, 5414},
		{28100, 3887},
		{28099, 3886},
		{26572, 2361},
		{24099, 2361},
		{22572, 3886},
	};
	static const BYTE types[] = {
		6,
		2,
		4,
		4,
		4,
		2,
		4,
		4,
		4,
		4,
		4,
		4,
		2,
		4,
		4,
		4,
		2,
		4,
		4,
		4,
		4,
		4,
		4,
		2,
		4,
		4,
		4,
		2,
		4,
		4,
		4,
		2,
		4,
		4,
		4,
		2,
		4,
		4,
		4,
		4,
		4,
		4,
		6,
		2,
		4,
		4,
		4,
		2,
		4,
		4,
		4,
		4,
		4,
		4,
		2,
		4,
		4,
		4,
		3,
		6,
		2,
		2,
		4,
		4,
		4,
		2,
		4,
		4,
		5,
	};
	int saved = SaveDC(dc);
	if (!saved) {
		return false;
	}
	bool ok = SetMapMode(dc, MM_ANISOTROPIC) && SetWindowExtEx(dc, 32000, 32000, NULL) &&
						SetViewportExtEx(dc, size, size, NULL) && SetPolyFillMode(dc, WINDING) &&
						SelectObject(dc, GetStockObject(DC_BRUSH)) &&
						SetDCBrushColor(dc, color) != CLR_INVALID && BeginPath(dc) &&
						PolyDraw(dc, points, types, (int)(sizeof points / sizeof points[0])) && EndPath(dc) &&
						FillPath(dc);
	return RestoreDC(dc, saved) && ok;
}

static HBITMAP createEditIcon(HDC dc, int size, COLORREF color, COLORREF background) {
	// Supersample only on cache misses; normal repaints copy the reduced bitmap.
	int largeSize = size * 4;
	HDC source = CreateCompatibleDC(dc);
	HDC target = CreateCompatibleDC(dc);
	HBITMAP large = CreateCompatibleBitmap(dc, largeSize, largeSize);
	HBITMAP bitmap = CreateCompatibleBitmap(dc, size, size);
	HGDIOBJ oldSource = source && large ? SelectObject(source, large) : NULL;
	HGDIOBJ oldTarget = target && bitmap ? SelectObject(target, bitmap) : NULL;
	RECT bounds = {0, 0, largeSize, largeSize};
	bool ok = oldSource && oldTarget && SetDCBrushColor(source, background) != CLR_INVALID &&
						FillRect(source, &bounds, GetStockObject(DC_BRUSH)) &&
						drawEditPath(source, largeSize, color) && SetStretchBltMode(target, HALFTONE) &&
						SetBrushOrgEx(target, 0, 0, NULL) &&
						StretchBlt(target, 0, 0, size, size, source, 0, 0, largeSize, largeSize, SRCCOPY);
	if (oldSource) {
		SelectObject(source, oldSource);
	}
	if (oldTarget) {
		SelectObject(target, oldTarget);
	}
	if (source) {
		DeleteDC(source);
	}
	if (target) {
		DeleteDC(target);
	}
	if (large) {
		DeleteObject(large);
	}
	if (!ok && bitmap) {
		DeleteObject(bitmap);
	}
	return ok ? bitmap : NULL;
}

static bool drawEditIcon(WindowState *state, const DRAWITEMSTRUCT *draw) {
	int inset = scaled(state, 3);
	int size = draw->rcItem.bottom - draw->rcItem.top - 2 * inset;
	if (size <= 0 || size > INT_MAX / 4) {
		return false;
	}
	COLORREF color = (draw->itemState & ODS_DISABLED) ? GetSysColor(COLOR_GRAYTEXT) : RGB_BLUE;
	COLORREF background = GetSysColor(COLOR_WINDOW);
	HDC dc = draw->hDC;
	if (!state->editIcon || state->editIconSize != size || state->editIconColor != color ||
			state->editIconBackground != background) {
		HBITMAP bitmap = createEditIcon(dc, size, color, background);
		if (!bitmap) {
			return false;
		}
		if (state->editIcon) {
			DeleteObject(state->editIcon);
		}
		state->editIcon = bitmap;
		state->editIconSize = size;
		state->editIconColor = color;
		state->editIconBackground = background;
	}
	HDC memory = CreateCompatibleDC(dc);
	if (!memory) {
		return false;
	}
	HGDIOBJ old = SelectObject(memory, state->editIcon);
	int pressed = (draw->itemState & ODS_SELECTED) ? 1 : 0;
	bool ok = old && FillRect(dc, &draw->rcItem, GetSysColorBrush(COLOR_WINDOW)) &&
						BitBlt(
							dc,
							draw->rcItem.left + inset + pressed,
							draw->rcItem.top + inset + pressed,
							size,
							size,
							memory,
							0,
							0,
							SRCCOPY
						);
	if (old) {
		SelectObject(memory, old);
	}
	DeleteDC(memory);
	if (ok && (draw->itemState & ODS_FOCUS)) {
		RECT focus = draw->rcItem;
		InflateRect(&focus, -1, -1);
		ok = DrawFocusRect(dc, &focus);
	}
	return ok;
}

static LRESULT
actionMessage(HWND window, WindowState *state, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
		case WM_COMMAND:
			if (HIWORD(wParam) != BN_CLICKED) {
				break;
			}
			if ((HWND)lParam == state->away[AWAY_ICON]) {
				editAway(window, state, true);
				return 0;
			}
			if ((HWND)lParam == state->away[AWAY_CONFIRM]) {
				startReplacement(window, state);
				return 0;
			}
			if ((HWND)lParam == state->away[AWAY_CANCEL]) {
				editAway(window, state, false);
				return 0;
			}
			if ((HWND)lParam == state->simulation[SIM_SUBMIT]) {
				startSimulation(window, state);
				return 0;
			}
			break;
		case WM_TIMER:
			if (wParam == REPLACE_TIMER) {
				checkReplacement(window, state);
				return 0;
			}
			if (wParam == SIMULATION_TIMER) {
				checkSimulation(window, state);
				return 0;
			}
			break;
		case WM_NOTIFY: {
			const NMHDR *notification = (const NMHDR *)lParam;
			if (notification->hwndFrom == state->tabs && notification->code == (UINT)TCN_SELCHANGE) {
				selectTab(state);
				return 0;
			}
			return metadataNotification(window, state, notification);
		}
		case WM_CLOSE:
			if (state->replacementThread) {
				showError(window, L"An away team replacement is running. Please wait before closing.");
				return 0;
			}
			if (state->simulationThread) {
				showError(window, L"A simulation is running. Please wait for it to finish before closing.");
				return 0;
			}
			break;
		default: break;
	}
	return DefWindowProcW(window, message, wParam, lParam);
}

// Draws the owner-drawn edit icon; returns false when the item is not ours.
static bool drawItem(HWND window, WindowState *state, const DRAWITEMSTRUCT *draw) {
	if (draw->hwndItem != state->away[AWAY_ICON]) {
		return false;
	}
	if (!drawEditIcon(state, draw)) {
		fprintf(stderr, "fixture viewer: cannot render edit icon\n");
		showError(window, L"Cannot render edit icon.");
		state->failed = true;
		DestroyWindow(window);
	}
	return true;
}

static void dpiChanged(HWND window, WindowState *state, UINT dpi, const RECT *bounds) {
	state->dpi = dpi;
	if (!setFonts(state) || !setMetadata(state)) {
		showError(window, L"Cannot update window fonts or summary.");
		state->failed = true;
		DestroyWindow(window);
		return;
	}
	SetWindowPos(
		window,
		NULL,
		bounds->left,
		bounds->top,
		bounds->right - bounds->left,
		bounds->bottom - bounds->top,
		SWP_NOZORDER | SWP_NOACTIVATE
	);
}

static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
	WindowState *state = (WindowState *)GetWindowLongPtrW(window, GWLP_USERDATA);
	if (message == WM_NCCREATE) {
		const CREATESTRUCTW *create = (const CREATESTRUCTW *)lParam;
		state = create->lpCreateParams;
		SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)state);
	}
	if (!state) {
		return DefWindowProcW(window, message, wParam, lParam);
	}
	if (state->taskbarCreated && message == state->taskbarCreated) {
		createTaskbar(window, state);
		return 0;
	}
	switch (message) {
		case WM_CREATE:
			state->dpi = GetDpiForWindow(window);
			if (!createControls(window, state)) {
				state->failed = true;
				return -1;
			}
			return 0;
		case WM_SIZE:
			if (wParam != SIZE_MINIMIZED) {
				layout(window, state);
			}
			return 0;
		case WM_FIXTURE_LAYOUT:
			state->layoutPending = false;
			layout(window, state);
			return 0;
		case HARNESS_PROGRESS_MESSAGE:
			updateSimulationProgress(window, state, wParam, lParam);
			return 0;
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORBTN: {
			HDC dc = (HDC)wParam;
			SetBkMode(dc, TRANSPARENT);
			SetTextColor(
				dc, (HWND)lParam == state->away[AWAY_ERROR] ? RGB(255, 0, 0) : GetSysColor(COLOR_WINDOWTEXT)
			);
			SetBkColor(dc, GetSysColor(COLOR_WINDOW));
			return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
		}
		case WM_DRAWITEM:
			if (drawItem(window, state, (const DRAWITEMSTRUCT *)lParam)) {
				return TRUE;
			}
			break;
		case WM_SYSCOLORCHANGE:
			if (state->away[AWAY_ICON]) {
				InvalidateRect(state->away[AWAY_ICON], NULL, TRUE);
			}
			break;
		case WM_SETCURSOR:
			if ((HWND)wParam == state->away[AWAY_ICON] && IsWindowEnabled(state->away[AWAY_ICON])) {
				SetCursor(LoadCursorW(NULL, IDC_HAND));
				return TRUE;
			}
			break;
		case WM_GETMINMAXINFO: {
			MINMAXINFO *limits = (MINMAXINFO *)lParam;
			limits->ptMinTrackSize.x = scaled(state, 900);
			limits->ptMinTrackSize.y = scaled(state, 600);
			return 0;
		}
		case WM_DPICHANGED: dpiChanged(window, state, HIWORD(wParam), (const RECT *)lParam); return 0;
		case WM_COMMAND:
		case WM_TIMER:
		case WM_NOTIFY:
		case WM_CLOSE: return actionMessage(window, state, message, wParam, lParam);
		case WM_DESTROY:
			if (state->editIcon) {
				DeleteObject(state->editIcon);
				state->editIcon = NULL;
			}
			PostQuitMessage(state->failed ? 1 : 0);
			return 0;
		default: break;
	}
	return DefWindowProcW(window, message, wParam, lParam);
}
// NOLINTEND(performance-no-int-to-ptr)

static bool handleKey(HWND window, WindowState *state, const MSG *message) {
	if (message->message != WM_KEYDOWN) {
		return false;
	}
	if (message->wParam == VK_ESCAPE && state->editingAway && TabCtrl_GetCurSel(state->tabs) == 0) {
		editAway(window, state, false);
		return true;
	}
	if (message->wParam == VK_RETURN) {
		if (message->hwnd == state->away[AWAY_ICON] || message->hwnd == state->away[AWAY_CANCEL]) {
			SendMessageW(message->hwnd, BM_CLICK, 0, 0);
			return true;
		}
		if (message->hwnd == state->away[AWAY_TEAM] || message->hwnd == state->away[AWAY_CONFIRM]) {
			startReplacement(window, state);
			return true;
		}
		if (message->hwnd == state->simulation[SIM_SUBMIT]) {
			SendMessageW(state->simulation[SIM_SUBMIT], BM_CLICK, 0, 0);
			return true;
		}
		if (message->hwnd == state->metadata && state->temporary && !state->missing) {
			exportFixture(window, state);
			return true;
		}
	}
	if (message->wParam == VK_TAB) {
		HWND focus = GetFocus();
		HWND next =
			GetNextDlgTabItem(window, focus == window ? NULL : focus, GetKeyState(VK_SHIFT) < 0);
		if (next) {
			SetFocus(next);
			return true;
		}
	}
	return false;
}

static int runMessageLoop(HWND window, WindowState *state) {
	ShowWindow(window, SW_SHOW);
	MSG message;
	BOOL status;
	while ((status = GetMessageW(&message, NULL, 0, 0)) > 0) {
		if (handleKey(window, state, &message)) {
			continue;
		}
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
	if (status == -1) {
		showError(window, L"Windows message loop failed.");
		DestroyWindow(window);
		return 1;
	}
	return (int)message.wParam;
}

static void joinThread(HANDLE thread) {
	if (thread) {
		WaitForSingleObject(thread, INFINITE);
		CloseHandle(thread);
	}
}

int fixtureWindow_open(const wchar_t *path, bool temporary) {
	FixtureData *data = calloc(1, sizeof *data);
	char error[512] = "";
	if (!data || !fixtureLoad_open(path, data, error, sizeof error)) {
		const char *reason = data ? error : "out of memory";
		wchar_t message[1024];
		if (!widen(reason, message, 1024)) {
			(void)swprintf(message, 1024, L"Cannot open fixture; invalid UTF-8 in error message.");
		}
		fprintf(stderr, "fixture viewer: %s\n", reason);
		showError(NULL, message);
		free(data);
		return 1;
	}
	(void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	HMODULE richEdit = LoadLibraryExW(L"Msftedit.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
	INITCOMMONCONTROLSEX controls = {.dwSize = sizeof controls,
		.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_UPDOWN_CLASS | ICC_PROGRESS_CLASS};
	HINSTANCE instance = GetModuleHandleW(NULL);
	WNDCLASSW definition = {.lpfnWndProc = windowProcedure,
		.hInstance = instance,
		.hCursor = LoadCursorW(NULL, IDC_ARROW),
		.hbrBackground = GetSysColorBrush(COLOR_WINDOW),
		.lpszClassName = windowClass};
	bool ok = richEdit && InitCommonControlsEx(&controls) && RegisterClassW(&definition);
	definition.lpfnWndProc = pitchProcedure;
	definition.lpszClassName = pitchClass;
	definition.cbWndExtra = sizeof(HFONT);
	ok = ok && RegisterClassW(&definition);
	const wchar_t *filename = path;
	for (const wchar_t *cursor = path; *cursor; ++cursor) {
		if (*cursor == L'\\' || *cursor == L'/') {
			filename = cursor + 1;
		}
	}
	WindowState state = {.data = data,
		.path = path,
		.filename = filename,
		.dpi = GetDpiForSystem(),
		.temporary = temporary};
	HRESULT comResult = E_FAIL;
	HWND window = NULL;
	if (ok) {
		comResult = initializeTaskbar(&state);
		RECT workArea;
		int width = scaled(&state, 1480);
		int height = scaled(&state, 980);
		if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
			int availableWidth = workArea.right - workArea.left;
			int availableHeight = workArea.bottom - workArea.top;
			width = width < availableWidth ? width : availableWidth;
			height = height < availableHeight ? height : availableHeight;
		}
		window = CreateWindowExW(
			WS_EX_CONTROLPARENT,
			windowClass,
			L"FM24 Match Harness (protocol " HARNESS_PROTOCOL L")",
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			width,
			height,
			NULL,
			NULL,
			instance,
			&state
		);
	}
	int result = 1;
	if (!window) {
		wchar_t message[160];
		(void)swprintf(
			message,
			160,
			L"Cannot create fixture viewer (Windows error %lu). Check input text is valid UTF-8.",
			GetLastError()
		);
		showError(NULL, message);
	} else {
		result = runMessageLoop(window, &state);
	}
	joinThread(state.simulationThread);
	joinThread(state.replacementThread);
	releaseTaskbar(&state);
	if (SUCCEEDED(comResult)) {
		CoUninitialize();
	}
	free(state.replacementJob.data);
	removeReplacement(state.replacementJob.output);
	removeReplacement(state.replacementPath);
	if (state.font) {
		DeleteObject(state.font);
		DeleteObject(state.headingFont);
	}
	UnregisterClassW(pitchClass, instance);
	UnregisterClassW(windowClass, instance);
	if (richEdit) {
		FreeLibrary(richEdit);
	}
	free(data);
	return result;
}
