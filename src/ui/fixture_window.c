// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixture_window.h"

#include "client.h"
#include "commands.h"
#include "constants.h"
#include "files.h"
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
#include <shlobj.h>
#include <shobjidl.h>


enum {
	WM_FIXTURE_LAYOUT = WM_APP + 1,
	SIMULATION_TIMER = 1,
	REPLACE_TIMER = 2,
	FILE_TIMER = 3,
	TEAMS_TIMER = 4,
	PLAYERS_TIMER = 5,
	WM_SWAP_PLAYERS = WM_APP + 2, // the player list has loaded or failed (to the swap prompt)
};

enum { TEAMS_SHOWN = 200, TEAM_LABEL_LENGTH = 256 }; // the team pickers' lists

// The swap prompt's player list: what is typed is matched once it has this many characters and
// has not changed for the delay.
enum {
	PLAYERS_SHOWN = 200,
	PLAYER_LABEL_LENGTH = 1024,
	PLAYER_FILTER_MINIMUM = 3,
	PLAYER_FILTER_DELAY_MS = 100,
	SWAP_FILTER_TIMER = 1,
};

enum { FILE_OPEN_BUTTON, FILE_SAVE_BUTTON, FILE_CONTROL_COUNT };

enum { CELL_COUNT = 9 }; // the squad lists' text cells

// The swap prompt's controls.
enum {
	SWAP_PROMPT_NEW_LABEL = 100,
	SWAP_PROMPT_NEW_PLAYER,
	SWAP_PROMPT_DUPLICATES,
	SWAP_ID_LENGTH = 16,
};

enum { TEAM_ICON, TEAM_INPUT, TEAM_CONFIRM, TEAM_CANCEL, TEAM_ERROR, TEAM_CONTROL_COUNT };

enum { PLAYER_HAPPINESS, PLAYER_CONDITION, PLAYER_BUTTON_COUNT };

enum {
	SIM_THREADS_LABEL,
	SIM_THREADS,
	SIM_SPINNER,
	SIM_SEEDS_LABEL,
	SIM_SEEDS,
	SIM_DETAILED,
	SIM_KEEP_BUILD,
	SIM_SUBMIT,
	SIM_STATUS,
	SIM_SUMMARY,
	SIM_CONTROL_COUNT,
};

typedef struct SimulationJob {
	wchar_t input[HARNESS_PATH_LENGTH];
	FixtureNumber venueValues[VENUE_FIELD_COUNT];
	FixturePlayerEdits playerEdits;
	char error[CLIENT_RESULT_LENGTH];
	wchar_t seeds[256];
	wchar_t threads[16];
	uintptr_t progressWindow;
	bool detailed;
	bool keepBuild; // play the fixture as it is, without rebuilding it per seed (--keep-build)
} SimulationJob;

// Rebuilds a side with another team, or (`swap`) puts another player in one of its player blocks.
typedef struct ReplaceTeamJob {
	wchar_t input[HARNESS_PATH_LENGTH];
	wchar_t output[HARNESS_PATH_LENGTH];
	wchar_t team[256];
	wchar_t block[32];              // swap: "home.player[N]"
	wchar_t person[SWAP_ID_LENGTH]; // swap: the injected player's unique ID
	char error[CLIENT_RESULT_LENGTH];
	FixtureData *data;
	FixtureNumber venueValues[VENUE_FIELD_COUNT];
	FixturePlayerEdits playerEdits;
	unsigned side;
	bool swap;
	bool keepUid; // swap: the player may already be in the fixture (inject-player --keep-uid)
} ReplaceTeamJob;

struct WindowState;

// The swap prompt's swapped-out player (the selected row), and the unique ID it returns.
typedef struct SwapPrompt {
	struct WindowState *state; // its player list, which may still be loading
	const char *name;          // the swapped-out player's
	int block;                 // the swapped-out player's block
	wchar_t id[SWAP_ID_LENGTH];
	bool filtering; // the new player's list is being refilled (ignore its own edit notifications)
	bool allowDuplicates; // "Allow duplicates?": the new player may already be in the fixture
} SwapPrompt;

// An in-memory dialog template (DLGTEMPLATE and its items), for a modal prompt without resources.
typedef struct DialogTemplate {
	_Alignas(DWORD) BYTE bytes[512];
	size_t length;
	bool overflow;
} DialogTemplate;

// A team the team pickers offer (fm24ctl list-teams), or a player the swap prompt offers (fm24ctl
// list-players).
typedef struct Choice {
	wchar_t *label;  // "Short name (ID)" or "Name - Club (ID)", as listed
	wchar_t *folded; // what is matched (foldText): the label, or the player's names
} Choice;

// Fetches the team or player list on a worker thread.
typedef struct ChoiceListJob {
	char error[CLIENT_RESULT_LENGTH];
	Choice *choices; // sorted by label
	size_t count;
} ChoiceListJob;

typedef struct FileJob {
	wchar_t input[HARNESS_PATH_LENGTH];
	wchar_t output[HARNESS_PATH_LENGTH];
	char error[CLIENT_RESULT_LENGTH];
	FixtureData *data;
	FixtureNumber venueValues[VENUE_FIELD_COUNT];
	FixturePlayerEdits playerEdits;
	bool save;
	bool temporary;
} FileJob;

typedef struct WindowState {
	FixtureData *data;
	const wchar_t *path;
	const wchar_t *filename;
	wchar_t savedFilename[HARNESS_PATH_LENGTH];
	HWND metadata;
	HWND fileButtons[FILE_CONTROL_COUNT];
	HWND tabs;
	HWND progress;
	HWND headings[2];
	wchar_t headingText[2][2 * FIXTURE_TEXT + 128];
	HWND pitches[2];
	HWND lists[2];
	HWND playerButtons[2][PLAYER_BUTTON_COUNT];
	bool selectedPlayers[2][FIXTURE_PLAYERS];
	FixturePlayerEdits playerEdits;
	HWND simulation[SIM_CONTROL_COUNT];
	HWND venueLabels[VENUE_FIELD_COUNT];
	HWND venueInputs[VENUE_FIELD_COUNT];
	HWND teamControls[2][TEAM_CONTROL_COUNT];
	HWND swapButtons[2]; // "Swap player", at the right of each team's heading
	HANDLE simulationThread;
	HANDLE replacementThread;
	HANDLE fileThread;
	HANDLE teamsThread;
	ITaskbarList3 *taskbar;
	UINT taskbarCreated;
	UINT progressCompleted;
	UINT progressTotal;
	SimulationJob simulationJob;
	ReplaceTeamJob replacementJob;
	FileJob fileJob;
	ChoiceListJob teamsJob;
	Choice *teams; // loaded once per viewer; NULL until then (free text still works)
	size_t teamCount;
	HANDLE playersThread;
	ChoiceListJob playersJob;
	Choice *players; // loaded once per viewer; NULL until then (unique IDs still work)
	size_t playerCount;
	HWND swapPrompt; // the open swap prompt, told when the player list is ready
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
	bool editingTeam[2];
	bool filteringTeams; // a picker's list is being refilled (ignore its own edit notifications)
	bool fillingVenue;
	bool fillingPlayers;
	bool venueDirty;
} WindowState;

static const wchar_t windowClass[] = L"FM24MatchHarness";
// fixtureWindow_open reuses the application's existing viewer on its UI thread.
static HWND activeWindow;
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
static const wchar_t *const venueLabels[VENUE_FIELD_COUNT] = {
	L"Pitch width (m):",
	L"Pitch length (m):",
	L"City longitude:",
	L"City latitude:",
	L"Capacity:",
	L"Seat Capacity:",
	L"Home fans:",
	L"Away fans:",
	L"Season ticket holders:",
	L"Surface:",
	L"Pitch condition (0-255):",
};

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
static void selectTab(WindowState *state);
static void showFileError(HWND window, const char *error);
static void removeReplacement(const wchar_t *path);
static const char *launchReplacement(HWND window, WindowState *state);
static void enableReplacementActions(const WindowState *state, BOOL enabled);
static bool
startFileJob(HWND window, WindowState *state, const wchar_t *path, bool save, bool temporary);

// The block of the side's one selected player, or -1 if none or several are selected.
static int selectedBlock(const WindowState *state, unsigned side) {
	int block = -1;
	for (unsigned i = 0; i < FIXTURE_PLAYERS; ++i) {
		if (state->selectedPlayers[side][i]) {
			if (block >= 0) {
				return -1;
			}
			block = (int)i;
		}
	}
	return block;
}

// The squad lists and the buttons acting on their selection: the player buttons need a player
// selected, "Swap player" exactly one.
static void enablePlayers(const WindowState *state) {
	bool idle = !state->simulationThread && !state->replacementThread && !state->fileThread;
	for (unsigned side = 0; side < 2; ++side) {
		bool selected = false;
		for (unsigned i = 0; i < FIXTURE_PLAYERS; ++i) {
			selected = selected || state->selectedPlayers[side][i];
		}
		EnableWindow(state->lists[side], idle);
		for (unsigned i = 0; i < PLAYER_BUTTON_COUNT; ++i) {
			EnableWindow(state->playerButtons[side][i], idle && selected);
		}
		EnableWindow(state->swapButtons[side], idle && selectedBlock(state, side) >= 0);
	}
}

static bool hasVenue(const FixtureNumber values[VENUE_FIELD_COUNT]) {
	for (unsigned i = 0; i < VENUE_FIELD_COUNT; ++i) {
		if (values[i].known) {
			return true;
		}
	}
	return false;
}

static void enableVenue(const WindowState *state, BOOL enabled) {
	bool pitchKnown =
		state->data->venueValues[VENUE_WIDTH].known && state->data->venueValues[VENUE_LENGTH].known;
	for (unsigned i = 0; i < VENUE_FIELD_COUNT; ++i) {
		EnableWindow(
			state->venueInputs[i],
			enabled && state->data->venueValues[i].known && (i > VENUE_LENGTH || pitchKnown)
		);
	}
}

static bool setVenue(WindowState *state) {
	state->fillingVenue = true;
	bool ok = true;
	for (unsigned i = 0; ok && i < VENUE_FIELD_COUNT; ++i) {
		FixtureNumber number = state->data->venueValues[i];
		HWND control = state->venueInputs[i];
		if (i == VENUE_SURFACE) {
			int selected = !number.known ? -1 : number.value == 1 ? 0 : number.value == 8 ? 1 : -1;
			SendMessageW(control, CB_SETCURSEL, (WPARAM)selected, 0);
		} else {
			wchar_t text[64] = L"";
			if (number.known) {
				if (i <= VENUE_LATITUDE) {
					(void)swprintf(text, 64, L"%.9g", (double)fixtureData_toFloat((uint32_t)number.value));
				} else {
					(void)swprintf(text, 64, L"%llu", (unsigned long long)number.value);
				}
			}
			ok = SetWindowTextW(control, text) != 0;
		}
	}
	state->fillingVenue = false;
	state->venueDirty = false;
	enableVenue(state, TRUE);
	return ok;
}

// Reads venue field i from its control into value; false if the control's entry isn't valid.
static bool readVenueInput(HWND control, unsigned i, uint64_t *value) {
	if (i == VENUE_SURFACE) {
		LRESULT selected = SendMessageW(control, CB_GETCURSEL, 0, 0);
		*value = selected == 0 ? 1 : 8;
		return selected == 0 || selected == 1;
	}
	wchar_t text[64];
	int length = GetWindowTextW(control, text, 64);
	wchar_t *end = NULL;
	errno = 0;
	if (i <= VENUE_LATITUDE) {
		*value = fixtureData_fromFloat(wcstof(text, &end));
	} else {
		*value = wcstoull(text, &end, 10);
	}
	return length > 0 && GetWindowTextLengthW(control) < 64 && !errno && !*end &&
				 wcsspn(text, i <= VENUE_LATITUDE ? L"+-0123456789.eE" : L"0123456789") == (size_t)length;
}

static bool readVenue(HWND window, WindowState *state, FixtureNumber values[VENUE_FIELD_COUNT]) {
	memcpy(values, state->data->venueValues, sizeof state->data->venueValues);
	for (unsigned i = 0; i < VENUE_FIELD_COUNT; ++i) {
		HWND control = state->venueInputs[i];
		if (values[i].known && !readVenueInput(control, i, &values[i].value)) {
			wchar_t message[192];
			(void)swprintf(
				message,
				192,
				L"%ls Enter a valid %ls.",
				venueLabels[i],
				i == VENUE_SURFACE    ? L"surface selection"
				: i <= VENUE_LATITUDE ? L"number"
															: L"whole number"
			);
			TabCtrl_SetCurSel(state->tabs, 1);
			selectTab(state);
			showError(window, message);
			SetFocus(control);
			return false;
		}
	}
	char error[256];
	if (!fixtureData_validateVenue(values, error, sizeof error)) {
		TabCtrl_SetCurSel(state->tabs, 1);
		selectTab(state);
		showFileError(window, error);
		return false;
	}
	return true;
}

