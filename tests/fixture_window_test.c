// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "files.h"
#include "paths.h"
#include "protocol.h"
#include "tests.h"

#include <shobjidl.h>
#include <time.h>
#include <windows.h>


static int errorMessages;
static time_t testTime(time_t *out);
static int testMessageBox(HWND window, const wchar_t *text, const wchar_t *caption, UINT type);
static HRESULT WINAPI testCoCreateInstance(
	REFCLSID classId,
	IUnknown *outer,
	DWORD context,
	REFIID interfaceId,
	void **out
);

#define time testTime
#define MessageBoxW testMessageBox
#define CoCreateInstance testCoCreateInstance
#include "../src/ui/fixture_window.c" // NOLINT(bugprone-suspicious-include): test internal UI callbacks.


#undef time
#undef MessageBoxW
#undef CoCreateInstance

#define CHECK(expression)                                                                          \
	do {                                                                                             \
		if (!(expression)) {                                                                           \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression);                             \
			exit(EXIT_FAILURE); /* NOLINT(concurrency-mt-unsafe): single-threaded test. */               \
		}                                                                                              \
	} while (0)

static time_t testTime(time_t *out) {
	if (out) {
		*out = 12345;
	}
	return 12345;
}

static int testMessageBox(HWND window, const wchar_t *text, const wchar_t *caption, UINT type) {
	(void)window;
	(void)text;
	(void)caption;
	(void)type;
	++errorMessages;
	return IDOK;
}

static const wchar_t *expectedSeeds;
static const wchar_t *expectedThreads;
static wchar_t expectedInput[HARNESS_PATH_LENGTH];
static bool expectedDetailed;
static unsigned simulationCalls;
static int simulationResult;
static HANDLE simulationRelease;
static HWND expectedProgressWindow;
static unsigned long threadLimit = 7;
static HANDLE replacementRelease;
static unsigned replacementCalls;
static int replacementResult;
static bool replacementLoadFailure;
static const wchar_t *expectedTeam;
static wchar_t replacementOutput[HARNESS_PATH_LENGTH];
static unsigned taskbarCreates;
static unsigned taskbarInitializations;
static unsigned taskbarReleases;
static unsigned taskbarStateCalls;
static unsigned taskbarValueCalls;
static TBPFLAG taskbarState;
static ULONGLONG taskbarCompleted;
static ULONGLONG taskbarTotal;
static HRESULT taskbarCreateResult = S_OK;
static HRESULT taskbarInitResult = S_OK;
static HRESULT taskbarStateResult = S_OK;
static HRESULT taskbarValueResult = S_OK;

static HRESULT STDMETHODCALLTYPE testTaskbarInit(ITaskbarList3 *taskbar) {
	CHECK(taskbar != NULL);
	++taskbarInitializations;
	return taskbarInitResult;
}

static ULONG STDMETHODCALLTYPE testTaskbarRelease(ITaskbarList3 *taskbar) {
	CHECK(taskbar != NULL);
	++taskbarReleases;
	return 0;
}

static HRESULT STDMETHODCALLTYPE
testTaskbarState(ITaskbarList3 *taskbar, HWND window, TBPFLAG progress) {
	CHECK(taskbar != NULL && window == expectedProgressWindow);
	++taskbarStateCalls;
	taskbarState = progress;
	return taskbarStateResult;
}

static HRESULT STDMETHODCALLTYPE
testTaskbarValue(ITaskbarList3 *taskbar, HWND window, ULONGLONG completed, ULONGLONG total) {
	CHECK(taskbar != NULL && window == expectedProgressWindow);
	CHECK(taskbarState == TBPF_NORMAL && completed <= total && total > 0);
	++taskbarValueCalls;
	taskbarCompleted = completed;
	taskbarTotal = total;
	return taskbarValueResult;
}

static HRESULT WINAPI testCoCreateInstance(
	REFCLSID classId,
	IUnknown *outer,
	DWORD context,
	REFIID interfaceId,
	void **out
) {
	static ITaskbarList3Vtbl methods = {.Release = testTaskbarRelease,
		.HrInit = testTaskbarInit,
		.SetProgressState = testTaskbarState,
		.SetProgressValue = testTaskbarValue};
	static ITaskbarList3 taskbar = {.lpVtbl = &methods};
	CHECK(IsEqualGUID(classId, &CLSID_TaskbarList));
	CHECK(IsEqualGUID(interfaceId, &IID_ITaskbarList3));
	CHECK(outer == NULL && context == CLSCTX_INPROC_SERVER);
	++taskbarCreates;
	*out = SUCCEEDED(taskbarCreateResult) ? &taskbar : NULL;
	return taskbarCreateResult;
}

unsigned long commands_defaultThreads(void) {
	return threadLimit;
}

int commands_runWithProgress(int argc, wchar_t **argv, uintptr_t progressWindow) {
	CHECK(progressWindow == (uintptr_t)expectedProgressWindow);
	CHECK(argc == (expectedDetailed ? 7 : 6));
	CHECK(wcscmp(argv[1], L"run") == 0);
	CHECK(wcscmp(argv[2], expectedInput) == 0);
	CHECK(wcscmp(argv[3], expectedSeeds) == 0);
	CHECK(wcscmp(argv[4], L"--threads") == 0);
	CHECK(wcscmp(argv[5], expectedThreads) == 0);
	if (expectedDetailed) {
		CHECK(wcscmp(argv[6], L"--detailed") == 0);
	}
	CHECK(argv[argc] == NULL);
	++simulationCalls;
	CHECK(WaitForSingleObject(simulationRelease, 5000) == WAIT_OBJECT_0);
	return simulationResult;
}