static bool temporaryJson(wchar_t *out, char *error, size_t errorSize) {
	wchar_t directory[HARNESS_PATH_LENGTH];
	wchar_t temporary[HARNESS_PATH_LENGTH];
	out[0] = 0;
	DWORD length = GetTempPathW(HARNESS_PATH_LENGTH, directory);
	if (!length || length >= HARNESS_PATH_LENGTH ||
			!GetTempFileNameW(directory, L"fmv", 0, temporary)) {
		(void)snprintf(
			error, errorSize, "Cannot create a temporary fixture (Windows error %lu).", GetLastError()
		);
		return false;
	}
	int count = swprintf(out, HARNESS_PATH_LENGTH, L"%ls.json", temporary);
	if (count < 0 || count >= HARNESS_PATH_LENGTH || !MoveFileW(temporary, out)) {
		(void)snprintf(
			error, errorSize, "Cannot create temporary JSON (Windows error %lu).", GetLastError()
		);
		removeReplacement(temporary);
		out[0] = 0;
		return false;
	}
	return true;
}

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
	wchar_t temporary[HARNESS_PATH_LENGTH] = L"";
	wchar_t output[HARNESS_PATH_LENGTH];
	if (hasVenue(job->venueValues) || fixtureData_hasPlayerEdits(&job->playerEdits)) {
		if (!temporaryJson(temporary, job->error, sizeof job->error)) {
			return 1;
		}
		if (!fixtureLoad_saveEdits(
					job->input,
					temporary,
					hasVenue(job->venueValues) ? job->venueValues : NULL,
					&job->playerEdits,
					job->error,
					sizeof job->error
				)) {
			removeReplacement(temporary);
			return 1;
		}
		wcscpy(output, job->input);
		wchar_t *extension = wcsrchr(output, L'.');
		wcscpy(extension, L".csv"); // input is a .json or .bin fixture
	}
	wchar_t *arguments[] = {L"fm24ctl.exe",
		L"run",
		temporary[0] ? temporary : job->input,
		job->seeds,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL};
	int count = 4;
	if (temporary[0]) {
		arguments[count++] = output;
	}
	arguments[count++] = L"--threads";
	arguments[count++] = job->threads;
	if (job->detailed) {
		arguments[count++] = L"--detailed";
	}
	if (job->keepBuild) {
		arguments[count++] = L"--keep-build";
	}
	DWORD result = (DWORD)commands_runWithProgress(count, arguments, job->progressWindow);
	removeReplacement(temporary);
	return result;
}