int commands_replaceAwayWithResult(int argc, wchar_t **argv, char *result, size_t resultSize) {
	CHECK(argc == 5 && argv[argc] == NULL);
	CHECK(wcscmp(argv[1], L"replace-away") == 0);
	CHECK(wcscmp(argv[2], expectedInput) == 0);
	CHECK(wcscmp(argv[3], expectedTeam) == 0);
	CHECK(files_hasExtension(argv[4], L".json"));
	wcscpy(replacementOutput, argv[4]);
	++replacementCalls;
	CHECK(WaitForSingleObject(replacementRelease, 5000) == WAIT_OBJECT_0);
	(void)snprintf(
		result, resultSize, "%s", replacementResult ? "error: that is the home team" : "ok"
	);
	if (!replacementResult) {
		CHECK(files_write(argv[4], "{\"replacement\":true}", 20));
	}
	return replacementResult;
}

bool fixtureLoad_open(const wchar_t *path, FixtureData *out, char *error, size_t errorSize) {
	CHECK(wcscmp(path, replacementOutput) == 0);
	if (replacementLoadFailure) {
		(void)snprintf(error, errorSize, "cannot read replacement fixture");
		return false;
	}
	memset(out, 0, sizeof *out);
	strcpy(out->teams[0].name, "Aston Villa");
	strcpy(out->teams[1].name, "Luton Town");
	strcpy(out->teams[1].tactic, "New tactic");
	strcpy(out->teams[1].formation, "4-2-3-1");
	out->teams[1].playerCount = (FixtureNumber){.value = 1, .known = true};
	strcpy(out->teams[1].players[0].name, "New away player");
	out->teams[1].slots[0].position = (FixtureNumber){.value = 1, .known = true};
	return true;
}

static void firstLine(HWND metadata, const wchar_t *expected) {
	wchar_t text[8192];
	CHECK(GetWindowTextW(metadata, text, 8192) > 0);
	text[wcscspn(text, L"\r\n")] = 0;
	CHECK(wcscmp(text, expected) == 0);
}

static void checkFormat(HWND metadata, LONG start, LONG end, DWORD effects, COLORREF color) {
	CHARRANGE range = {.cpMin = start, .cpMax = end};
	SendMessageW(metadata, EM_EXSETSEL, 0, (LPARAM)&range);
	CHARFORMAT2W format = {.cbSize = sizeof format};
	SendMessageW(metadata, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&format);
	CHECK(
		(format.dwMask & (CFM_BOLD | CFM_COLOR | CFM_UNDERLINE | CFM_LINK)) ==
		(CFM_BOLD | CFM_COLOR | CFM_UNDERLINE | CFM_LINK)
	);
	CHECK((format.dwEffects & (CFE_BOLD | CFE_UNDERLINE | CFE_LINK)) == effects);
	CHECK(format.crTextColor == color);
}

static void checkContents(const wchar_t *path, const char *expected) {
	ByteBuffer contents = {0};
	CHECK(files_read(path, &contents));
	CHECK(contents.length == strlen(expected));
	CHECK(memcmp(contents.data, expected, contents.length) == 0);
	buffer_free(&contents);
}

static bool controlVisible(HWND control) {
	return (GetWindowLongW(control, GWL_STYLE) & WS_VISIBLE) != 0;
}

static void changeTab(HWND window, WindowState *state, int tab) {
	TabCtrl_SetCurSel(state->tabs, tab);
	NMHDR notification = {.hwndFrom = state->tabs, .code = (UINT)TCN_SELCHANGE};
	CHECK(windowProcedure(window, WM_NOTIFY, 0, (LPARAM)&notification) == 0);
	for (unsigned i = 0; i < 2; ++i) {
		CHECK(controlVisible(state->headings[i]) == (tab == 0));
		CHECK(controlVisible(state->notes[i]) == (tab == 0));
		CHECK(controlVisible(state->pitches[i]) == (tab == 0));
		CHECK(controlVisible(state->lists[i]) == (tab == 0));
	}
	for (unsigned i = 0; i < SIM_CONTROL_COUNT; ++i) {
		CHECK(controlVisible(state->simulation[i]) == (tab == 1));
	}
	for (unsigned i = 0; i < AWAY_CONTROL_COUNT; ++i) {
		CHECK(
			controlVisible(state->away[i]) ==
			(tab == 0 && (i == AWAY_ICON ? !state->editingAway : state->editingAway) &&
				(i != AWAY_ERROR || GetWindowTextLengthW(state->away[i]) > 0))
		);
	}
}

static void clickSimulate(HWND window, WindowState *state) {
	CHECK(
		windowProcedure(
			window, WM_COMMAND, MAKEWPARAM(0, BN_CLICKED), (LPARAM)state->simulation[SIM_SUBMIT]
		) == 0
	);
}

static RECT controlBounds(HWND window, HWND control) {
	RECT bounds;
	CHECK(GetWindowRect(control, &bounds));
	MapWindowPoints(NULL, window, (POINT *)&bounds, 2);
	return bounds;
}

static void checkPageLayout(HWND window, const WindowState *state) {
	RECT client;
	CHECK(GetClientRect(window, &client));
	RECT tabs = controlBounds(window, state->tabs);
	CHECK(tabs.bottom == client.bottom - scaled(state, state->simulationThread ? 24 : 12));
	RECT page;
	CHECK(GetClientRect(state->tabs, &page));
	TabCtrl_AdjustRect(state->tabs, FALSE, &page);
	MapWindowPoints(state->tabs, window, (POINT *)&page, 2);
	HWND contents[] = {state->headings[0],
		state->headings[1],
		state->lists[0],
		state->lists[1],
		state->simulation[SIM_THREADS_LABEL],
		state->simulation[SIM_SEEDS],
		state->simulation[SIM_STATUS]};
	for (unsigned i = 0; i < sizeof contents / sizeof contents[0]; ++i) {
		RECT bounds = controlBounds(window, contents[i]);
		CHECK(bounds.left > page.left && bounds.right < page.right);
		CHECK(bounds.top >= page.top && bounds.bottom < page.bottom);
	}
	RECT seeds = controlBounds(window, state->simulation[SIM_SEEDS]);
	CHECK(seeds.right - seeds.left == scaled(state, 220));
	RECT threads = controlBounds(window, state->simulation[SIM_THREADS_LABEL]);
	CHECK(threads.right - threads.left == scaled(state, 180));
	RECT progress = controlBounds(window, state->progress);
	CHECK(progress.left == 0 && progress.right == client.right);
	CHECK(progress.bottom == client.bottom && progress.bottom - progress.top == scaled(state, 12));
}

static void checkControlColors(HWND window, HWND control, UINT message) {
	HDC dc = GetDC(control);
	CHECK(dc != NULL);
	CHECK(
		windowProcedure(window, message, (WPARAM)dc, (LPARAM)control) ==
		(LRESULT)GetSysColorBrush(COLOR_WINDOW)
	);
	CHECK(GetBkMode(dc) == TRANSPARENT);
	CHECK(GetTextColor(dc) == GetSysColor(COLOR_WINDOWTEXT));
	CHECK(GetBkColor(dc) == GetSysColor(COLOR_WINDOW));
	CHECK(ReleaseDC(control, dc));
}

static void clickAway(HWND window, WindowState *state, unsigned control) {
	CHECK(
		windowProcedure(window, WM_COMMAND, MAKEWPARAM(0, BN_CLICKED), (LPARAM)state->away[control]) ==
		0
	);
}

static void checkAwayLayout(HWND window, WindowState *state) {
	RECT heading = controlBounds(window, state->headings[1]);
	RECT icon = controlBounds(window, state->away[AWAY_ICON]);
	CHECK(icon.left == heading.right && icon.top == heading.top);
	if (!state->editingAway) {
		return;
	}
	RECT input = controlBounds(window, state->away[AWAY_TEAM]);
	RECT confirm = controlBounds(window, state->away[AWAY_CONFIRM]);
	RECT cancel = controlBounds(window, state->away[AWAY_CANCEL]);
	RECT error = controlBounds(window, state->away[AWAY_ERROR]);
	RECT tabs = controlBounds(window, state->tabs);
	CHECK(input.left == heading.right && input.top == heading.top);
	CHECK(confirm.left > input.right && cancel.left > confirm.right);
	CHECK(error.left > cancel.right && error.right < tabs.right);
	CHECK(error.top < cancel.bottom);
}

static void checkAwayIcon(HWND window, WindowState *state) {
	HDC dc = GetDC(window);
	HDC memory = CreateCompatibleDC(dc);
	HBITMAP bitmap = CreateCompatibleBitmap(dc, 27, 27);
	CHECK(memory && bitmap);
	HGDIOBJ old = SelectObject(memory, bitmap);
	DRAWITEMSTRUCT draw = {
		.hwndItem = state->away[AWAY_ICON], .hDC = memory, .rcItem = {0, 0, 27, 27}};
	CHECK(windowProcedure(window, WM_DRAWITEM, 0, (LPARAM)&draw) == TRUE);
	unsigned blue = 0;
	for (int y = 0; y < 27; ++y) {
		for (int x = 0; x < 27; ++x) {
			blue += GetPixel(memory, x, y) == RGB(0, 0, 255);
		}
	}
	CHECK(blue > 50);
	CHECK(GetPixel(memory, 0, 0) == GetSysColor(COLOR_WINDOW));
	SelectObject(memory, old);
	CHECK(DeleteObject(bitmap));
	CHECK(DeleteDC(memory));
	CHECK(ReleaseDC(window, dc));
}