static void startSimulation(HWND window, WindowState *state) {
	if (state->simulationThread || state->replacementThread || state->fileThread) {
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
	if (GetWindowTextLengthW(state->simulation[SIM_SEEDS]) >= 256 ||
			(wcscmp(job->seeds, L"keep") != 0 && wcsspn(job->seeds, L"0123456789,-") != (size_t)length)) {
		showError(
			window,
			L"Enter a seed pattern such as 1, 42, 1-1000, 1,3,5 or 1,4,7-70; "
			L"or leave blank to use the fixture's seed."
		);
		SetFocus(state->simulation[SIM_SEEDS]);
		return;
	}
	if (length == 0) {
		wcscpy(job->seeds, L"keep");
	}
	if (!readVenue(window, state, job->venueValues)) {
		return;
	}
	job->error[0] = 0;
	const wchar_t *input = state->savedFilename[0] ? state->savedFilename : state->path;
	if (!paths_absolute(input, job->input, HARNESS_PATH_LENGTH)) {
		showError(window, L"Cannot resolve the fixture path.");
		return;
	}
	job->detailed = SendMessageW(state->simulation[SIM_DETAILED], BM_GETCHECK, 0, 0) == BST_CHECKED;
	job->playerEdits = state->playerEdits;
	job->keepBuild =
		SendMessageW(state->simulation[SIM_KEEP_BUILD], BM_GETCHECK, 0, 0) == BST_CHECKED;
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
	for (unsigned side = 0; side < 2; ++side) {
		EnableWindow(state->teamControls[side][TEAM_ICON], FALSE);
		EnableWindow(state->teamControls[side][TEAM_CONFIRM], FALSE);
	}
	for (unsigned i = 0; i < FILE_CONTROL_COUNT; ++i) {
		EnableWindow(state->fileButtons[i], FALSE);
	}
	enableVenue(state, FALSE);
	enablePlayers(state);
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
	for (unsigned side = 0; side < 2; ++side) {
		EnableWindow(state->teamControls[side][TEAM_ICON], TRUE);
		EnableWindow(state->teamControls[side][TEAM_CONFIRM], TRUE);
	}
	for (unsigned i = 0; i < FILE_CONTROL_COUNT; ++i) {
		EnableWindow(state->fileButtons[i], TRUE);
	}
	enableVenue(state, TRUE);
	enablePlayers(state);
	const wchar_t *message =
		result == 0
			? L"Simulation complete. Results were saved beside the fixture; see the console for paths."
			: L"Simulation failed. Check the console and harness.log for details.";
	SetWindowTextW(state->simulation[SIM_STATUS], message);
	if (result != 0) {
		if (state->simulationJob.error[0]) {
			showFileError(window, state->simulationJob.error);
		} else {
			showError(window, message);
		}
	}
}

// A side's heading, pitch, squad list and the heading's buttons (not its team editor).
static void showSquad(const WindowState *state, unsigned side, bool show) {
	int command = show ? SW_SHOW : SW_HIDE;
	ShowWindow(state->headings[side], command);
	ShowWindow(state->pitches[side], command);
	ShowWindow(state->lists[side], command);
	for (unsigned button = 0; button < PLAYER_BUTTON_COUNT; ++button) {
		ShowWindow(state->playerButtons[side][button], command);
	}
	ShowWindow(state->swapButtons[side], command);
}

static void selectTab(WindowState *state) {
	int tab = TabCtrl_GetCurSel(state->tabs);
	bool simulation = tab == 2;
	for (unsigned i = 0; i < 2; ++i) {
		showSquad(state, i, tab == 0);
	}
	for (unsigned i = 0; i < VENUE_FIELD_COUNT; ++i) {
		ShowWindow(state->venueLabels[i], tab == 1 ? SW_SHOW : SW_HIDE);
		ShowWindow(state->venueInputs[i], tab == 1 ? SW_SHOW : SW_HIDE);
	}
	for (unsigned i = 0; i < SIM_CONTROL_COUNT; ++i) {
		ShowWindow(state->simulation[i], simulation ? SW_SHOW : SW_HIDE);
	}
	for (unsigned side = 0; side < 2; ++side) {
		for (unsigned i = 0; i < TEAM_CONTROL_COUNT; ++i) {
			bool visible = tab == 0 &&
										 (i == TEAM_ICON ? !state->editingTeam[side] : state->editingTeam[side]) &&
										 (i != TEAM_ERROR || GetWindowTextLengthW(state->teamControls[side][i]) > 0);
			ShowWindow(state->teamControls[side][i], visible ? SW_SHOW : SW_HIDE);
		}
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

// Formats the summary's first line: the unsaved or missing warning and its export link, or the
// filename (which starts at filenameStart) in bold.
static bool formatMetadataHeading(const WindowState *state, LONG filenameStart, COLORREF normal) {
	if (!state->temporary) {
		return formatMetadata(
			state->metadata,
			filenameStart,
			filenameStart + (LONG)wcslen(state->filename),
			CFE_BOLD,
			normal
		);
	}
	LONG warningLength = (LONG)wcslen(state->missing ? missingFixture : unsavedPrefix);
	if (!formatMetadata(state->metadata, 0, warningLength, CFE_BOLD, RGB(255, 0, 0))) {
		return false;
	}
	return state->missing || formatMetadata(
														 state->metadata,
														 warningLength,
														 warningLength + (LONG)wcslen(exportLink),
														 CFE_LINK | CFE_UNDERLINE,
														 RGB_BLUE
													 );
}

static bool setMetadata(WindowState *state) {
	static const wchar_t filenamePrefix[] = L"Viewing fixture: ";
	static const char note[] =
		"NOTE: Condition/sharpness are percentages. Fatigue is signed jadedness (0 = fresh), not a "
		"percentage.";
	static const char controlNote[] =
		"NOTE 2: AI/unknown control: assignments unmapped. Squad listed below; AI may replace this "
		"tactic.";
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
		"Referee: %s\r\n\r\n%s\r\n%s",
		competition,
		data->venue[0] ? data->venue : "Unknown",
		data->when[0] ? data->when : "Unknown (no export date annotation)",
		data->officials[4][0] ? data->officials[4] : "Not stored/unassigned",
		note,
		controlNote
	);
	wchar_t wide[8192];
	int length;
	if (state->temporary) {
		length = state->missing
							 ? swprintf(wide, 8192, L"%ls\r\n", missingFixture)
							 : swprintf(wide, 8192, L"%ls%ls to export it\r\n", unsavedPrefix, exportLink);
	} else {
		length = swprintf(
			wide,
			8192,
			L"%ls%ls%ls\r\n",
			filenamePrefix,
			state->filename,
			fixtureData_hasPlayerEdits(&state->playerEdits)
				? (state->venueDirty ? L" (unsaved venue and player edits)" : L" (unsaved player edits)")
			: state->venueDirty ? L" (unsaved venue edits)"
													: L""
		);
	}
	if (length < 0 || !widen(text, wide + length, 8192 - (size_t)length) ||
			!SetWindowTextW(state->metadata, wide)) {
		return false;
	}
	COLORREF normal = GetSysColor(COLOR_WINDOWTEXT);
	if (!formatMetadata(state->metadata, 0, -1, 0, normal)) {
		return false;
	}
	bool ok = formatMetadataHeading(
		state, (LONG)(sizeof filenamePrefix / sizeof filenamePrefix[0] - 1), normal
	);
	GETTEXTLENGTHEX textLength = {.flags = GTL_NUMCHARS | GTL_PRECISE, .codepage = 1200};
	LONG controlNoteStart =
		(LONG)SendMessageW(state->metadata, EM_GETTEXTLENGTHEX, (WPARAM)&textLength, 0) -
		(LONG)(sizeof controlNote - 1);
	LONG noteStart = controlNoteStart - 1 - (LONG)(sizeof note - 1);
	if (ok && noteStart >= 0) {
		ok = formatMetadata(state->metadata, noteStart, noteStart + 5, CFE_BOLD, normal) &&
				 formatMetadata(state->metadata, controlNoteStart, controlNoteStart + 7, CFE_BOLD, normal);
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

// The export's new filename, from the fixture's teams and the current time.
static bool exportFilename(HWND window, const WindowState *state, wchar_t *filename) {
	time_t timestamp = time(NULL);
	if (timestamp == (time_t)-1) {
		fputs("fixture viewer: cannot read current time\n", stderr);
		showError(window, L"Cannot read current time.");
		return false;
	}
	if (!fixtureExport_filename(
				state->data, (uint64_t)timestamp, filename, FIXTURE_EXPORT_FILENAME_LENGTH
			)) {
		fputs("fixture viewer: export filename is too long\n", stderr);
		showError(window, L"Fixture export filename is too long.");
		return false;
	}
	return true;
}

// The file to export: the fixture itself or, with edits, a temporary copy (in `edited`,
// which the caller removes) that has them. NULL after showing an error.
static const wchar_t *exportInput(
	HWND window,
	const WindowState *state,
	const FixtureNumber values[VENUE_FIELD_COUNT],
	wchar_t edited[HARNESS_PATH_LENGTH]
) {
	if (!hasVenue(values) && !fixtureData_hasPlayerEdits(&state->playerEdits)) {
		return state->path;
	}
	char error[512];
	if (!temporaryJson(edited, error, sizeof error)) {
		showFileError(window, error);
		return NULL;
	}
	if (!fixtureLoad_saveEdits(
				state->path,
				edited,
				hasVenue(values) ? values : NULL,
				&state->playerEdits,
				error,
				sizeof error
			)) {
		removeReplacement(edited);
		showFileError(window, error);
		return NULL;
	}
	return edited;
}

// Copies the fixture, with any pending edits, to a new export file. False after showing an error.
static bool writeExport(HWND window, WindowState *state) {
	wchar_t filename[FIXTURE_EXPORT_FILENAME_LENGTH];
	FixtureNumber values[VENUE_FIELD_COUNT];
	if (!exportFilename(window, state, filename) || !readVenue(window, state, values)) {
		return false;
	}
	wchar_t edited[HARNESS_PATH_LENGTH] = L"";
	const wchar_t *input = exportInput(window, state, values, edited);
	if (!input) {
		return false;
	}
	BOOL copied = CopyFileW(input, filename, TRUE);
	DWORD copyError = GetLastError();
	removeReplacement(edited);
	if (copied) {
		wcscpy(state->savedFilename, filename);
		state->filename = state->savedFilename;
		state->temporary = false;
		memcpy(state->data->venueValues, values, sizeof values);
		state->venueDirty = false;
		memset(&state->playerEdits, 0, sizeof state->playerEdits);
	} else if (fileMissing(state->path)) {
		state->missing = true;
	} else {
		wchar_t message[256];
		(void)swprintf(
			message,
			256,
			L"Cannot export fixture (Windows error %lu). No file was overwritten.",
			copyError
		);
		fprintf(stderr, "fixture viewer: cannot export fixture (Windows error %lu)\n", copyError);
		showError(window, message);
		return false;
	}
	return true;
}

static void exportFixture(HWND window, WindowState *state) {
	if (state->fileThread) {
		showError(window, L"Please wait for the fixture file operation to finish before exporting.");
		return;
	}
	if (state->replacementThread) {
		showError(window, L"Please wait for the team replacement to finish before exporting.");
		return;
	}
	if (!state->temporary || state->missing) {
		return;
	}
	if (fileMissing(state->path)) {
		state->missing = true;
	} else if (!writeExport(window, state)) {
		return;
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
		if (state->editingTeam[side]) {
			(void)snprintf(text, sizeof text, "%s: ", side ? "Away" : "Home");
		}
		if (!widen(text, state->headingText[side], sizeof state->headingText[side] / sizeof(wchar_t))) {
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

static void playerCells(const FixturePlayer *player, char cells[CELL_COUNT][FIXTURE_TEXT]) {
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

static bool setListCell(HWND list, unsigned row, unsigned column, const char *text) {
	wchar_t wide[FIXTURE_TEXT];
	if (!widen(text, wide, FIXTURE_TEXT)) {
		return false;
	}
	LVITEMW item = {.iSubItem = (int)column, .pszText = wide};
	return SendMessageW(list, LVM_SETITEMTEXTW, row, (LPARAM)&item) != FALSE;
}

static bool insertListRow(
	HWND list,
	unsigned row,
	char cells[CELL_COUNT][FIXTURE_TEXT],
	int player,
	bool selected
) {
	LVITEMW selection = {
		.mask = LVIF_TEXT | LVIF_PARAM, .iItem = (int)row, .pszText = L"", .lParam = player + 1};
	if (SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&selection) == -1) {
		return false;
	}
	ListView_SetItemState(list, (int)row, selected ? LVIS_SELECTED : 0, LVIS_SELECTED);
	for (unsigned cell = 0; cell < CELL_COUNT; ++cell) {
		if (!setListCell(list, row, cell, cells[cell])) {
			return false;
		}
	}
	return true;
}

static bool fillList(WindowState *state, unsigned side) {
	HWND list = state->lists[side];
	const FixtureTeam *team = &state->data->teams[side];
	state->fillingPlayers = true;
	SendMessageW(list, WM_SETREDRAW, FALSE, 0);
	ListView_DeleteAllItems(list);
	unsigned count = (unsigned)team->playerCount.value + (team->lineupKnown ? 0 : FIXTURE_SLOTS);
	bool ok = true;
	for (unsigned i = 0; i < count && ok; ++i) {
		char cells[CELL_COUNT][FIXTURE_TEXT] = {"", "", "-", "-", "-", "-", "-", "-", "-"};
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
		int playerIndex =
			player && player->present && player->name[0] && strcmp(player->name, "<Unknown>") != 0
				? (int)(player - team->players)
				: -1;
		ok = insertListRow(
			list, i, cells, playerIndex, playerIndex >= 0 && state->selectedPlayers[side][playerIndex]
		);
	}
	state->fillingPlayers = false;
	SendMessageW(list, WM_SETREDRAW, TRUE, 0);
	InvalidateRect(list, NULL, TRUE);
	enablePlayers(state);
	return ok;
}

static void maxPlayers(HWND window, WindowState *state, unsigned side, unsigned button) {
	if (state->simulationThread || state->replacementThread || state->fileThread) {
		return;
	}
	FixtureTeam *team = &state->data->teams[side];
	bool selected = false;
	for (unsigned i = 0; i < FIXTURE_PLAYERS; ++i) {
		if (!state->selectedPlayers[side][i]) {
			continue;
		}
		const FixturePlayer *player = &team->players[i];
		if (!player->present || !player->name[0] || strcmp(player->name, "<Unknown>") == 0 ||
				(button == PLAYER_HAPPINESS ? !player->morale.known
																		: !player->condition.known || !player->sharpness[0].known ||
																				!player->sharpness[1].known || !player->fatigue.known)) {
			showError(window, L"The selected player is missing wellbeing data; no players were changed.");
			return;
		}
		selected = true;
	}
	if (!selected) {
		return;
	}
	bool ok = true;
	for (unsigned i = 0; i < FIXTURE_PLAYERS; ++i) {
		if (!state->selectedPlayers[side][i]) {
			continue;
		}
		FixturePlayer *player = &team->players[i];
		if (button == PLAYER_HAPPINESS) {
			player->morale.value = 20;
			state->playerEdits.players[side][i] |= FIXTURE_MAX_HAPPINESS;
		} else {
			player->condition.value = 10000;
			player->sharpness[0].value = 10000;
			player->sharpness[1].value = 10000;
			player->fatigue.value = 0;
			state->playerEdits.players[side][i] |= FIXTURE_MAX_CONDITION;
		}
		char cells[CELL_COUNT][FIXTURE_TEXT] = {"", "", "", "", "", "-", "-", "-", "-"};
		playerCells(player, cells);
		unsigned row = i + (team->lineupKnown ? 0 : FIXTURE_SLOTS);
		for (unsigned cell = 5; cell < CELL_COUNT; ++cell) {
			ok = setListCell(state->lists[side], row, cell, cells[cell]) && ok;
		}
	}
	if (!ok || !setMetadata(state)) {
		showError(window, L"Cannot display the player edits.");
		state->failed = true;
		DestroyWindow(window);
	}
}

static LRESULT
playerNotification(WindowState *state, const NMLISTVIEW *notification, unsigned side) {
	if (state->fillingPlayers || !(notification->uChanged & LVIF_STATE) ||
			!((notification->uOldState ^ notification->uNewState) & LVIS_SELECTED)) {
		return 0;
	}
	if (notification->hdr.code == (UINT)LVN_ITEMCHANGING) {
		return state->simulationThread || state->replacementThread || state->fileThread;
	}
	if (notification->hdr.code == (UINT)LVN_ITEMCHANGED) {
		if (notification->iItem == -1) {
			const FixtureTeam *team = &state->data->teams[side];
			for (unsigned i = 0; i < FIXTURE_PLAYERS; ++i) {
				const FixturePlayer *player = &team->players[i];
				unsigned row = i + (team->lineupKnown ? 0 : FIXTURE_SLOTS);
				state->selectedPlayers[side][i] =
					i < team->playerCount.value && player->present && player->name[0] &&
					strcmp(player->name, "<Unknown>") != 0 &&
					ListView_GetItemState(state->lists[side], row, LVIS_SELECTED) != 0;
			}
		} else if (notification->lParam > 0 && notification->lParam <= FIXTURE_PLAYERS) {
			state->selectedPlayers[side][notification->lParam - 1] =
				(notification->uNewState & LVIS_SELECTED) != 0;
		}
		enablePlayers(state);
	}
	return 0;
}

static void removeReplacement(const wchar_t *path) {
	if (path[0] && !DeleteFileW(path)) {
		fprintf(
			stderr, "cannot remove replacement fixture %ls (Windows error %lu)\n", path, GetLastError()
		);
	}
}

static void teamError(HWND window, WindowState *state, unsigned side, const char *error) {
	fprintf(stderr, "fixture viewer: %s\n", error);
	if (!setText(state->teamControls[side][TEAM_ERROR], error)) {
		SetWindowTextW(
			state->teamControls[side][TEAM_ERROR], L"Cannot display replacement error (invalid UTF-8)."
		);
	}
	ShowWindow(
		state->teamControls[side][TEAM_ERROR], TabCtrl_GetCurSel(state->tabs) == 0 ? SW_SHOW : SW_HIDE
	);
	layout(window, state);
}

static void freeChoices(Choice *choices, size_t count) {
	for (size_t i = 0; i < count; ++i) {
		free(choices[i].label);
		free(choices[i].folded);
	}
	free(choices);
}

static int compareChoices(const void *a, const void *b) {
	const Choice *left = a;
	const Choice *right = b;
	return CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE, left->label, -1, right->label, -1) -
				 CSTR_EQUAL;
}

// Lower case without accents, so that "odegaard" matches "Ødegaard": accents are split off and
// dropped, and the Latin letters that do not split are spelled out.
static bool foldText(const wchar_t *text, wchar_t *out, size_t size) {
	static const struct {
		wchar_t letter;
		const wchar_t *spelling;
	} letters[] = {
		{L'Æ', L"ae"},
		{L'æ', L"ae"},
		{L'Ð', L"d"},
		{L'ð', L"d"},
		{L'Ø', L"o"},
		{L'ø', L"o"},
		{L'Þ', L"th"},
		{L'þ', L"th"},
		{L'ß', L"ss"},
		{L'Đ', L"d"},
		{L'đ', L"d"},
		{L'Ħ', L"h"},
		{L'ħ', L"h"},
		{L'ı', L"i"},
		{L'Ł', L"l"},
		{L'ł', L"l"},
		{L'Œ', L"oe"},
		{L'œ', L"oe"},
	};
	wchar_t split[PLAYER_LABEL_LENGTH * 2];
	WORD types[PLAYER_LABEL_LENGTH * 2];
	int length = FoldStringW(MAP_COMPOSITE, text, -1, split, PLAYER_LABEL_LENGTH * 2);
	if (length <= 0 || !GetStringTypeW(CT_CTYPE3, split, length, types)) {
		return false;
	}
	size_t used = 0;
	for (int i = 0; i < length && split[i]; ++i) {
		const wchar_t *spelling = NULL;
		for (size_t j = 0; j < sizeof letters / sizeof letters[0] && !spelling; ++j) {
			spelling = split[i] == letters[j].letter ? letters[j].spelling : NULL;
		}
		// Letters such as Ø are typed as diacritics too; only the marks that are not letters go.
		if (!spelling && (types[i] & (C3_NONSPACING | C3_DIACRITIC)) && !(types[i] & C3_ALPHA)) {
			continue;
		}
		size_t count = spelling ? wcslen(spelling) : 1;
		if (used + count >= size) {
			return false;
		}
		if (spelling) {
			wmemcpy(out + used, spelling, count);
		} else {
			out[used] = split[i];
		}
		used += count;
	}
	out[used] = 0;
	CharLowerW(out);
	return true;
}

// Adds a choice listed as `label` and found by `matched`; returns false if memory runs out.
static bool
addChoice(ChoiceListJob *job, size_t *capacity, const wchar_t *label, const wchar_t *matched) {
	wchar_t folded[PLAYER_LABEL_LENGTH];
	if (!foldText(matched, folded, PLAYER_LABEL_LENGTH)) {
		return true; // not listed, but it can still be typed
	}
	if (job->count == *capacity) {
		size_t grown = *capacity ? *capacity * 2 : 1024;
		Choice *choices = realloc(job->choices, grown * sizeof *choices);
		if (!choices) {
			return false;
		}
		job->choices = choices;
		*capacity = grown;
	}
	Choice choice = {.label = _wcsdup(label), .folded = _wcsdup(folded)};
	if (!choice.label || !choice.folded) {
		free(choice.label);
		free(choice.folded);
		return false;
	}
	job->choices[job->count++] = choice;
	return true;
}

// Adds "name (id)"; a name that is not valid UTF-8 is skipped.
static bool addTeam(ChoiceListJob *job, size_t *capacity, unsigned long id, const char *name) {
	wchar_t wide[TEAM_LABEL_LENGTH];
	wchar_t label[TEAM_LABEL_LENGTH + 16];
	if (!widen(name, wide, TEAM_LABEL_LENGTH)) {
		return true;
	}
	(void)swprintf(label, sizeof label / sizeof label[0], L"%ls (%lu)", wide, id);
	return addChoice(job, capacity, label, label);
}

// Adds "Common name (Forename Surname) - Club (id)", leaving out what the player lacks; their
// names are what is matched. A player without names, or with text that is not valid UTF-8, is
// skipped. `fields` are the forename, surname, common name and club.
static bool
addPlayer(ChoiceListJob *job, size_t *capacity, unsigned long id, char *const fields[4]) {
	wchar_t names[4][TEAM_LABEL_LENGTH];
	for (size_t i = 0; i < 4; ++i) {
		if (!widen(fields[i], names[i], TEAM_LABEL_LENGTH)) {
			return true;
		}
	}
	wchar_t full[TEAM_LABEL_LENGTH * 2];
	(void)swprintf(
		full,
		sizeof full / sizeof full[0],
		L"%ls%ls%ls",
		names[0],
		names[0][0] && names[1][0] ? L" " : L"",
		names[1]
	);
	const wchar_t *common = names[2];
	if (!full[0] && !common[0]) {
		return true;
	}
	wchar_t label[PLAYER_LABEL_LENGTH];
	wchar_t matched[PLAYER_LABEL_LENGTH];
	(void)swprintf(
		label,
		PLAYER_LABEL_LENGTH,
		L"%ls%ls%ls%ls%ls%ls (%lu)",
		common[0] ? common : full,
		common[0] && full[0] ? L" (" : L"",
		common[0] ? full : L"",
		common[0] && full[0] ? L")" : L"",
		names[3][0] ? L" – " : L"",
		names[3],
		id
	);
	(void)swprintf(matched, PLAYER_LABEL_LENGTH, L"%ls %ls", full, common);
	return addChoice(job, capacity, label, matched);
}

// Adds one list line's choice (after "id<TAB>"); false if memory runs out.
static bool
addLine(ChoiceListJob *job, size_t *capacity, unsigned long id, char *rest, bool players) {
	if (!players) {
		return strlen(rest) >= TEAM_LABEL_LENGTH || addTeam(job, capacity, id, rest);
	}
	char *fields[4] = {rest, NULL, NULL, NULL};
	size_t count = 1;
	for (char *c = rest; *c && count < 4; ++c) {
		if (*c == '\t') {
			*c = 0;
			fields[count++] = c + 1;
		}
	}
	return count < 4 || strchr(fields[3], '\t') || addPlayer(job, capacity, id, fields);
}

// "id<TAB>short name" lines (fm24ctl list-teams), or (`players`) "id<TAB>forename<TAB>surname<TAB>
// common name<TAB>club" lines (fm24ctl list-players).
static bool parseChoices(const ByteBuffer *text, ChoiceListJob *job, bool players) {
	const char *kind = players ? "player" : "team";
	size_t capacity = 0;
	for (const char *line = buffer_string(text); *line;) {
		size_t length = strcspn(line, "\n");
		char *tab = NULL;
		unsigned long id = strtoul(line, &tab, 10);
		size_t restLength = length - (size_t)(tab - line) - 1;
		if (tab > line && *tab == '\t' && tab < line + length && restLength < PLAYER_LABEL_LENGTH) {
			char rest[PLAYER_LABEL_LENGTH];
			memcpy(rest, tab + 1, restLength);
			rest[restLength] = 0;
			rest[strcspn(rest, "\r")] = 0;
			if (!addLine(job, &capacity, id, rest, players)) {
				(void)snprintf(job->error, sizeof job->error, "out of memory listing %ss", kind);
				return false;
			}
		}
		line += line[length] ? length + 1 : length;
	}
	if (!job->count) {
		(void)snprintf(job->error, sizeof job->error, "the %s list is empty", kind);
		return false;
	}
	qsort(job->choices, job->count, sizeof *job->choices, compareChoices);
	return true;
}

// Runs list-teams, or (`players`) list-players, into a temporary file and parses it.
static DWORD loadChoices(ChoiceListJob *job, bool players) {
	const char *kind = players ? "player" : "team";
	wchar_t directory[HARNESS_PATH_LENGTH];
	wchar_t path[HARNESS_PATH_LENGTH];
	DWORD length = GetTempPathW(HARNESS_PATH_LENGTH, directory);
	if (!length || length >= HARNESS_PATH_LENGTH || !GetTempFileNameW(directory, L"fmt", 0, path)) {
		(void)snprintf(
			job->error,
			sizeof job->error,
			"cannot create a %s list file (Windows error %lu)",
			kind,
			GetLastError()
		);
		return 1;
	}
	wchar_t *arguments[] = {L"fm24ctl.exe", players ? L"list-players" : L"list-teams", path, NULL};
	ByteBuffer text = {0};
	int result = players ? commands_listPlayersWithResult(3, arguments, job->error, sizeof job->error)
											 : commands_listTeamsWithResult(3, arguments, job->error, sizeof job->error);
	if (result == 0 && !files_read(path, &text)) {
		(void)snprintf(job->error, sizeof job->error, "cannot read the %s list", kind);
		result = 1;
	}
	if (result == 0 && !parseChoices(&text, job, players)) {
		result = 1;
	}
	buffer_free(&text);
	(void)DeleteFileW(path);
	return (DWORD)result;
}

static DWORD WINAPI loadTeams(LPVOID parameter) {
	return loadChoices(parameter, false);
}

static DWORD WINAPI loadPlayers(LPVOID parameter) {
	return loadChoices(parameter, true);
}

// Starts a list's worker unless the list is loaded or loading; `timer` polls it.
static void startChoiceLoad(
	HWND window,
	const Choice *loaded,
	HANDLE *thread,
	ChoiceListJob *job,
	UINT_PTR timer,
	LPTHREAD_START_ROUTINE load
) {
	if (loaded || *thread) {
		return;
	}
	freeChoices(job->choices, job->count);
	*job = (ChoiceListJob){0};
	if (!SetTimer(window, timer, 200, NULL)) {
		return;
	}
	*thread = CreateThread(NULL, 0, load, job, 0, NULL);
	if (!*thread) {
		KillTimer(window, timer);
	}
}

// Fetches the team list in the background the first time either team is edited (FM must be
// running for the replacement anyway). A failure is retried on the next edit.
static void startTeamLoad(HWND window, WindowState *state) {
	startChoiceLoad(
		window, state->teams, &state->teamsThread, &state->teamsJob, TEAMS_TIMER, loadTeams
	);
}

// Fetches the player list in the background the first time the swap prompt opens (FM must be
// running for the swap anyway). A failure is retried on the next swap.
static void startPlayerLoad(HWND window, WindowState *state) {
	startChoiceLoad(
		window, state->players, &state->playersThread, &state->playersJob, PLAYERS_TIMER, loadPlayers
	);
}

// Collects a list's worker once it has finished; true if the list is now in `choices`.
static bool finishChoiceLoad(
	HWND window,
	HANDLE *thread,
	ChoiceListJob *job,
	UINT_PTR timer,
	Choice **choices,
	size_t *count
) {
	if (!*thread || WaitForSingleObject(*thread, 0) == WAIT_TIMEOUT) {
		return false;
	}
	KillTimer(window, timer);
	DWORD result = 1;
	if (!GetExitCodeThread(*thread, &result)) {
		result = 1;
	}
	CloseHandle(*thread);
	*thread = NULL;
	if (result != 0) {
		fprintf(
			stderr,
			"fixture viewer: cannot list %s: %s\n",
			timer == TEAMS_TIMER ? "teams" : "players",
			job->error
		);
		freeChoices(job->choices, job->count);
		*job = (ChoiceListJob){0};
		return false;
	}
	*choices = job->choices;
	*count = job->count;
	*job = (ChoiceListJob){0};
	return true;
}

// The side whose team picker `control` is (or is the edit field of), or 2 for neither.
static unsigned pickerSide(const WindowState *state, HWND control) {
	for (unsigned side = 0; side < 2; ++side) {
		HWND picker = state->teamControls[side][TEAM_INPUT];
		if (control && (control == picker || GetParent(control) == picker)) {
			return side;
		}
	}
	return 2;
}

// Where `part` is in `text`: 0 at its start (or, for `words`, at the start of any of its words),
// 1 only elsewhere, or -1 nowhere.
static int matchRank(const wchar_t *text, const wchar_t *part, bool words) {
	int rank = -1;
	for (const wchar_t *found = wcsstr(text, part); found; found = wcsstr(found + 1, part)) {
		if (found == text || (words && (found[-1] == L' ' || found[-1] == L'-'))) {
			return 0;
		}
		rank = 1;
		if (!words || !*found) {
			break;
		}
	}
	return rank;
}

// Lists (up to `limit`) the choices that match what is typed in `picker`, those that match at the
// start first (see matchRank), and drops the list down if asked and the picker has the focus.
// Fewer than `minimum` characters list nothing.
static void filterPicker(
	HWND picker,
	const Choice *choices,
	size_t count,
	size_t limit,
	size_t minimum,
	bool words,
	bool drop,
	bool *filtering
) {
	wchar_t text[PLAYER_LABEL_LENGTH] = L"";
	wchar_t folded[PLAYER_LABEL_LENGTH] = L"";
	DWORD start = 0;
	DWORD end = 0;
	GetWindowTextW(picker, text, PLAYER_LABEL_LENGTH);
	SendMessageW(picker, CB_GETEDITSEL, (WPARAM)&start, (LPARAM)&end);
	if (!foldText(text, folded, PLAYER_LABEL_LENGTH)) {
		folded[0] = 0;
	}
	size_t length = wcslen(folded);
	if (length < minimum) {
		count = 0;
	}
	*filtering = true;
	// Closing first lets the list resize to its new contents when it is dropped again.
	SendMessageW(picker, CB_SHOWDROPDOWN, FALSE, 0);
	SendMessageW(picker, WM_SETREDRAW, FALSE, 0);
	SendMessageW(picker, CB_RESETCONTENT, 0, 0);
	size_t shown = 0;
	for (int pass = 0; pass < 2; ++pass) {
		for (size_t i = 0; i < count && shown < limit; ++i) {
			if (matchRank(choices[i].folded, folded, words) == pass) {
				SendMessageW(picker, CB_ADDSTRING, 0, (LPARAM)choices[i].label);
				++shown;
			}
		}
	}
	SendMessageW(picker, WM_SETREDRAW, TRUE, 0);
	HWND focus = GetFocus();
	if (drop && shown && length && focus && (focus == picker || GetParent(focus) == picker)) {
		SendMessageW(picker, CB_SHOWDROPDOWN, TRUE, 0);
		// The drop-down hides the mouse pointer and may complete the text to its first match.
		SetCursor(LoadCursorW(NULL, IDC_ARROW));
	}
	SetWindowTextW(picker, text);
	SendMessageW(picker, CB_SETEDITSEL, 0, MAKELPARAM(start, end));
	*filtering = false;
}

// Lists the teams whose label contains the typed text (those starting with it first), and drops
// the list down if asked and the picker has the focus.
static void filterTeams(WindowState *state, unsigned side, bool drop) {
	filterPicker(
		state->teamControls[side][TEAM_INPUT],
		state->teams,
		state->teamCount,
		TEAMS_SHOWN,
		0,
		false,
		drop,
		&state->filteringTeams
	);
}

static void checkTeams(HWND window, WindowState *state) {
	if (!finishChoiceLoad(
				window, &state->teamsThread, &state->teamsJob, TEAMS_TIMER, &state->teams, &state->teamCount
			)) {
		return;
	}
	for (unsigned side = 0; side < 2; ++side) {
		if (state->editingTeam[side]) {
			filterTeams(state, side, false);
		}
	}
}

// Collects the player list's worker, and tells an open swap prompt that it has finished.
static void checkPlayers(HWND window, WindowState *state) {
	if (!state->playersThread || WaitForSingleObject(state->playersThread, 0) == WAIT_TIMEOUT) {
		return;
	}
	(void)finishChoiceLoad(
		window,
		&state->playersThread,
		&state->playersJob,
		PLAYERS_TIMER,
		&state->players,
		&state->playerCount
	);
	if (state->swapPrompt) {
		SendMessageW(state->swapPrompt, WM_SWAP_PLAYERS, 0, 0);
	}
}

// The ID in the trailing "(ID)" of a label that is one of the choices', so that a shared name
// still resolves.
static bool
listedId(const Choice *choices, size_t count, const wchar_t *text, wchar_t *id, size_t size) {
	for (size_t i = 0; i < count; ++i) {
		if (wcscmp(choices[i].label, text) == 0) {
			const wchar_t *open = wcsrchr(text, L'(');
			size_t length = open ? wcscspn(open + 1, L")") : 0;
			if (!length || length >= size) {
				return false;
			}
			wmemcpy(id, open + 1, length);
			id[length] = 0;
			return true;
		}
	}
	return false;
}

// The ID for a label picked from the list, so that a club whose name is shared still resolves.
static bool pickedTeamId(const WindowState *state, const wchar_t *text, wchar_t *id, size_t size) {
	return listedId(state->teams, state->teamCount, text, id, size);
}

static void editTeam(HWND window, WindowState *state, unsigned side, bool editing) {
	if (state->fileThread || state->replacementThread || (editing && state->simulationThread)) {
		return;
	}
	if (editing && !setText(state->teamControls[side][TEAM_INPUT], state->data->teams[side].name)) {
		showError(window, L"Cannot display the team name.");
		return;
	}
	if (editing) {
		startTeamLoad(window, state);
		filterTeams(state, side, false);
	}
	state->editingTeam[side] = editing;
	if (editing) {
		state->editingTeam[1 - side] = false;
	}
	SetWindowTextW(state->teamControls[side][TEAM_ERROR], L"");
	if (!setHeadings(state)) {
		showError(window, L"Cannot update team headings.");
		state->failed = true;
		DestroyWindow(window);
		return;
	}
	selectTab(state);
	layout(window, state);
	SetFocus(state->teamControls[side][editing ? TEAM_INPUT : TEAM_ICON]);
	if (editing) {
		SendMessageW(state->teamControls[side][TEAM_INPUT], CB_SETEDITSEL, 0, MAKELPARAM(0, -1));
	}
}

static DWORD WINAPI replaceTeam(LPVOID parameter) {
	ReplaceTeamJob *job = parameter;
	if (!temporaryJson(job->output, job->error, sizeof job->error)) {
		return 1;
	}
	wchar_t *arguments[] = {L"fm24ctl.exe",
		job->side ? L"replace-away" : L"replace-home",
		job->input,
		job->team,
		job->output,
		NULL};
	if (files_hasExtension(job->input, L".bin") || fixtureData_hasPlayerEdits(&job->playerEdits)) {
		if (!fixtureLoad_saveEdits(
					job->input, job->output, NULL, &job->playerEdits, job->error, sizeof job->error
				)) {
			return 1;
		}
		arguments[2] = job->output;
	}
	int result = 0;
	if (job->swap) {
		// "Allow duplicates?" keeps the block's own unique ID, as the player is already in the fixture.
		wchar_t *swapArguments[] = {L"fm24ctl.exe",
			L"inject-player",
			arguments[2],
			job->block,
			job->person,
			job->output,
			job->keepUid ? L"--keep-uid" : NULL,
			NULL};
		result = commands_injectPlayerWithResult(
			job->keepUid ? 7 : 6, swapArguments, job->error, sizeof job->error
		);
	} else {
		result = commands_replaceTeamWithResult(5, arguments, job->error, sizeof job->error);
	}
	if (result == 0 && hasVenue(job->venueValues) &&
			!fixtureLoad_saveVenue(
				job->output, job->output, job->venueValues, job->error, sizeof job->error
			)) {
		result = 1;
	}
	if (result == 0 && !fixtureLoad_open(job->output, job->data, job->error, sizeof job->error)) {
		result = 1;
	}
	return (DWORD)result;
}

static void startReplacement(HWND window, WindowState *state, unsigned side) {
	if (!state->editingTeam[side] || state->simulationThread || state->replacementThread ||
			state->fileThread) {
		return;
	}
	ReplaceTeamJob *job = &state->replacementJob;
	job->side = side;
	job->playerEdits = state->playerEdits;
	memset(job->playerEdits.players[side], 0, sizeof job->playerEdits.players[side]);
	job->swap = false;
	int length = GetWindowTextW(state->teamControls[side][TEAM_INPUT], job->team, 256);
	char team[256];
	if (!length || GetWindowTextLengthW(state->teamControls[side][TEAM_INPUT]) >= 256 ||
			wcsspn(job->team, L" \t") == (size_t)length || wcspbrk(job->team, L"\r\n") ||
			!paths_toUtf8(job->team, team, sizeof team)) {
		teamError(window, state, side, "Enter a team ID or club short name (up to 255 UTF-8 bytes).");
		SetFocus(state->teamControls[side][TEAM_INPUT]);
		return;
	}
	wchar_t id[16];
	if (pickedTeamId(state, job->team, id, sizeof id / sizeof id[0])) {
		wcscpy(job->team, id);
	}
	const char *error = launchReplacement(window, state);
	if (error) {
		if (error[0]) {
			teamError(window, state, side, error);
		}
		return;
	}
	SetWindowTextW(state->teamControls[side][TEAM_ERROR], L"");
	ShowWindow(state->teamControls[side][TEAM_ERROR], SW_HIDE);
	SetFocus(state->tabs);
}

// Starts the worker for state->replacementJob, whose side and team (or swap) are set. Returns NULL,
// or why it cannot start ("" if readVenue has already said why).
static const char *launchReplacement(HWND window, WindowState *state) {
	ReplaceTeamJob *job = &state->replacementJob;
	if (!readVenue(window, state, job->venueValues)) {
		return "";
	}
	const wchar_t *input = state->savedFilename[0] ? state->savedFilename : state->path;
	if (!paths_absolute(input, job->input, HARNESS_PATH_LENGTH)) {
		return "Cannot resolve the fixture path.";
	}
	job->data = malloc(sizeof *job->data);
	if (!job->data) {
		return "Out of memory loading the replacement fixture.";
	}
	job->output[0] = 0;
	job->error[0] = 0;
	if (!SetTimer(window, REPLACE_TIMER, 200, NULL)) {
		free(job->data);
		job->data = NULL;
		return job->swap ? "Cannot monitor the player swap." : "Cannot monitor the team replacement.";
	}
	state->replacementThread = CreateThread(NULL, 0, replaceTeam, job, 0, NULL);
	if (!state->replacementThread) {
		KillTimer(window, REPLACE_TIMER);
		free(job->data);
		job->data = NULL;
		return job->swap ? "Cannot start the player swap." : "Cannot start the team replacement.";
	}
	enableReplacementActions(state, FALSE);
	return NULL;
}

static void appendTemplate(DialogTemplate *dialog, const void *data, size_t size) {
	if (dialog->length + size > sizeof dialog->bytes) {
		dialog->overflow = true;
		return;
	}
	memcpy(dialog->bytes + dialog->length, data, size);
	dialog->length += size;
}

static void appendTemplateString(DialogTemplate *dialog, const wchar_t *text) {
	appendTemplate(dialog, text, (wcslen(text) + 1) * sizeof *text);
}

// One control: DWORD-aligned, with a predefined class (0x80 button, 0x81 edit, 0x82 static,
// 0x85 combo box).
static void appendTemplateItem(
	DialogTemplate *dialog,
	DWORD style,
	const short bounds[4],
	WORD id,
	WORD classAtom,
	const wchar_t *text
) {
	static const BYTE padding[3] = {0};
	appendTemplate(dialog, padding, (4 - dialog->length % 4) % 4);
	DLGITEMTEMPLATE item = {.style = WS_CHILD | WS_VISIBLE | style,
		.x = bounds[0],
		.y = bounds[1],
		.cx = bounds[2],
		.cy = bounds[3],
		.id = id};
	const WORD itemClass[2] = {0xFFFF, classAtom};
	const WORD creationData = 0;
	appendTemplate(dialog, &item, sizeof item);
	appendTemplate(dialog, itemClass, sizeof itemClass);
	appendTemplateString(dialog, text);
	appendTemplate(dialog, &creationData, sizeof creationData);
}

// The swap prompt (dialog units): "Swap <name> for...", set once it opens, over the new player's
// drop-down (a name to search for, or a unique ID), then "Allow duplicates?", Confirm and Cancel.
static bool swapPromptTemplate(DialogTemplate *dialog) {
	static const short newLabelBounds[4] = {7, 9, 306, 10};
	static const short newPlayerBounds[4] = {7, 22, 306, 150};
	static const short duplicatesBounds[4] = {7, 45, 140, 12};
	static const short confirmBounds[4] = {209, 44, 50, 14};
	static const short cancelBounds[4] = {263, 44, 50, 14};
	*dialog = (DialogTemplate){0};
	DLGTEMPLATE header = {
		.style = DS_MODALFRAME | DS_CENTER | DS_SETFONT | WS_POPUP | WS_CAPTION | WS_SYSMENU,
		.cdit = 5,
		.cx = 320,
		.cy = 65};
	const WORD noMenuOrClass[2] = {0, 0};
	const WORD fontPoints = 9;
	appendTemplate(dialog, &header, sizeof header);
	appendTemplate(dialog, noMenuOrClass, sizeof noMenuOrClass);
	appendTemplateString(dialog, L"Swap player");
	appendTemplate(dialog, &fontPoints, sizeof fontPoints);
	appendTemplateString(dialog, L"Segoe UI");
	appendTemplateItem(
		dialog, SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, newLabelBounds, SWAP_PROMPT_NEW_LABEL, 0x82, L""
	);
	appendTemplateItem(
		dialog,
		WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL,
		newPlayerBounds,
		SWAP_PROMPT_NEW_PLAYER,
		0x85,
		L""
	);
	appendTemplateItem(
		dialog,
		BS_AUTOCHECKBOX | WS_TABSTOP,
		duplicatesBounds,
		SWAP_PROMPT_DUPLICATES,
		0x80,
		L"Allow duplicates?"
	);
	appendTemplateItem(dialog, BS_DEFPUSHBUTTON | WS_TABSTOP, confirmBounds, IDOK, 0x80, L"Confirm");
	appendTemplateItem(dialog, BS_PUSHBUTTON | WS_TABSTOP, cancelBounds, IDCANCEL, 0x80, L"Cancel");
	return !dialog->overflow;
}

// A nonzero decimal u32: a person's unique ID.
static bool validSwapId(const wchar_t *text) {
	wchar_t *end = NULL;
	errno = 0;
	unsigned long long id = wcstoull(text, &end, 10);
	return text[0] >= L'0' && text[0] <= L'9' && *end == 0 && errno == 0 && id > 0 &&
				 id <= UINT32_MAX;
}

// Labels the new player's field "Swap <name> for...".
static bool setSwapLabel(HWND dialog, const char *name) {
	wchar_t wide[FIXTURE_TEXT];
	wchar_t label[FIXTURE_TEXT + 16];
	if (!widen(name, wide, FIXTURE_TEXT)) {
		return false;
	}
	(void)swprintf(label, sizeof label / sizeof label[0], L"Swap %ls for...", wide);
	return SetDlgItemTextW(dialog, SWAP_PROMPT_NEW_LABEL, label);
}

// What the new player's field takes: a name once the player list has loaded, otherwise only an ID.
static void setSwapCue(HWND dialog, const WindowState *state) {
	const wchar_t *cue = L"Unique ID";
	if (state->players) {
		cue = L"Player name or unique ID";
	} else if (state->playersThread) {
		cue = L"Unique ID (loading player names…)";
	}
	SendDlgItemMessageW(dialog, SWAP_PROMPT_NEW_PLAYER, CB_SETCUEBANNER, 0, (LPARAM)cue);
}

// Lists the players whose names match what is typed, unless it is a unique ID.
static void filterSwapPlayers(HWND dialog, SwapPrompt *prompt) {
	HWND picker = GetDlgItem(dialog, SWAP_PROMPT_NEW_PLAYER);
	wchar_t text[PLAYER_LABEL_LENGTH] = L"";
	GetWindowTextW(picker, text, PLAYER_LABEL_LENGTH);
	bool id = text[0] && wcsspn(text, L"0123456789") == wcslen(text);
	filterPicker(
		picker,
		prompt->state->players,
		id || !prompt->state->players ? 0 : prompt->state->playerCount,
		PLAYERS_SHOWN,
		PLAYER_FILTER_MINIMUM,
		true,
		true,
		&prompt->filtering
	);
}

static void swapPromptError(HWND dialog, const wchar_t *message, int control) {
	MessageBoxW(dialog, message, L"Swap player", MB_OK | MB_ICONERROR);
	SetFocus(GetDlgItem(dialog, control));
}

// The prompt's answer: the new player's unique ID (from the label picked from the list, or as
// typed). Returns false, having said why, if it is missing.
static bool readSwapPrompt(HWND dialog, SwapPrompt *prompt) {
	wchar_t text[PLAYER_LABEL_LENGTH] = L"";
	GetDlgItemTextW(dialog, SWAP_PROMPT_NEW_PLAYER, text, PLAYER_LABEL_LENGTH);
	const WindowState *state = prompt->state;
	if (!listedId(state->players, state->playerCount, text, prompt->id, SWAP_ID_LENGTH)) {
		prompt->id[0] = 0;
		if (wcslen(text) < SWAP_ID_LENGTH) {
			wcscpy(prompt->id, text);
		}
	}
	if (!validSwapId(prompt->id)) {
		swapPromptError(
			dialog,
			L"Choose the new player from the list, or enter their unique ID: a number from 1 to "
			L"4294967295.",
			SWAP_PROMPT_NEW_PLAYER
		);
		return false;
	}
	prompt->allowDuplicates = IsDlgButtonChecked(dialog, SWAP_PROMPT_DUPLICATES) == BST_CHECKED;
	return true;
}

static INT_PTR CALLBACK
swapPromptProcedure(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
	// NOLINTBEGIN(performance-no-int-to-ptr): lParam and DWLP_USER carry the prompt's pointer.
	SwapPrompt *prompt = (SwapPrompt *)GetWindowLongPtrW(dialog, DWLP_USER);
	if (message == WM_INITDIALOG) {
		prompt = (SwapPrompt *)lParam;
		// NOLINTEND(performance-no-int-to-ptr)
		SetWindowLongPtrW(dialog, DWLP_USER, lParam);
		prompt->state->swapPrompt = dialog;
		SendDlgItemMessageW(dialog, SWAP_PROMPT_NEW_PLAYER, CB_LIMITTEXT, PLAYER_LABEL_LENGTH - 1, 0);
		setSwapCue(dialog, prompt->state);
		if (!setSwapLabel(dialog, prompt->name)) {
			EndDialog(dialog, -1);
		}
		return TRUE;
	}
	if (!prompt) {
		return FALSE;
	}
	switch (message) {
		case WM_SWAP_PLAYERS:
			setSwapCue(dialog, prompt->state);
			filterSwapPlayers(dialog, prompt);
			return TRUE;
		case WM_TIMER:
			if (wParam != SWAP_FILTER_TIMER) {
				return FALSE;
			}
			KillTimer(dialog, SWAP_FILTER_TIMER);
			filterSwapPlayers(dialog, prompt);
			return TRUE;
		case WM_DESTROY:
			KillTimer(dialog, SWAP_FILTER_TIMER);
			if (prompt->state->swapPrompt == dialog) {
				prompt->state->swapPrompt = NULL;
			}
			return FALSE;
		case WM_COMMAND: break;
		default: return FALSE;
	}
	if (LOWORD(wParam) == SWAP_PROMPT_NEW_PLAYER && HIWORD(wParam) == CBN_EDITCHANGE) {
		// Typing restarts the delay, so the list is refilled once the typing pauses.
		if (!prompt->filtering) {
			SetTimer(dialog, SWAP_FILTER_TIMER, PLAYER_FILTER_DELAY_MS, NULL);
		}
		return TRUE;
	}
	if (LOWORD(wParam) == IDCANCEL) {
		EndDialog(dialog, IDCANCEL);
		return TRUE;
	}
	if (LOWORD(wParam) != IDOK) {
		return FALSE;
	}
	if (readSwapPrompt(dialog, prompt)) {
		EndDialog(dialog, IDOK);
	}
	return TRUE;
}

// Asks for the player to put in place of the side's selected player (searched for by name, or
// given by unique ID), then injects that player into a temporary copy of the fixture, shown as
// unsaved.
static void startSwap(HWND window, WindowState *state, unsigned side) {
	int block = selectedBlock(state, side);
	if (block < 0 || state->simulationThread || state->replacementThread || state->fileThread) {
		return;
	}
	startPlayerLoad(window, state);
	SwapPrompt prompt = {
		.state = state, .name = state->data->teams[side].players[block].name, .block = block};
	DialogTemplate dialog;
	INT_PTR answer = swapPromptTemplate(&dialog) ? DialogBoxIndirectParamW(
																									 GetModuleHandleW(NULL),
																									 (LPCDLGTEMPLATEW)dialog.bytes,
																									 window,
																									 swapPromptProcedure,
																									 (LPARAM)&prompt
																								 )
																							 : -1;
	if (answer == -1) {
		showError(window, L"Cannot show the swap prompt.");
	}
	// The prompt's message loop may have let another job start.
	if (answer != IDOK || state->simulationThread || state->replacementThread || state->fileThread) {
		return;
	}
	ReplaceTeamJob *job = &state->replacementJob;
	job->side = side;
	job->swap = true;
	// The pending edits carry over, except the swapped-out player's.
	job->playerEdits = state->playerEdits;
	job->playerEdits.players[side][prompt.block] = 0;
	(void)swprintf(
		job->block,
		sizeof job->block / sizeof job->block[0],
		L"%ls.player[%d]",
		side ? L"away" : L"home",
		prompt.block
	);
	wcscpy(job->person, prompt.id);
	job->keepUid = prompt.allowDuplicates;
	const char *error = launchReplacement(window, state);
	if (error && error[0]) {
		showFileError(window, error);
	}
}

// The team editors, simulation, file buttons, venue, swap and player buttons, which wait while
// a side is being rebuilt.
static void enableReplacementActions(const WindowState *state, BOOL enabled) {
	for (unsigned side = 0; side < 2; ++side) {
		for (unsigned i = TEAM_ICON; i <= TEAM_CANCEL; ++i) {
			EnableWindow(state->teamControls[side][i], enabled);
		}
	}
	EnableWindow(state->simulation[SIM_SUBMIT], enabled);
	for (unsigned i = 0; i < FILE_CONTROL_COUNT; ++i) {
		EnableWindow(state->fileButtons[i], enabled);
	}
	enableVenue(state, enabled);
	enablePlayers(state);
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
		showError(window, L"Cannot read the team replacement result.");
		state->failed = true;
		DestroyWindow(window);
		return;
	}
	KillTimer(window, REPLACE_TIMER);
	CloseHandle(state->replacementThread);
	state->replacementThread = NULL;
	enableReplacementActions(state, TRUE);
	ReplaceTeamJob *job = &state->replacementJob;
	unsigned side = job->side;
	if (result == 0) {
		removeReplacement(state->replacementPath);
		wcscpy(state->replacementPath, job->output);
		job->output[0] = 0;
		state->path = state->replacementPath;
		state->savedFilename[0] = 0;
		state->temporary = true;
		state->missing = false;
		*state->data = *job->data;
		memset(&state->playerEdits, 0, sizeof state->playerEdits);
		memset(state->selectedPlayers, 0, sizeof state->selectedPlayers);
		state->editingTeam[0] = false;
		state->editingTeam[1] = false;
		if (!setVenue(state) || !setMetadata(state) || !setHeadings(state) || !fillList(state, 0) ||
				!fillList(state, 1)) {
			showError(window, L"Cannot display the replacement fixture.");
			state->failed = true;
			DestroyWindow(window);
		} else {
			for (unsigned teamSide = 0; teamSide < 2; ++teamSide) {
				InvalidateRect(state->pitches[teamSide], NULL, TRUE);
			}
			selectTab(state);
			layout(window, state);
			HWND focus = job->swap ? state->lists[side] : state->teamControls[side][TEAM_ICON];
			SetFocus(TabCtrl_GetCurSel(state->tabs) == 0 ? focus : state->tabs);
		}
	} else if (job->swap) {
		// The prompt's checkbox stands for inject-player's --keep-uid.
		char *hint = strstr(job->error, "use --keep-uid to inject anyway");
		if (hint) {
			(void)snprintf(
				hint,
				sizeof job->error - (size_t)(hint - job->error),
				"tick \"Allow duplicates?\" to swap them in anyway"
			);
		}
		showFileError(window, job->error);
	} else {
		teamError(window, state, side, job->error);
		if (TabCtrl_GetCurSel(state->tabs) == 0) {
			SetFocus(state->teamControls[side][TEAM_INPUT]);
		}
	}
	removeReplacement(job->output);
	job->output[0] = 0;
	free(job->data);
	job->data = NULL;
}

static const wchar_t *pathFilename(const wchar_t *path) {
	const wchar_t *filename = path;
	for (; *path; ++path) {
		if (*path == L'\\' || *path == L'/') {
			filename = path + 1;
		}
	}
	return filename;
}

static void setActivePath(WindowState *state, const wchar_t *path, bool temporary) {
	if (state->replacementPath[0]) {
		if (_wcsicmp(state->replacementPath, path) != 0) {
			removeReplacement(state->replacementPath);
		}
		state->replacementPath[0] = 0;
	}
	wcscpy(state->savedFilename, path);
	state->path = state->savedFilename;
	state->filename = pathFilename(state->savedFilename);
	state->temporary = temporary;
	state->missing = false;
}

static void showFileError(HWND window, const char *error) {
	fprintf(stderr, "fixture viewer: %s\n", error);
	wchar_t message[1024];
	showError(
		window,
		widen(error, message, 1024) ? message
																: L"Fixture file operation failed (invalid UTF-8 in error)."
	);
}

static DWORD WINAPI fixtureFile(LPVOID parameter) {
	FileJob *job = parameter;
	return !(
		job->save ? fixtureLoad_saveEdits(
									job->input,
									job->output,
									hasVenue(job->venueValues) ? job->venueValues : NULL,
									&job->playerEdits,
									job->error,
									sizeof job->error
								)
							: fixtureLoad_open(job->input, job->data, job->error, sizeof job->error)
	);
}

// Locks out (or restores) the controls that would race a fixture file operation.
static void enableFileControls(const WindowState *state, BOOL enable) {
	for (unsigned i = 0; i < FILE_CONTROL_COUNT; ++i) {
		EnableWindow(state->fileButtons[i], enable);
	}
	for (unsigned side = 0; side < 2; ++side) {
		for (unsigned i = TEAM_ICON; i <= TEAM_CANCEL; ++i) {
			EnableWindow(state->teamControls[side][i], enable);
		}
	}
	EnableWindow(state->simulation[SIM_SUBMIT], enable);
	enableVenue(state, enable);
	enablePlayers(state);
}

static bool
startFileJob(HWND window, WindowState *state, const wchar_t *path, bool save, bool temporary) {
	if (state->simulationThread || state->replacementThread || state->fileThread) {
		showError(window, L"Please wait for the current fixture operation to finish.");
		return false;
	}
	FileJob *job = &state->fileJob;
	*job = (FileJob){.save = save, .temporary = temporary};
	job->playerEdits = state->playerEdits;
	if (save && !readVenue(window, state, job->venueValues)) {
		return false;
	}
	const wchar_t *input =
		save ? (state->savedFilename[0] ? state->savedFilename : state->path) : path;
	if (!paths_absolute(input, job->input, HARNESS_PATH_LENGTH) ||
			(save && !paths_absolute(path, job->output, HARNESS_PATH_LENGTH))) {
		showError(window, L"Cannot resolve the fixture path.");
		return false;
	}
	if (!save) {
		job->data = calloc(1, sizeof *job->data);
		if (!job->data) {
			showError(window, L"Out of memory loading the fixture.");
			return false;
		}
	}
	if (!SetTimer(window, FILE_TIMER, 200, NULL)) {
		free(job->data);
		job->data = NULL;
		showError(window, L"Cannot monitor the fixture file operation.");
		return false;
	}
	state->fileThread = CreateThread(NULL, 0, fixtureFile, job, 0, NULL);
	if (!state->fileThread) {
		KillTimer(window, FILE_TIMER);
		free(job->data);
		job->data = NULL;
		showError(window, L"Cannot start the fixture file operation.");
		return false;
	}
	enableFileControls(state, FALSE);
	SetFocus(state->tabs);
	return true;
}

// Shows the fixture a successful file operation opened or saved.
static void showFileResult(HWND window, WindowState *state, const FileJob *job) {
	setActivePath(state, job->save ? job->output : job->input, job->temporary);
	memset(&state->playerEdits, 0, sizeof state->playerEdits);
	if (job->save) {
		memcpy(state->data->venueValues, job->venueValues, sizeof job->venueValues);
	}
	if (!job->save) {
		*state->data = *job->data;
		memset(state->selectedPlayers, 0, sizeof state->selectedPlayers);
		for (unsigned side = 0; side < 2; ++side) {
			state->editingTeam[side] = false;
			SetWindowTextW(state->teamControls[side][TEAM_ERROR], L"");
		}
		state->progressCompleted = 0;
		state->progressTotal = 0;
		SetWindowTextW(state->simulation[SIM_SUMMARY], L"");
		SetWindowTextW(
			state->simulation[SIM_STATUS],
			L"Results are saved beside the fixture, using its filename with .csv."
		);
	}
	bool ok = setVenue(state) && setMetadata(state);
	if (!job->save) {
		ok = ok && setHeadings(state) && fillList(state, 0) && fillList(state, 1);
	}
	if (!ok) {
		showError(window, L"Cannot display the fixture.");
		state->failed = true;
		DestroyWindow(window);
		return;
	}
	for (unsigned side = 0; side < 2; ++side) {
		InvalidateRect(state->pitches[side], NULL, TRUE);
	}
	selectTab(state);
	layout(window, state);
}

static void checkFileJob(HWND window, WindowState *state) {
	if (!state->fileThread) {
		return;
	}
	DWORD wait = WaitForSingleObject(state->fileThread, 0);
	if (wait == WAIT_TIMEOUT) {
		return;
	}
	DWORD result = 1;
	if (wait != WAIT_OBJECT_0 || !GetExitCodeThread(state->fileThread, &result)) {
		KillTimer(window, FILE_TIMER);
		showError(window, L"Cannot read the fixture file operation result.");
		state->failed = true;
		DestroyWindow(window);
		return;
	}
	KillTimer(window, FILE_TIMER);
	CloseHandle(state->fileThread);
	state->fileThread = NULL;
	enableFileControls(state, TRUE);
	FileJob *job = &state->fileJob;
	if (result == 0) {
		showFileResult(window, state, job);
	} else {
		showFileError(window, job->error);
	}
	free(job->data);
	job->data = NULL;
}

static HRESULT configureFileDialog(
	IFileDialog *dialog,
	const wchar_t *directory,
	const wchar_t *filename,
	bool save,
	bool binary
) {
	static const COMDLG_FILTERSPEC openTypes[] = {
		{L"Fixture files (*.bin; *.json)", L"*.bin;*.json"},
		{L"JSON fixture (*.json)", L"*.json"},
		{L"Binary fixture (*.bin)", L"*.bin"},
	};
	static const COMDLG_FILTERSPEC saveTypes[] = {
		{L"JSON fixture (*.json)", L"*.json"},
		{L"Binary fixture (*.bin)", L"*.bin"},
	};
	HRESULT result = dialog->lpVtbl->SetOptions(
		dialog,
		FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR |
			(save ? FOS_OVERWRITEPROMPT | FOS_STRICTFILETYPES : FOS_FILEMUSTEXIST)
	);
	if (SUCCEEDED(result)) {
		result = dialog->lpVtbl->SetFileTypes(dialog, save ? 2 : 3, save ? saveTypes : openTypes);
	}
	if (SUCCEEDED(result)) {
		result = dialog->lpVtbl->SetFileTypeIndex(dialog, save && binary ? 2 : 1);
	}
	if (SUCCEEDED(result)) {
		result = dialog->lpVtbl->SetTitle(dialog, save ? L"Save fixture as" : L"Open fixture");
	}
	if (SUCCEEDED(result) && save) {
		result = dialog->lpVtbl->SetDefaultExtension(dialog, binary ? L"bin" : L"json");
		if (SUCCEEDED(result)) {
			result = dialog->lpVtbl->SetFileName(dialog, filename);
		}
	}
	IShellItem *folder = NULL;
	if (SUCCEEDED(result)) {
		result = SHCreateItemFromParsingName(directory, NULL, &IID_IShellItem, (void **)&folder);
	}
	if (SUCCEEDED(result)) {
		result = dialog->lpVtbl->SetFolder(dialog, folder);
	}
	if (folder) {
		folder->lpVtbl->Release(folder);
	}
	return result;
}

static void useSelectedFile(HWND window, WindowState *state, const wchar_t *path, bool save) {
	if (wcslen(path) >= HARNESS_PATH_LENGTH ||
			(!files_hasExtension(path, L".json") && !files_hasExtension(path, L".bin"))) {
		showError(window, L"Select a .json or .bin fixture with a path shorter than 1024 characters.");
	} else if (save) {
		(void)startFileJob(window, state, path, true, false);
	} else if ((!state->temporary && !state->venueDirty &&
							 !fixtureData_hasPlayerEdits(&state->playerEdits)) ||
						 MessageBoxW(
							 window,
							 L"The current fixture has unsaved changes. Discard them and open the selected "
							 L"fixture?",
							 L"Open fixture",
							 MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2
						 ) == IDYES) {
		(void)fixtureWindow_open(path, false);
	}
}

static void chooseFixtureFile(HWND window, WindowState *state, bool save) {
	if (state->simulationThread || state->replacementThread || state->fileThread) {
		showError(window, L"Please wait for the current fixture operation to finish.");
		return;
	}
	wchar_t directory[HARNESS_PATH_LENGTH];
	DWORD length = GetCurrentDirectoryW(HARNESS_PATH_LENGTH, directory);
	if (!length || length >= HARNESS_PATH_LENGTH) {
		showError(window, L"Cannot read the current working directory.");
		return;
	}
	const wchar_t *source = state->savedFilename[0] ? state->savedFilename : state->path;
	bool binary = files_hasExtension(source, L".bin");
	const wchar_t *filename = pathFilename(source);
	IFileDialog *dialog = NULL;
	IShellItem *selected = NULL;
	wchar_t *path = NULL;
	HRESULT result = CoCreateInstance(
		save ? &CLSID_FileSaveDialog : &CLSID_FileOpenDialog,
		NULL,
		CLSCTX_INPROC_SERVER,
		&IID_IFileDialog,
		(void **)&dialog
	);
	if (SUCCEEDED(result)) {
		result = configureFileDialog(dialog, directory, filename, save, binary);
	}
	if (SUCCEEDED(result)) {
		result = dialog->lpVtbl->Show(dialog, window);
	}
	if (SUCCEEDED(result)) {
		result = dialog->lpVtbl->GetResult(dialog, &selected);
	}
	if (SUCCEEDED(result)) {
		result = selected->lpVtbl->GetDisplayName(selected, SIGDN_FILESYSPATH, &path);
	}
	if (SUCCEEDED(result)) {
		useSelectedFile(window, state, path, save);
	} else if (result != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
		wchar_t message[128];
		(void)swprintf(
			message, 128, L"Cannot use the fixture file dialog (HRESULT 0x%08lx).", (unsigned long)result
		);
		showError(window, message);
	}
	CoTaskMemFree(path);
	if (selected) {
		selected->lpVtbl->Release(selected);
	}
	if (dialog) {
		dialog->lpVtbl->Release(dialog);
	}
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
	for (unsigned i = 0; i < FILE_CONTROL_COUNT; ++i) {
		SendMessageW(state->fileButtons[i], WM_SETFONT, (WPARAM)font, TRUE);
	}
	SendMessageW(state->tabs, WM_SETFONT, (WPARAM)font, TRUE);
	for (unsigned i = 0; i < SIM_CONTROL_COUNT; ++i) {
		SendMessageW(state->simulation[i], WM_SETFONT, (WPARAM)font, TRUE);
	}
	for (unsigned i = 0; i < VENUE_FIELD_COUNT; ++i) {
		SendMessageW(state->venueLabels[i], WM_SETFONT, (WPARAM)font, TRUE);
		SendMessageW(state->venueInputs[i], WM_SETFONT, (WPARAM)font, TRUE);
	}
	for (unsigned i = 0; i < 2; ++i) {
		for (unsigned button = 0; button < PLAYER_BUTTON_COUNT; ++button) {
			SendMessageW(state->playerButtons[i][button], WM_SETFONT, (WPARAM)font, TRUE);
		}
		for (unsigned control = 0; control < TEAM_CONTROL_COUNT; ++control) {
			SendMessageW(state->teamControls[i][control], WM_SETFONT, (WPARAM)font, TRUE);
		}
		SendMessageW(state->headings[i], WM_SETFONT, (WPARAM)heading, TRUE);
		SendMessageW(state->swapButtons[i], WM_SETFONT, (WPARAM)font, TRUE);
		SendMessageW(state->lists[i], WM_SETFONT, (WPARAM)font, TRUE);
		SendMessageW(state->pitches[i], WM_SETFONT, (WPARAM)font, TRUE);
		for (unsigned column = 0; column < sizeof columns / sizeof columns[0]; ++column) {
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

// A combo box's height includes its list; its own field is sized by its item height, which is set
// to make the field `height` tall like the buttons beside it.
static void
placeTeamPicker(const WindowState *state, unsigned side, int x, int y, int width, int height) {
	HWND picker = state->teamControls[side][TEAM_INPUT];
	MoveWindow(picker, x, y, width, height + scaled(state, 300), TRUE);
	RECT field = {0};
	GetWindowRect(picker, &field);
	int itemHeight = (int)SendMessageW(picker, CB_GETITEMHEIGHT, (WPARAM)-1, 0);
	int adjust = height - (int)(field.bottom - field.top);
	if (adjust && itemHeight + adjust > 0) {
		SendMessageW(picker, CB_SETITEMHEIGHT, (WPARAM)-1, itemHeight + adjust);
	}
	SendMessageW(picker, CB_SETDROPPEDWIDTH, (WPARAM)scaled(state, 320), 0);
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
	RECT textBounds;
	GetClientRect(state->metadata, &textBounds);
	textBounds.right -= scaled(state, 192);
	SendMessageW(state->metadata, EM_SETRECTNP, 0, (LPARAM)&textBounds);
	SendMessageW(state->metadata, EM_REQUESTRESIZE, 0, 0);
	MoveWindow(state->metadata, margin, margin, width, state->metadataHeight, TRUE);
	for (unsigned i = 0; i < FILE_CONTROL_COUNT; ++i) {
		MoveWindow(
			state->fileButtons[i],
			margin + width - scaled(state, 180) + (int)i * scaled(state, 90),
			margin + scaled(state, 10),
			scaled(state, 78),
			scaled(state, 26),
			TRUE
		);
	}
	int top = margin + state->metadataHeight + margin;
	MoveWindow(state->tabs, margin, top, width, bounds.bottom - top - margin - progressHeight, TRUE);
	RECT page;
	GetClientRect(state->tabs, &page);
	TabCtrl_AdjustRect(state->tabs, FALSE, &page);
	MapWindowPoints(state->tabs, window, (POINT *)&page, 2);
	int left = page.left + margin;
	top = page.top + margin;
	width = page.right - left - margin;
	for (unsigned i = 0; i < VENUE_FIELD_COUNT; ++i) {
		int x = left + (int)(i / 6) * (width / 2);
		int y = top + (int)(i % 6) * scaled(state, 42);
		MoveWindow(
			state->venueLabels[i], x, y + scaled(state, 5), scaled(state, 180), scaled(state, 24), TRUE
		);
		MoveWindow(
			state->venueInputs[i],
			x + scaled(state, 184),
			y,
			scaled(state, 210),
			scaled(state, i == VENUE_SURFACE ? 100 : 28),
			TRUE
		);
	}
	static const struct {
		int x, y, width, height;
	} positions[SIM_CONTROL_COUNT] = {
		{0, 4, 180, 24},
		{192, 0, 58, 28},
		{250, 0, 20, 28},
		{0, 46, 180, 24},
		{192, 42, 220, 30},
		{0, 92, 220, 28},
		{0, 128, 220, 28},
		{0, 172, 180, 34},
		{0, 274, 620, 80},
		{0, 222, 620, 44},
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
		int actionWidth = scaled(state, 120);
		int buttonGap = scaled(state, 6);
		int swapWidth = scaled(state, 100);
		// The player buttons, then "Swap player" at the right edge.
		int actionsWidth = PLAYER_BUTTON_COUNT * (actionWidth + buttonGap) + buttonGap + swapWidth;
		MoveWindow(state->swapButtons[i], left + width - swapWidth, y, swapWidth, header, TRUE);
		for (unsigned button = 0; button < PLAYER_BUTTON_COUNT; ++button) {
			MoveWindow(
				state->playerButtons[i][button],
				left + width - actionsWidth + (int)button * (actionWidth + buttonGap) + buttonGap,
				y,
				actionWidth,
				header,
				TRUE
			);
		}
		HDC dc = GetDC(window);
		HGDIOBJ oldFont = SelectObject(dc, state->headingFont);
		SIZE extent = {0};
		GetTextExtentPoint32W(dc, state->headingText[i], (int)wcslen(state->headingText[i]), &extent);
		SelectObject(dc, oldFont);
		int headingWidth = extent.cx + scaled(state, 4);
		if (headingWidth > width - header - actionsWidth) {
			headingWidth = width - header - actionsWidth;
		}
		MoveWindow(
			state->teamControls[i][TEAM_ICON],
			left + headingWidth,
			y + (extent.cy - header) / 2,
			header,
			header,
			TRUE
		);
		if (state->editingTeam[i]) {
			int x = left + headingWidth;
			int gap = scaled(state, 6);
			int inputWidth = scaled(state, 190);
			int buttonWidth = scaled(state, 70);
			placeTeamPicker(state, i, x, y, inputWidth, header);
			x += inputWidth + gap;
			MoveWindow(state->teamControls[i][TEAM_CONFIRM], x, y, buttonWidth, header, TRUE);
			x += buttonWidth + gap;
			MoveWindow(state->teamControls[i][TEAM_CANCEL], x, y, buttonWidth, header, TRUE);
			x += buttonWidth + gap;
			int errorWidth = left + width - actionsWidth - x;
			if (errorWidth < 0) {
				errorWidth = 0;
			}
			int errorHeight = header - scaled(state, 4);
			SelectObject(dc, state->font);
			wchar_t text[2 * FIXTURE_TEXT + 128];
			GetWindowTextW(state->teamControls[i][TEAM_ERROR], text, 2 * FIXTURE_TEXT + 128);
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
			MoveWindow(
				state->teamControls[i][TEAM_ERROR], x, y + scaled(state, 4), errorWidth, errorHeight, TRUE
			);
			ShowScrollBar(state->teamControls[i][TEAM_ERROR], SB_VERT, errorBounds.bottom > errorHeight);
		}
		ReleaseDC(window, dc);
		MoveWindow(state->headings[i], left, y, headingWidth, scaled(state, 27), TRUE);
		// Ellipsis controls can alter their text; restore the full heading after resizing.
		if (!SetWindowTextW(state->headings[i], state->headingText[i])) {
			showError(window, L"Cannot update team headings.");
			state->failed = true;
			DestroyWindow(window);
			return;
		}
		int height = panelHeight - header;
		int pitchWidth = scaled(state, 220);
		MoveWindow(state->pitches[i], left, y + header, pitchWidth, height, TRUE);
		int offset = pitchWidth + margin;
		MoveWindow(state->lists[i], left + offset, y + header, width - offset, height, TRUE);
	}
}

static bool createTeamControls(HWND window, WindowState *state, unsigned side) {
	static const struct {
		const wchar_t *className;
		const wchar_t *text;
		DWORD style;
		DWORD extended;
	} controls[TEAM_CONTROL_COUNT] = {
		{L"BUTTON", L"", WS_TABSTOP | BS_OWNERDRAW, 0},
		{L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL, 0},
		{L"BUTTON", L"Confirm", WS_TABSTOP | BS_PUSHBUTTON, 0},
		{L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, 0},
		{L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 0},
	};
	for (unsigned i = 0; i < TEAM_CONTROL_COUNT; ++i) {
		state->teamControls[side][i] = CreateWindowExW(
			controls[i].extended,
			controls[i].className,
			i == TEAM_ICON ? (side ? L"Replace away team" : L"Replace home team") : controls[i].text,
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
		if (!state->teamControls[side][i]) {
			return false;
		}
	}
	state->swapButtons[side] = CreateWindowExW(
		0,
		L"BUTTON",
		L"Swap player",
		WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
		0,
		0,
		0,
		0,
		window,
		NULL,
		GetModuleHandleW(NULL),
		NULL
	);
	if (!state->swapButtons[side]) {
		return false;
	}
	HWND picker = state->teamControls[side][TEAM_INPUT];
	SendMessageW(picker, CB_LIMITTEXT, 255, 0);
	SendMessageW(picker, CB_SETMINVISIBLE, 15, 0);
	SendMessageW(picker, CB_SETCUEBANNER, 0, (LPARAM)L"Team ID or club short name");
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
		{L"STATIC", L"Seed pattern (optional):", 0, 0},
		{L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE},
		{L"BUTTON", L"Generate detailed reports?", WS_TABSTOP | BS_AUTOCHECKBOX, 0},
		{L"BUTTON", L"Skip rebuild", WS_TABSTOP | BS_AUTOCHECKBOX, 0},
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
	// Skipping the per-seed rebuild plays any fixture, including one the game has moved on from.
	SendMessageW(state->simulation[SIM_KEEP_BUILD], BM_SETCHECK, BST_CHECKED, 0);
	SendMessageW(state->simulation[SIM_SEEDS], EM_SETCUEBANNER, TRUE, (LPARAM)L"e.g. 1,4,7-70");
	SendMessageW(
		state->simulation[SIM_SPINNER], UDM_SETBUDDY, (WPARAM)state->simulation[SIM_THREADS], 0
	);
	SendMessageW(state->simulation[SIM_SPINNER], UDM_SETRANGE32, 1, (LPARAM)state->threadLimit);
	SendMessageW(state->simulation[SIM_SPINNER], UDM_SETPOS32, 0, (LPARAM)state->threadLimit);
	return true;
}

static bool createVenueControls(HWND window, WindowState *state) {
	for (unsigned i = 0; i < VENUE_FIELD_COUNT; ++i) {
		state->venueLabels[i] = CreateWindowExW(
			0, L"STATIC", venueLabels[i], WS_CHILD, 0, 0, 0, 0, window, NULL, GetModuleHandleW(NULL), NULL
		);
		state->venueInputs[i] = CreateWindowExW(
			i == VENUE_SURFACE ? 0 : WS_EX_CLIENTEDGE,
			i == VENUE_SURFACE ? L"COMBOBOX" : L"EDIT",
			L"",
			WS_CHILD | WS_TABSTOP | (i == VENUE_SURFACE ? CBS_DROPDOWNLIST | WS_VSCROLL : ES_AUTOHSCROLL),
			0,
			0,
			0,
			0,
			window,
			NULL,
			GetModuleHandleW(NULL),
			NULL
		);
		if (!state->venueLabels[i] || !state->venueInputs[i]) {
			return false;
		}
		if (i != VENUE_SURFACE) {
			SendMessageW(state->venueInputs[i], EM_SETLIMITTEXT, 63, 0);
			SendMessageW(state->venueInputs[i], EM_SETCUEBANNER, TRUE, (LPARAM)L"Not stored");
		}
	}
	HWND surface = state->venueInputs[VENUE_SURFACE];
	if (SendMessageW(surface, CB_ADDSTRING, 0, (LPARAM)L"Grass") != 0 ||
			SendMessageW(surface, CB_ADDSTRING, 0, (LPARAM)L"Grass/Synthetic mix") != 1) {
		return false;
	}
	SendMessageW(surface, CB_SETITEMDATA, 0, 1);
	SendMessageW(surface, CB_SETITEMDATA, 1, 8);
	return setVenue(state);
}

static bool createControls(HWND window, WindowState *state) {
	HINSTANCE instance = GetModuleHandleW(NULL);
	static const wchar_t *const fileLabels[] = {L"Open...", L"Save"};
	// Create these first so the summary clips around them in the sibling Z order.
	for (unsigned i = 0; i < FILE_CONTROL_COUNT; ++i) {
		state->fileButtons[i] = CreateWindowExW(
			0,
			L"BUTTON",
			fileLabels[i],
			WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
			0,
			0,
			0,
			0,
			window,
			NULL,
			instance,
			NULL
		);
		if (!state->fileButtons[i]) {
			return false;
		}
	}
	state->metadata = CreateWindowExW(
		WS_EX_CLIENTEDGE,
		MSFTEDIT_CLASS,
		L"",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | ES_MULTILINE | ES_READONLY,
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
	tab.pszText = L"Venue";
	if (SendMessageW(state->tabs, TCM_INSERTITEMW, 1, (LPARAM)&tab) == -1) {
		return false;
	}
	tab.pszText = L"Simulation";
	if (SendMessageW(state->tabs, TCM_INSERTITEMW, 2, (LPARAM)&tab) == -1) {
		return false;
	}
	for (unsigned i = 0; i < 2; ++i) {
		static const wchar_t *const playerLabels[] = {L"Max happiness", L"Max condition"};
		for (unsigned button = 0; button < PLAYER_BUTTON_COUNT; ++button) {
			state->playerButtons[i][button] = CreateWindowExW(
				0,
				L"BUTTON",
				playerLabels[button],
				WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON | WS_DISABLED,
				0,
				0,
				0,
				0,
				window,
				NULL,
				instance,
				NULL
			);
			if (!state->playerButtons[i][button]) {
				return false;
			}
		}
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
		if (!state->headings[i] || !state->pitches[i] || !state->lists[i]) {
			return false;
		}
		ListView_SetExtendedListViewStyle(
			state->lists[i],
			LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP | LVS_EX_GRIDLINES
		);
		for (unsigned column = 0; column < sizeof columns / sizeof columns[0]; ++column) {
			LVCOLUMNW definition = {.mask = LVCF_TEXT | LVCF_WIDTH,
				.pszText = (wchar_t *)columns[column],
				.cx = scaled(state, columnWidths[column])};
			if (SendMessageW(state->lists[i], LVM_INSERTCOLUMNW, column, (LPARAM)&definition) == -1) {
				return false;
			}
		}
	}
	bool ok = createTeamControls(window, state, 0) && createTeamControls(window, state, 1) &&
						createVenueControls(window, state) && createSimulationControls(window, state) &&
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

static bool clickFixtureButton(HWND window, WindowState *state, HWND control) {
	if (control == state->fileButtons[FILE_OPEN_BUTTON] ||
			control == state->fileButtons[FILE_SAVE_BUTTON]) {
		chooseFixtureFile(window, state, control == state->fileButtons[FILE_SAVE_BUTTON]);
		return true;
	}
	for (unsigned side = 0; side < 2; ++side) {
		for (unsigned button = 0; button < PLAYER_BUTTON_COUNT; ++button) {
			if (control == state->playerButtons[side][button]) {
				maxPlayers(window, state, side, button);
				return true;
			}
		}
		if (control == state->teamControls[side][TEAM_ICON]) {
			editTeam(window, state, side, true);
			return true;
		}
		if (control == state->teamControls[side][TEAM_CONFIRM]) {
			startReplacement(window, state, side);
			return true;
		}
		if (control == state->teamControls[side][TEAM_CANCEL]) {
			editTeam(window, state, side, false);
			return true;
		}
		if (control == state->swapButtons[side]) {
			startSwap(window, state, side);
			return true;
		}
	}
	if (control == state->simulation[SIM_SUBMIT]) {
		startSimulation(window, state);
		return true;
	}
	return false;
}

// Polls the background operation behind `timer`; returns false when the timer is not ours.
static bool checkTimer(HWND window, WindowState *state, WPARAM timer) {
	switch (timer) {
		case FILE_TIMER: checkFileJob(window, state); return true;
		case REPLACE_TIMER: checkReplacement(window, state); return true;
		case TEAMS_TIMER: checkTeams(window, state); return true;
		case PLAYERS_TIMER: checkPlayers(window, state); return true;
		case SIMULATION_TIMER: checkSimulation(window, state); return true;
		default: return false;
	}
}

// Explains why the window cannot close yet; returns false when nothing is running.
static bool closeBlocked(HWND window, const WindowState *state) {
	if (state->fileThread) {
		showError(window, L"A fixture file operation is running. Please wait before closing.");
		return true;
	}
	if (state->replacementThread) {
		showError(window, L"A team replacement is running. Please wait before closing.");
		return true;
	}
	if (state->simulationThread) {
		showError(window, L"A simulation is running. Please wait for it to finish before closing.");
		return true;
	}
	if ((state->venueDirty || fixtureData_hasPlayerEdits(&state->playerEdits)) &&
			MessageBoxW(
				window,
				L"Fixture changes are not saved. Discard them and close?",
				L"Close fixture",
				MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2
			) != IDYES) {
		return true;
	}
	return false;
}

// Marks the venue edited when `control` is one of its inputs; returns false when it is not.
static bool venueEdited(HWND window, WindowState *state, HWND control) {
	for (unsigned i = 0; i < VENUE_FIELD_COUNT; ++i) {
		if (control != state->venueInputs[i]) {
			continue;
		}
		state->venueDirty = true;
		if (!setMetadata(state)) {
			showError(window, L"Cannot update fixture summary.");
			state->failed = true;
			DestroyWindow(window);
		}
		return true;
	}
	return false;
}

// Handles a WM_COMMAND from one of the window's controls; returns false when it is not ours.
static bool commandMessage(HWND window, WindowState *state, WPARAM wParam, LPARAM lParam) {
	HWND control = (HWND)lParam;
	WORD code = HIWORD(wParam);
	if (!state->fillingVenue && (code == EN_CHANGE || code == CBN_SELCHANGE) &&
			venueEdited(window, state, control)) {
		return true;
	}
	if (code == BN_CLICKED && clickFixtureButton(window, state, control)) {
		return true;
	}
	if (code != CBN_EDITCHANGE) {
		return false;
	}
	unsigned side = pickerSide(state, control);
	if (side >= 2) {
		return false;
	}
	if (!state->filteringTeams) {
		filterTeams(state, side, true);
	}
	return true;
}

static LRESULT
actionMessage(HWND window, WindowState *state, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
		case WM_COMMAND:
			if (commandMessage(window, state, wParam, lParam)) {
				return 0;
			}
			break;
		case WM_TIMER:
			if (checkTimer(window, state, wParam)) {
				return 0;
			}
			break;
		case WM_NOTIFY: {
			const NMHDR *notification = (const NMHDR *)lParam;
			for (unsigned side = 0; side < 2; ++side) {
				if (notification->hwndFrom == state->lists[side] &&
						(notification->code == (UINT)LVN_ITEMCHANGING ||
							notification->code == (UINT)LVN_ITEMCHANGED)) {
					return playerNotification(state, (const NMLISTVIEW *)notification, side);
				}
			}
			if (notification->hwndFrom == state->tabs && notification->code == (UINT)TCN_SELCHANGE) {
				selectTab(state);
				return 0;
			}
			return metadataNotification(window, state, notification);
		}
		case WM_CLOSE:
			if (closeBlocked(window, state)) {
				return 0;
			}
			break;
		default: break;
	}
	return DefWindowProcW(window, message, wParam, lParam);
}

// Draws the owner-drawn edit icon; returns false when the item is not ours.
static bool drawItem(HWND window, WindowState *state, const DRAWITEMSTRUCT *draw) {
	if (draw->hwndItem != state->teamControls[0][TEAM_ICON] &&
			draw->hwndItem != state->teamControls[1][TEAM_ICON]) {
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

// Paints static text and buttons on the window background, with team errors in red.
static LRESULT colorControl(const WindowState *state, HDC dc, HWND control) {
	SetBkMode(dc, TRANSPARENT);
	SetTextColor(
		dc,
		control == state->teamControls[0][TEAM_ERROR] || control == state->teamControls[1][TEAM_ERROR]
			? RGB(255, 0, 0)
			: GetSysColor(COLOR_WINDOWTEXT)
	);
	SetBkColor(dc, GetSysColor(COLOR_WINDOW));
	return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
}

// Shows the hand cursor over an enabled edit icon; returns false for any other control.
static bool setIconCursor(const WindowState *state, HWND control) {
	if ((control != state->teamControls[0][TEAM_ICON] &&
				control != state->teamControls[1][TEAM_ICON]) ||
			!IsWindowEnabled(control)) {
		return false;
	}
	SetCursor(LoadCursorW(NULL, IDC_HAND));
	return true;
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
		case WM_CTLCOLORBTN: return colorControl(state, (HDC)wParam, (HWND)lParam);
		case WM_DRAWITEM:
			if (drawItem(window, state, (const DRAWITEMSTRUCT *)lParam)) {
				return TRUE;
			}
			break;
		case WM_SYSCOLORCHANGE:
			for (unsigned side = 0; side < 2; ++side) {
				if (state->teamControls[side][TEAM_ICON]) {
					InvalidateRect(state->teamControls[side][TEAM_ICON], NULL, TRUE);
				}
			}
			break;
		case WM_SETCURSOR:
			if (setIconCursor(state, (HWND)wParam)) {
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

// Enter on a button presses it, and in a team picker confirms the replacement.
static bool handleReturn(HWND window, WindowState *state, HWND control, unsigned picker) {
	if (control == state->fileButtons[FILE_OPEN_BUTTON] ||
			control == state->fileButtons[FILE_SAVE_BUTTON]) {
		SendMessageW(control, BM_CLICK, 0, 0);
		return true;
	}
	for (unsigned side = 0; side < 2; ++side) {
		if (control == state->teamControls[side][TEAM_ICON] ||
				control == state->teamControls[side][TEAM_CANCEL]) {
			SendMessageW(control, BM_CLICK, 0, 0);
			return true;
		}
		if (picker == side || control == state->teamControls[side][TEAM_CONFIRM]) {
			startReplacement(window, state, side);
			return true;
		}
	}
	if (control == state->simulation[SIM_SUBMIT]) {
		SendMessageW(state->simulation[SIM_SUBMIT], BM_CLICK, 0, 0);
		return true;
	}
	if (control == state->metadata && state->temporary && !state->missing) {
		exportFixture(window, state);
		return true;
	}
	return false;
}

static bool handleKey(HWND window, WindowState *state, const MSG *message) {
	if (message->message != WM_KEYDOWN) {
		return false;
	}
	// Typing goes to a picker's own edit field.
	unsigned side = pickerSide(state, message->hwnd);
	if (side < 2 && (message->wParam == VK_RETURN || message->wParam == VK_ESCAPE) &&
			SendMessageW(state->teamControls[side][TEAM_INPUT], CB_GETDROPPEDSTATE, 0, 0)) {
		return false; // the open list takes Enter (pick) and Escape (close)
	}
	if (message->wParam == VK_ESCAPE && TabCtrl_GetCurSel(state->tabs) == 0) {
		for (unsigned editing = 0; editing < 2; ++editing) {
			if (state->editingTeam[editing]) {
				editTeam(window, state, editing, false);
				return true;
			}
		}
	}
	if (message->wParam == VK_RETURN && handleReturn(window, state, message->hwnd, side)) {
		return true;
	}
	if (message->wParam == VK_TAB) {
		HWND focus = GetFocus();
		unsigned focusSide = pickerSide(state, focus);
		if (focusSide < 2) {
			focus = state->teamControls[focusSide][TEAM_INPUT];
		}
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
	if (activeWindow) {
		// NOLINTNEXTLINE(performance-no-int-to-ptr): GWLP_USERDATA stores the viewer's state pointer.
		WindowState *state = (WindowState *)GetWindowLongPtrW(activeWindow, GWLP_USERDATA);
		return startFileJob(activeWindow, state, path, false, temporary) ? 0 : 1;
	}
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
	WindowState state = {.data = data,
		.path = path,
		.filename = pathFilename(path),
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
		activeWindow = window;
		result = runMessageLoop(window, &state);
		activeWindow = NULL;
	}
	joinThread(state.simulationThread);
	joinThread(state.replacementThread);
	joinThread(state.fileThread);
	joinThread(state.teamsThread);
	joinThread(state.playersThread);
	freeChoices(state.teamsJob.choices, state.teamsJob.count);
	freeChoices(state.teams, state.teamCount);
	freeChoices(state.playersJob.choices, state.playersJob.count);
	freeChoices(state.players, state.playerCount);
	releaseTaskbar(&state);
	if (SUCCEEDED(comResult)) {
		CoUninitialize();
	}
	free(state.replacementJob.data);
	free(state.fileJob.data);
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