static void checkAwayEditor(HWND window, WindowState *state) {
	wchar_t heading[2048];
	wchar_t text[2048];
	changeTab(window, state, 0);
	layout(window, state);
	checkAwayLayout(window, state);
	checkAwayIcon(window, state);
	CHECK(GetWindowTextW(state->headings[1], heading, 2048) > 0);
	CHECK(wcsstr(heading, L"Bristol City") != NULL);
	CHECK(files_write(state->savedFilename, "original saved fixture", 22));
	wchar_t saved[FIXTURE_EXPORT_FILENAME_LENGTH];
	wcscpy(saved, state->savedFilename);
	const wchar_t *originalPath = state->path;
	clickAway(window, state, AWAY_ICON);
	CHECK(state->editingAway);
	CHECK(GetWindowTextW(state->headings[1], text, 2048) == 6);
	CHECK(wcscmp(text, L"Away: ") == 0);
	CHECK(GetWindowTextW(state->away[AWAY_TEAM], text, 2048) > 0);
	CHECK(wcscmp(text, L"Bristol City") == 0);
	CHECK(SendMessageW(state->away[AWAY_TEAM], EM_GETLIMITTEXT, 0, 0) == 255);
	checkAwayLayout(window, state);
	state->dpi = 144;
	CHECK(SetWindowPos(window, NULL, 0, 0, 1350, 900, SWP_NOMOVE | SWP_NOZORDER));
	CHECK(setFonts(state));
	layout(window, state);
	checkAwayLayout(window, state);
	state->dpi = 96;
	CHECK(SetWindowPos(window, NULL, 0, 0, 900, 600, SWP_NOMOVE | SWP_NOZORDER));
	CHECK(setFonts(state));
	layout(window, state);
	CHECK(SetWindowTextW(state->away[AWAY_TEAM], L"Not submitted"));
	MSG key = {.hwnd = state->away[AWAY_TEAM], .message = WM_KEYUP, .wParam = VK_ESCAPE};
	CHECK(!handleKey(window, state, &key) && state->editingAway);
	key.message = WM_KEYDOWN;
	CHECK(handleKey(window, state, &key));
	CHECK(!state->editingAway && replacementCalls == 0);
	CHECK(GetWindowTextW(state->headings[1], text, 2048) > 0 && wcscmp(text, heading) == 0);
	CHECK(wcscmp(state->savedFilename, saved) == 0 && state->path == originalPath);
	clickAway(window, state, AWAY_ICON);
	key.wParam = VK_RETURN;
	static const wchar_t *const invalid[] = {L"", L" \t", L"1\nteam=2"};
	for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		CHECK(SetWindowTextW(state->away[AWAY_TEAM], invalid[i]));
		CHECK(handleKey(window, state, &key));
		CHECK(!state->replacementThread && replacementCalls == 0 && state->editingAway);
		CHECK(GetWindowTextLengthW(state->away[AWAY_ERROR]) > 0);
	}
	CHECK(paths_absolute(saved, expectedInput, HARNESS_PATH_LENGTH));
	replacementRelease = CreateEventW(NULL, TRUE, FALSE, NULL);
	CHECK(replacementRelease != NULL);
	expectedTeam = L"Aston Villa";
	CHECK(SetWindowTextW(state->away[AWAY_TEAM], expectedTeam));
	replacementResult = 1;
	int errors = errorMessages;
	unsigned simulations = simulationCalls;
	clickAway(window, state, AWAY_CONFIRM);
	CHECK(state->replacementThread != NULL);
	HANDLE worker = state->replacementThread;
	CHECK(!IsWindowEnabled(state->away[AWAY_CONFIRM]));
	CHECK(!IsWindowEnabled(state->away[AWAY_CANCEL]));
	CHECK(!IsWindowEnabled(state->simulation[SIM_SUBMIT]));
	clickAway(window, state, AWAY_CONFIRM);
	clickAway(window, state, AWAY_CANCEL);
	clickSimulate(window, state);
	CHECK(state->replacementThread == worker && state->editingAway);
	CHECK(simulationCalls == simulations);
	CHECK(windowProcedure(window, WM_CLOSE, 0, 0) == 0);
	CHECK(IsWindow(window) && errorMessages == ++errors);
	CHECK(SetEvent(replacementRelease));
	CHECK(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0);
	CHECK(windowProcedure(window, WM_TIMER, REPLACE_TIMER, 0) == 0);
	CHECK(replacementCalls == 1 && !state->replacementThread && state->editingAway);
	CHECK(GetWindowTextW(state->away[AWAY_ERROR], text, 2048) > 0);
	CHECK(wcscmp(text, L"error: that is the home team") == 0);
	CHECK(strcmp(state->data->teams[1].name, "Bristol City") == 0);
	CHECK(wcscmp(state->savedFilename, saved) == 0 && state->path == originalPath);
	CHECK(fileMissing(replacementOutput));
	HDC dc = GetDC(state->away[AWAY_ERROR]);
	CHECK(
		windowProcedure(window, WM_CTLCOLORSTATIC, (WPARAM)dc, (LPARAM)state->away[AWAY_ERROR]) ==
		(LRESULT)GetSysColorBrush(COLOR_WINDOW)
	);
	CHECK(GetTextColor(dc) == RGB(255, 0, 0));
	CHECK(ReleaseDC(state->away[AWAY_ERROR], dc));
	CHECK(IsWindowEnabled(state->away[AWAY_CONFIRM]) && IsWindowEnabled(state->away[AWAY_CANCEL]));
	checkAwayLayout(window, state);
	changeTab(window, state, 1);
	changeTab(window, state, 0);

	replacementResult = 0;
	replacementLoadFailure = true;
	expectedTeam = L"Luton Town";
	CHECK(SetWindowTextW(state->away[AWAY_TEAM], expectedTeam));
	clickAway(window, state, AWAY_CONFIRM);
	CHECK(state->replacementThread != NULL);
	CHECK(WaitForSingleObject(state->replacementThread, 5000) == WAIT_OBJECT_0);
	checkReplacement(window, state);
	CHECK(state->editingAway && strcmp(state->data->teams[1].name, "Bristol City") == 0);
	CHECK(GetWindowTextW(state->away[AWAY_ERROR], text, 2048) > 0);
	CHECK(wcscmp(text, L"cannot read replacement fixture") == 0);
	CHECK(fileMissing(replacementOutput));
	replacementLoadFailure = false;
	for (unsigned i = 0; i < 2; ++i) {
		expectedTeam = i ? L"677" : L"Luton Town";
		CHECK(SetWindowTextW(state->away[AWAY_TEAM], expectedTeam));
		if (i) {
			CHECK(paths_absolute(state->path, expectedInput, HARNESS_PATH_LENGTH));
		}
		wchar_t previous[HARNESS_PATH_LENGTH];
		wcscpy(previous, state->replacementPath);
		clickAway(window, state, AWAY_CONFIRM);
		CHECK(state->replacementThread != NULL);
		changeTab(window, state, 1);
		CHECK(WaitForSingleObject(state->replacementThread, 5000) == WAIT_OBJECT_0);
		checkReplacement(window, state);
		CHECK(!state->replacementThread && !state->editingAway && state->temporary);
		CHECK(!state->savedFilename[0] && !state->missing);
		CHECK(wcscmp(state->path, replacementOutput) == 0);
		if (previous[0]) {
			CHECK(fileMissing(previous));
		}
		checkContents(state->path, "{\"replacement\":true}");
		checkContents(saved, "original saved fixture");
		firstLine(state->metadata, L"This fixture object is not saved. Click here to export it");
		changeTab(window, state, 0);
		CHECK(GetWindowTextW(state->headings[1], text, 2048) > 0);
		CHECK(wcscmp(text, L"Away: Luton Town - New tactic [4-2-3-1]") == 0);
		CHECK(ListView_GetItemCount(state->lists[1]) == FIXTURE_SLOTS + 1);
		CHECK(GetWindowLongPtrW(state->pitches[1], GWLP_USERDATA) == (LONG_PTR)&state->data->teams[1]);
		CHECK(IsWindowEnabled(state->simulation[SIM_SUBMIT]));
		clickAway(window, state, AWAY_ICON);
		clickAway(window, state, AWAY_CANCEL);
		CHECK(GetWindowTextW(state->headings[1], text, 2048) > 0);
		CHECK(wcscmp(text, L"Away: Luton Town - New tactic [4-2-3-1]") == 0);
		if (!i) {
			clickAway(window, state, AWAY_ICON);
		}
	}
	CHECK(replacementCalls == 4);
	for (unsigned exported = 0; exported < 2; ++exported) {
		if (exported) {
			exportFixture(window, state);
			CHECK(!state->temporary);
			CHECK(wcscmp(state->savedFilename, L"aston-villa-v-luton-town-12345.json") == 0);
			checkContents(state->savedFilename, "{\"replacement\":true}");
		}
		CHECK(paths_absolute(
			exported ? state->savedFilename : state->path, expectedInput, HARNESS_PATH_LENGTH
		));
		simulationResult = 0;
		clickSimulate(window, state);
		CHECK(state->simulationThread != NULL);
		CHECK(!IsWindowEnabled(state->away[AWAY_ICON]));
		clickAway(window, state, AWAY_ICON);
		CHECK(!state->editingAway);
		CHECK(WaitForSingleObject(state->simulationThread, 5000) == WAIT_OBJECT_0);
		checkSimulation(window, state);
		CHECK(IsWindowEnabled(state->away[AWAY_ICON]));
	}
	CHECK(errorMessages == errors);
	CHECK(CloseHandle(replacementRelease));
	CHECK(DeleteFileW(saved));
	CHECK(DeleteFileW(state->savedFilename));
	CHECK(DeleteFileW(state->replacementPath));
	state->replacementPath[0] = 0;
}

static void checkSimulationForm(FixtureData *data, const wchar_t *path) {
	INITCOMMONCONTROLSEX controls = {.dwSize = sizeof controls,
		.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_UPDOWN_CLASS | ICC_PROGRESS_CLASS};
	CHECK(InitCommonControlsEx(&controls));
	HINSTANCE instance = GetModuleHandleW(NULL);
	WNDCLASSW definition = {.lpfnWndProc = pitchProcedure,
		.hInstance = instance,
		.cbWndExtra = sizeof(HFONT),
		.lpszClassName = pitchClass};
	CHECK(RegisterClassW(&definition));
	HWND window =
		CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 900, 600, NULL, NULL, instance, NULL);
	CHECK(window != NULL);
	WindowState state = {.data = data, .path = path, .filename = L"temp-fixture.json", .dpi = 96};
	SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)&state);
	CHECK(createControls(window, &state));
	state.taskbarCreated = RegisterWindowMessageW(L"TaskbarButtonCreated");
	CHECK(state.taskbarCreated != 0 && state.taskbar == NULL && taskbarCreates == 0);
	CHECK(TabCtrl_GetItemCount(state.tabs) == 2);
	wchar_t text[256];
	TCITEMW tab = {.mask = TCIF_TEXT, .pszText = text, .cchTextMax = 256};
	CHECK(SendMessageW(state.tabs, TCM_GETITEMW, 0, (LPARAM)&tab));
	CHECK(wcscmp(text, L"Squads") == 0);
	CHECK(SendMessageW(state.tabs, TCM_GETITEMW, 1, (LPARAM)&tab));
	CHECK(wcscmp(text, L"Simulation") == 0);
	int minimum;
	int maximum;
	SendMessageW(state.simulation[SIM_SPINNER], UDM_GETRANGE32, (WPARAM)&minimum, (LPARAM)&maximum);
	CHECK(minimum == 1 && maximum == 7);
	CHECK(GetWindowTextW(state.simulation[SIM_THREADS], text, 256) == 1);
	CHECK(wcscmp(text, L"7") == 0);
	CHECK(GetWindowTextW(state.simulation[SIM_SEEDS], text, 256) == 4);
	CHECK(wcscmp(text, L"keep") == 0);
	CHECK(SendMessageW(state.simulation[SIM_DETAILED], BM_GETCHECK, 0, 0) == BST_UNCHECKED);
	CHECK(SendMessageW(state.simulation[SIM_SEEDS], EM_GETLIMITTEXT, 0, 0) == 255);
	CHECK(!controlVisible(state.progress));
	CHECK(GetWindowLongW(state.tabs, GWL_STYLE) & WS_CLIPSIBLINGS);
	checkControlColors(window, state.headings[0], WM_CTLCOLORSTATIC);
	checkControlColors(window, state.notes[0], WM_CTLCOLORSTATIC);
	checkControlColors(window, state.simulation[SIM_THREADS_LABEL], WM_CTLCOLORSTATIC);
	checkControlColors(window, state.simulation[SIM_DETAILED], WM_CTLCOLORBTN);
	changeTab(window, &state, 0);
	changeTab(window, &state, 1);
	layout(window, &state);
	checkPageLayout(window, &state);
	state.dpi = 144;
	CHECK(SetWindowPos(window, NULL, 0, 0, 1350, 900, SWP_NOMOVE | SWP_NOZORDER));
	CHECK(setFonts(&state));
	layout(window, &state);
	checkPageLayout(window, &state);
	state.dpi = 96;
	CHECK(SetWindowPos(window, NULL, 0, 0, 900, 600, SWP_NOMOVE | SWP_NOZORDER));
	CHECK(setFonts(&state));
	layout(window, &state);

	int errors = errorMessages;
	static const wchar_t *const invalidThreads[] = {L"", L"0", L"8", L"-1", L"abc", L"4294967296"};
	for (unsigned i = 0; i < sizeof invalidThreads / sizeof invalidThreads[0]; ++i) {
		CHECK(SetWindowTextW(state.simulation[SIM_THREADS], invalidThreads[i]));
		clickSimulate(window, &state);
		CHECK(state.simulationThread == NULL && errorMessages == ++errors);
	}
	CHECK(SetWindowTextW(state.simulation[SIM_THREADS], L"2"));
	static const wchar_t *const invalidSeeds[] = {L"", L"abc", L"1\nthreads=7", L"1;2"};
	for (unsigned i = 0; i < sizeof invalidSeeds / sizeof invalidSeeds[0]; ++i) {
		CHECK(SetWindowTextW(state.simulation[SIM_SEEDS], invalidSeeds[i]));
		clickSimulate(window, &state);
		CHECK(state.simulationThread == NULL && errorMessages == ++errors);
	}
	CHECK(simulationCalls == 0);
	simulationRelease = CreateEventW(NULL, TRUE, FALSE, NULL);
	CHECK(simulationRelease != NULL);
	expectedProgressWindow = window;
	static const wchar_t *const patterns[] = {L"1", L"42", L"1-1000", L"1,3,5", L"1,4,7-70", L"keep"};
	for (unsigned i = 0; i < sizeof patterns / sizeof patterns[0]; ++i) {
		if (i == 4) {
			wcscpy(state.savedFilename, L"saved fixture.json");
		}
		CHECK(paths_absolute(
			state.savedFilename[0] ? state.savedFilename : path, expectedInput, HARNESS_PATH_LENGTH
		));
		expectedSeeds = patterns[i];
		expectedThreads = i % 2 ? L"7" : L"1";
		expectedDetailed = i % 2 != 0;
		simulationResult = i == 5 ? 1 : 0;
		CHECK(SetWindowTextW(state.simulation[SIM_SEEDS], expectedSeeds));
		CHECK(SetWindowTextW(state.simulation[SIM_THREADS], expectedThreads));
		SendMessageW(
			state.simulation[SIM_DETAILED], BM_SETCHECK, expectedDetailed ? BST_CHECKED : BST_UNCHECKED, 0
		);
		CHECK(ResetEvent(simulationRelease));
		clickSimulate(window, &state);
		CHECK(state.simulationThread != NULL);
		if (i == 0) {
			CHECK(taskbarStateCalls == 0 && state.taskbar == NULL);
			CHECK(windowProcedure(window, state.taskbarCreated, 0, 0) == 0);
		}
		CHECK(state.taskbar != NULL && taskbarState == TBPF_INDETERMINATE);
		HANDLE worker = state.simulationThread;
		CHECK(controlVisible(state.progress));
		CHECK(SendMessageW(state.progress, PBM_GETPOS, 0, 0) == 0);
		checkPageLayout(window, &state);
		CHECK(windowProcedure(window, HARNESS_PROGRESS_MESSAGE, 0, 1000) == 0);
		CHECK(SendMessageW(state.progress, PBM_GETRANGE, FALSE, 0) == 1000);
		CHECK(taskbarState == TBPF_NORMAL && taskbarCompleted == 0 && taskbarTotal == 1000);
		CHECK(windowProcedure(window, HARNESS_PROGRESS_MESSAGE, 250, 1000) == 0);
		CHECK(SendMessageW(state.progress, PBM_GETPOS, 0, 0) == 250);
		CHECK(taskbarState == TBPF_NORMAL && taskbarCompleted == 250 && taskbarTotal == 1000);
		unsigned stateCalls = taskbarStateCalls;
		unsigned valueCalls = taskbarValueCalls;
		CHECK(windowProcedure(window, HARNESS_PROGRESS_MESSAGE, 1001, 1000) == 0);
		CHECK(windowProcedure(window, HARNESS_PROGRESS_MESSAGE, 0, 0) == 0);
		CHECK(windowProcedure(window, HARNESS_PROGRESS_MESSAGE, 0, -1) == 0);
		CHECK(windowProcedure(window, HARNESS_PROGRESS_MESSAGE, 0, (LPARAM)INT_MAX + 1) == 0);
		CHECK(SendMessageW(state.progress, PBM_GETPOS, 0, 0) == 250);
		CHECK(taskbarStateCalls == stateCalls && taskbarValueCalls == valueCalls);
		unsigned creates = taskbarCreates;
		unsigned releases = taskbarReleases;
		CHECK(windowProcedure(window, state.taskbarCreated, 0, 0) == 0);
		CHECK(taskbarCreates == creates + 1 && taskbarReleases == releases + 1);
		CHECK(taskbarInitializations == taskbarCreates);
		CHECK(taskbarState == TBPF_NORMAL && taskbarCompleted == 250 && taskbarTotal == 1000);
		if (i == 0) {
			taskbarValueResult = E_FAIL;
			CHECK(windowProcedure(window, HARNESS_PROGRESS_MESSAGE, 500, 1000) == 0);
			CHECK(state.taskbar == NULL && state.simulationThread == worker);
			CHECK(SendMessageW(state.progress, PBM_GETPOS, 0, 0) == 500);
			taskbarValueResult = S_OK;
			CHECK(windowProcedure(window, state.taskbarCreated, 0, 0) == 0);
			CHECK(taskbarCompleted == 500 && taskbarTotal == 1000);
		}
		CHECK(!IsWindowEnabled(state.simulation[SIM_SUBMIT]));
		CHECK(WaitForSingleObject(worker, 0) == WAIT_TIMEOUT);
		clickSimulate(window, &state);
		CHECK(state.simulationThread == worker);
		CHECK(windowProcedure(window, WM_CLOSE, 0, 0) == 0);
		CHECK(IsWindow(window) && errorMessages == ++errors);
		changeTab(window, &state, 0);
		CHECK(controlVisible(state.progress));
		changeTab(window, &state, 1);
		CHECK(controlVisible(state.progress));
		CHECK(SetWindowPos(window, NULL, 0, 0, 1000, 700, SWP_NOMOVE | SWP_NOZORDER));
		layout(window, &state);
		checkPageLayout(window, &state);
		CHECK(windowProcedure(window, HARNESS_PROGRESS_MESSAGE, 1000, 1000) == 0);
		CHECK(SendMessageW(state.progress, PBM_GETPOS, 0, 0) == 1000);
		CHECK(taskbarCompleted == 1000 && taskbarTotal == 1000);
		CHECK(SetEvent(simulationRelease));
		CHECK(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0);
		CHECK(windowProcedure(window, WM_TIMER, SIMULATION_TIMER, 0) == 0);
		CHECK(state.simulationThread == NULL);
		CHECK(taskbarState == TBPF_NOPROGRESS);
		CHECK(!controlVisible(state.progress));
		checkPageLayout(window, &state);
		stateCalls = taskbarStateCalls;
		valueCalls = taskbarValueCalls;
		CHECK(windowProcedure(window, HARNESS_PROGRESS_MESSAGE, 1, 10) == 0);
		CHECK(SendMessageW(state.progress, PBM_GETPOS, 0, 0) == 1000);
		CHECK(taskbarStateCalls == stateCalls && taskbarValueCalls == valueCalls);
		CHECK(IsWindowEnabled(state.simulation[SIM_SUBMIT]));
		CHECK(simulationCalls == i + 1);
		CHECK(GetWindowTextW(state.simulation[SIM_STATUS], text, 256) > 0);
		CHECK(wcsstr(text, simulationResult ? L"failed" : L"complete") != NULL);
		if (simulationResult) {
			++errors;
		}
		CHECK(errorMessages == errors);
		CHECK(GetWindowTextW(state.simulation[SIM_SEEDS], text, 256) > 0);
		CHECK(wcscmp(text, expectedSeeds) == 0);
	}
	taskbarCreateResult = E_FAIL;
	CHECK(windowProcedure(window, state.taskbarCreated, 0, 0) == 0);
	CHECK(state.taskbar == NULL);
	taskbarCreateResult = S_OK;
	taskbarInitResult = E_FAIL;
	CHECK(windowProcedure(window, state.taskbarCreated, 0, 0) == 0);
	CHECK(state.taskbar == NULL);
	taskbarInitResult = S_OK;
	taskbarStateResult = E_FAIL;
	CHECK(windowProcedure(window, state.taskbarCreated, 0, 0) == 0);
	CHECK(state.taskbar == NULL);
	taskbarStateResult = S_OK;
	checkAwayEditor(window, &state);
	CHECK(windowProcedure(window, state.taskbarCreated, 0, 0) == 0);
	CHECK(state.taskbar != NULL && taskbarState == TBPF_NOPROGRESS);
	releaseTaskbar(&state);
	CHECK(state.taskbar == NULL && taskbarReleases == taskbarInitializations);
	CHECK(CloseHandle(simulationRelease));
	CHECK(DestroyWindow(window));
	CHECK(DeleteObject(state.font));
	CHECK(DeleteObject(state.headingFont));
	CHECK(UnregisterClassW(pitchClass, instance));

	static const unsigned long limits[] = {1, HARNESS_MAX_THREADS};
	for (unsigned i = 0; i < sizeof limits / sizeof limits[0]; ++i) {
		threadLimit = limits[i];
		window =
			CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 900, 600, NULL, NULL, instance, NULL);
		CHECK(window != NULL);
		state = (WindowState){0};
		CHECK(createSimulationControls(window, &state));
		SendMessageW(state.simulation[SIM_SPINNER], UDM_GETRANGE32, (WPARAM)&minimum, (LPARAM)&maximum);
		CHECK(minimum == 1 && maximum == (int)threadLimit);
		CHECK(SendMessageW(state.simulation[SIM_SPINNER], UDM_GETPOS32, 0, 0) == (LRESULT)threadLimit);
		CHECK(DestroyWindow(window));
	}
}

int fixture_window_test(int argc, char **argv) {
	(void)argc;
	(void)argv;
	wchar_t original[HARNESS_PATH_LENGTH];
	wchar_t temporary[HARNESS_PATH_LENGTH];
	wchar_t directory[HARNESS_PATH_LENGTH];
	wchar_t outputDirectory[HARNESS_PATH_LENGTH];
	wchar_t source[HARNESS_PATH_LENGTH];
	CHECK(GetCurrentDirectoryW(HARNESS_PATH_LENGTH, original) > 0);
	CHECK(GetTempPathW(HARNESS_PATH_LENGTH, temporary) > 0);
	CHECK(GetTempFileNameW(temporary, L"fme", 0, directory));
	CHECK(DeleteFileW(directory));
	CHECK(CreateDirectoryW(directory, NULL));
	CHECK(paths_join(outputDirectory, HARNESS_PATH_LENGTH, directory, L"exports"));
	CHECK(CreateDirectoryW(outputDirectory, NULL));
	CHECK(paths_join(source, HARNESS_PATH_LENGTH, directory, L"temp-fixture.json"));
	CHECK(files_write(source, "{\"fixture\":true}", 16));
	CHECK(SetCurrentDirectoryW(outputDirectory));

	HMODULE richEdit = LoadLibraryW(L"Msftedit.dll");
	CHECK(richEdit != NULL);
	HWND window =
		CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 800, 400, NULL, NULL, NULL, NULL);
	CHECK(window != NULL);
	FixtureData *data = calloc(1, sizeof *data);
	CHECK(data != NULL);
	(void)snprintf(data->teams[0].name, FIXTURE_TEXT, "Aston Villa");
	(void)snprintf(data->teams[1].name, FIXTURE_TEXT, "Bristol City");
	WindowState state = {.data = data, .path = source, .filename = L"temp-fixture.json", .dpi = 96};
	state.metadata = CreateWindowExW(
		0,
		MSFTEDIT_CLASS,
		L"",
		WS_CHILD | ES_MULTILINE | ES_READONLY,
		0,
		0,
		800,
		400,
		window,
		NULL,
		NULL,
		NULL
	);
	CHECK(state.metadata != NULL);
	SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)&state);
	CHECK(setMetadata(&state));
	firstLine(state.metadata, L"Viewing fixture: temp-fixture.json");
	checkFormat(state.metadata, 17, 34, CFE_BOLD, GetSysColor(COLOR_WINDOWTEXT));

	state.temporary = true;
	CHECK(setMetadata(&state));
	firstLine(state.metadata, L"This fixture object is not saved. Click here to export it");
	LONG linkStart = (LONG)wcslen(unsavedPrefix);
	LONG linkEnd = linkStart + (LONG)wcslen(exportLink);
	checkFormat(state.metadata, 0, linkStart, CFE_BOLD, RGB(255, 0, 0));
	checkFormat(state.metadata, linkStart, linkEnd, CFE_LINK | CFE_UNDERLINE, RGB(0, 0, 255));
	checkFormat(state.metadata, linkEnd, linkEnd + 13, 0, GetSysColor(COLOR_WINDOWTEXT));

	ENLINK link = {.nmhdr = {.hwndFrom = state.metadata, .code = EN_LINK}, .msg = WM_LBUTTONDOWN};
	CHECK(windowProcedure(window, WM_NOTIFY, 0, (LPARAM)&link) == 0);
	CHECK(state.temporary);
	link.msg = WM_LBUTTONUP;
	CHECK(windowProcedure(window, WM_NOTIFY, 0, (LPARAM)&link) == 1);
	CHECK(!state.temporary && !state.missing && errorMessages == 0);
	CHECK(wcscmp(state.savedFilename, L"aston-villa-v-bristol-city-12345.json") == 0);
	firstLine(state.metadata, L"Viewing fixture: aston-villa-v-bristol-city-12345.json");
	checkContents(state.savedFilename, "{\"fixture\":true}");
	checkContents(source, "{\"fixture\":true}");

	CHECK(files_write(state.savedFilename, "existing", 8));
	state.temporary = true;
	CHECK(setMetadata(&state));
	CHECK(windowProcedure(window, WM_NOTIFY, 0, (LPARAM)&link) == 1);
	CHECK(state.temporary && !state.missing && errorMessages == 1);
	firstLine(state.metadata, L"This fixture object is not saved. Click here to export it");
	checkContents(state.savedFilename, "existing");
	CHECK(DeleteFileW(state.savedFilename));

	CHECK(DeleteFileW(source));
	CHECK(windowProcedure(window, WM_NOTIFY, 0, (LPARAM)&link) == 1);
	CHECK(state.temporary && state.missing && errorMessages == 1);
	firstLine(state.metadata, L"Fixture cannot be found; restart the app");
	checkFormat(state.metadata, 0, (LONG)wcslen(missingFixture), CFE_BOLD, RGB(255, 0, 0));
	CHECK(setMetadata(&state));
	firstLine(state.metadata, L"Fixture cannot be found; restart the app");
	CHECK(windowProcedure(window, WM_NOTIFY, 0, (LPARAM)&link) == 1);
	CHECK(errorMessages == 1);

	checkSimulationForm(data, source);
	CHECK(DestroyWindow(window));
	CHECK(FreeLibrary(richEdit));
	free(data);
	CHECK(SetCurrentDirectoryW(original));
	CHECK(RemoveDirectoryW(outputDirectory));
	CHECK(RemoveDirectoryW(directory));
	return 0;
}
