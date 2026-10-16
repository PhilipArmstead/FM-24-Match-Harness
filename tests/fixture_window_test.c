// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../ui/constants.h"
#include "files.h"
#include "paths.h"
#include "protocol.h"
#include "test.h"
#include "tests.h"

#include <shobjidl.h>
#include <time.h>
#include <windows.h>


static int errorMessages;
static bool calibriInstalled;
static time_t testTime(time_t *out);
static int testMessageBox(HWND window, const wchar_t *text, const wchar_t *caption, UINT type);
static int WINAPI
testEnumFontFamiliesExW(HDC dc, LOGFONTW *font, FONTENUMPROCW callback, LPARAM data, DWORD flags);
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
#define EnumFontFamiliesExW testEnumFontFamiliesExW
#include "../src/ui/fixture_window.c" // NOLINT(bugprone-suspicious-include): test internal UI callbacks.


#undef time
#undef MessageBoxW
#undef CoCreateInstance
#undef EnumFontFamiliesExW

static int WINAPI
testEnumFontFamiliesExW(HDC dc, LOGFONTW *font, FONTENUMPROCW callback, LPARAM data, DWORD flags) {
	ASSERT_BOOL_EQ(dc != NULL && flags == 0, true);
	ASSERT_INT_EQ(font->lfCharSet, DEFAULT_CHARSET);
	ASSERT_INT_EQ(wcscmp(font->lfFaceName, L"Calibri"), 0);
	if (!calibriInstalled) {
		return 1;
	}
	TEXTMETRICW metrics = {0};
	int result = callback(font, &metrics, TRUETYPE_FONTTYPE, data);
	ASSERT_INT_EQ(result, 0);
	return result;
}

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
	ASSERT_BOOL_EQ(taskbar != NULL, true);
	++taskbarInitializations;
	return taskbarInitResult;
}

static ULONG STDMETHODCALLTYPE testTaskbarRelease(ITaskbarList3 *taskbar) {
	ASSERT_BOOL_EQ(taskbar != NULL, true);
	++taskbarReleases;
	return 0;
}

static HRESULT STDMETHODCALLTYPE
testTaskbarState(ITaskbarList3 *taskbar, HWND window, TBPFLAG progress) {
	ASSERT_BOOL_EQ(taskbar != NULL && window == expectedProgressWindow, true);
	++taskbarStateCalls;
	taskbarState = progress;
	return taskbarStateResult;
}

static HRESULT STDMETHODCALLTYPE
testTaskbarValue(ITaskbarList3 *taskbar, HWND window, ULONGLONG completed, ULONGLONG total) {
	ASSERT_BOOL_EQ(taskbar != NULL && window == expectedProgressWindow, true);
	ASSERT_BOOL_EQ(taskbarState == TBPF_NORMAL && completed <= total && total > 0, true);
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
	ASSERT_BOOL_EQ(IsEqualGUID(classId, &CLSID_TaskbarList), true);
	ASSERT_BOOL_EQ(IsEqualGUID(interfaceId, &IID_ITaskbarList3), true);
	ASSERT_BOOL_EQ(outer == NULL && context == CLSCTX_INPROC_SERVER, true);
	++taskbarCreates;
	*out = SUCCEEDED(taskbarCreateResult) ? &taskbar : NULL;
	return taskbarCreateResult;
}

unsigned long commands_defaultThreads(void) {
	return threadLimit;
}

int commands_runWithProgress(int argc, wchar_t **argv, uintptr_t progressWindow) {
	ASSERT_BOOL_EQ(progressWindow == (uintptr_t)expectedProgressWindow, true);
	ASSERT_BOOL_EQ(argc == (expectedDetailed ? 7 : 6), true);
	ASSERT_INT_EQ(wcscmp(argv[1], L"run"), 0);
	ASSERT_INT_EQ(wcscmp(argv[2], expectedInput), 0);
	ASSERT_INT_EQ(wcscmp(argv[3], expectedSeeds), 0);
	ASSERT_INT_EQ(wcscmp(argv[4], L"--threads"), 0);
	ASSERT_INT_EQ(wcscmp(argv[5], expectedThreads), 0);
	if (expectedDetailed) {
		ASSERT_INT_EQ(wcscmp(argv[6], L"--detailed"), 0);
	}
	ASSERT_BOOL_EQ(argv[argc] == NULL, true);
	++simulationCalls;
	ASSERT_BOOL_EQ(WaitForSingleObject(simulationRelease, 5000) == WAIT_OBJECT_0, true);
	return simulationResult;
}

int commands_replaceAwayWithResult(int argc, wchar_t **argv, char *result, size_t resultSize) {
	ASSERT_BOOL_EQ(argc == 5 && argv[argc] == NULL, true);
	ASSERT_INT_EQ(wcscmp(argv[1], L"replace-away"), 0);
	ASSERT_INT_EQ(wcscmp(argv[2], expectedInput), 0);
	ASSERT_INT_EQ(wcscmp(argv[3], expectedTeam), 0);
	ASSERT_BOOL_EQ(files_hasExtension(argv[4], L".json"), true);
	wcscpy(replacementOutput, argv[4]);
	++replacementCalls;
	ASSERT_BOOL_EQ(WaitForSingleObject(replacementRelease, 5000) == WAIT_OBJECT_0, true);
	(void)snprintf(
		result, resultSize, "%s", replacementResult ? "error: that is the home team" : "ok"
	);
	if (!replacementResult) {
		ASSERT_BOOL_EQ(files_write(argv[4], "{\"replacement\":true}", 20), true);
	}
	return replacementResult;
}

bool fixtureLoad_open(const wchar_t *path, FixtureData *out, char *error, size_t errorSize) {
	ASSERT_INT_EQ(wcscmp(path, replacementOutput), 0);
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
	wchar_t text[8192] = {0};
	ASSERT_BOOL_EQ(GetWindowTextW(metadata, text, 8192) > 0, true);
	text[wcscspn(text, L"\r\n")] = 0;
	ASSERT_INT_EQ(wcscmp(text, expected), 0);
}

static void checkFormat(HWND metadata, LONG start, LONG end, DWORD effects, COLORREF color) {
	CHARRANGE range = {.cpMin = start, .cpMax = end};
	SendMessageW(metadata, EM_EXSETSEL, 0, (LPARAM)&range);
	CHARFORMAT2W format = {.cbSize = sizeof format};
	SendMessageW(metadata, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&format);
	ASSERT_BOOL_EQ(
		(format.dwMask & (CFM_BOLD | CFM_COLOR | CFM_UNDERLINE | CFM_LINK)) ==
			(CFM_BOLD | CFM_COLOR | CFM_UNDERLINE | CFM_LINK),
		true
	);
	ASSERT_BOOL_EQ((format.dwEffects & (CFE_BOLD | CFE_UNDERLINE | CFE_LINK)) == effects, true);
	ASSERT_BOOL_EQ(format.crTextColor == color, true);
}

static void checkContents(const wchar_t *path, const char *expected) {
	ByteBuffer contents = {0};
	ASSERT_BOOL_EQ(files_read(path, &contents), true);
	ASSERT_BOOL_EQ(contents.length == strlen(expected), true);
	if (contents.length && contents.length == strlen(expected)) {
		ASSERT_INT_EQ(memcmp(contents.data, expected, contents.length), 0);
	}
	buffer_free(&contents);
}

static bool controlVisible(HWND control) {
	return (GetWindowLongW(control, GWL_STYLE) & WS_VISIBLE) != 0;
}

static void changeTab(HWND window, WindowState *state, int tab) {
	TabCtrl_SetCurSel(state->tabs, tab);
	NMHDR notification = {.hwndFrom = state->tabs, .code = (UINT)TCN_SELCHANGE};
	ASSERT_BOOL_EQ(windowProcedure(window, WM_NOTIFY, 0, (LPARAM)&notification) == 0, true);
	for (unsigned i = 0; i < 2; ++i) {
		ASSERT_BOOL_EQ(controlVisible(state->headings[i]), tab == 0);
		ASSERT_BOOL_EQ(controlVisible(state->notes[i]), tab == 0);
		ASSERT_BOOL_EQ(controlVisible(state->pitches[i]), tab == 0);
		ASSERT_BOOL_EQ(controlVisible(state->lists[i]), tab == 0);
	}
	for (unsigned i = 0; i < SIM_CONTROL_COUNT; ++i) {
		ASSERT_BOOL_EQ(controlVisible(state->simulation[i]), tab == 1);
	}
	for (unsigned i = 0; i < AWAY_CONTROL_COUNT; ++i) {
		ASSERT_BOOL_EQ(
			controlVisible(state->away[i]),
			tab == 0 && (i == AWAY_ICON ? !state->editingAway : state->editingAway) &&
				(i != AWAY_ERROR || GetWindowTextLengthW(state->away[i]) > 0)
		);
	}
}

static void clickSimulate(HWND window, WindowState *state) {
	ASSERT_BOOL_EQ(
		windowProcedure(
			window, WM_COMMAND, MAKEWPARAM(0, BN_CLICKED), (LPARAM)state->simulation[SIM_SUBMIT]
		) == 0,
		true
	);
}

static void simulationProgress(HWND window, UINT completed, UINT total, UINT homeWins, UINT draws) {
	ASSERT_BOOL_EQ(
		windowProcedure(
			window,
			HARNESS_PROGRESS_MESSAGE,
			(WPARAM)((uint64_t)homeWins << 32 | completed),
			(LPARAM)((uint64_t)draws << 32 | total)
		) == 0,
		true
	);
}

static void checkSimulationSummary(const WindowState *state, const wchar_t *expected) {
	wchar_t text[256] = {0};
	ASSERT_BOOL_EQ(GetWindowTextW(state->simulation[SIM_SUMMARY], text, 256) > 0, true);
	ASSERT_INT_EQ(wcscmp(text, expected), 0);
}

static RECT controlBounds(HWND window, HWND control) {
	RECT bounds = {0};
	ASSERT_BOOL_EQ(GetWindowRect(control, &bounds), true);
	MapWindowPoints(NULL, window, (POINT *)&bounds, 2);
	return bounds;
}

static void checkPageLayout(HWND window, const WindowState *state) {
	RECT client = {0};
	ASSERT_BOOL_EQ(GetClientRect(window, &client), true);
	RECT tabs = controlBounds(window, state->tabs);
	ASSERT_BOOL_EQ(
		tabs.bottom == client.bottom - scaled(state, state->simulationThread ? 24 : 12), true
	);
	RECT page = {0};
	ASSERT_BOOL_EQ(GetClientRect(state->tabs, &page), true);
	TabCtrl_AdjustRect(state->tabs, FALSE, &page);
	MapWindowPoints(state->tabs, window, (POINT *)&page, 2);
	HWND contents[] = {state->headings[0],
		state->headings[1],
		state->lists[0],
		state->lists[1],
		state->simulation[SIM_THREADS_LABEL],
		state->simulation[SIM_SEEDS],
		state->simulation[SIM_SUMMARY],
		state->simulation[SIM_STATUS]};
	for (unsigned i = 0; i < sizeof contents / sizeof contents[0]; ++i) {
		RECT bounds = controlBounds(window, contents[i]);
		ASSERT_BOOL_EQ(bounds.left > page.left && bounds.right < page.right, true);
		ASSERT_BOOL_EQ(bounds.top >= page.top && bounds.bottom < page.bottom, true);
	}
	RECT seeds = controlBounds(window, state->simulation[SIM_SEEDS]);
	ASSERT_BOOL_EQ(seeds.right - seeds.left == scaled(state, 220), true);
	RECT threads = controlBounds(window, state->simulation[SIM_THREADS_LABEL]);
	ASSERT_BOOL_EQ(threads.right - threads.left == scaled(state, 180), true);
	RECT submit = controlBounds(window, state->simulation[SIM_SUBMIT]);
	RECT summary = controlBounds(window, state->simulation[SIM_SUMMARY]);
	RECT status = controlBounds(window, state->simulation[SIM_STATUS]);
	ASSERT_BOOL_EQ(summary.top > submit.bottom && status.top > summary.bottom, true);
	RECT progress = controlBounds(window, state->progress);
	ASSERT_BOOL_EQ(progress.left == 0 && progress.right == client.right, true);
	ASSERT_BOOL_EQ(
		progress.bottom == client.bottom && progress.bottom - progress.top == scaled(state, 12), true
	);
}

static void checkControlColors(HWND window, HWND control, UINT message) {
	HDC dc = GetDC(control);
	ASSERT_BOOL_EQ(dc != NULL, true);
	ASSERT_BOOL_EQ(
		windowProcedure(window, message, (WPARAM)dc, (LPARAM)control) ==
			(LRESULT)GetSysColorBrush(COLOR_WINDOW),
		true
	);
	ASSERT_BOOL_EQ(GetBkMode(dc) == TRANSPARENT, true);
	ASSERT_BOOL_EQ(GetTextColor(dc) == GetSysColor(COLOR_WINDOWTEXT), true);
	ASSERT_BOOL_EQ(GetBkColor(dc) == GetSysColor(COLOR_WINDOW), true);
	ASSERT_BOOL_EQ(ReleaseDC(control, dc), true);
}

static void clickAway(HWND window, WindowState *state, unsigned control) {
	ASSERT_BOOL_EQ(
		windowProcedure(window, WM_COMMAND, MAKEWPARAM(0, BN_CLICKED), (LPARAM)state->away[control]) ==
			0,
		true
	);
}

static void checkAwayLayout(HWND window, WindowState *state) {
	RECT heading = controlBounds(window, state->headings[1]);
	RECT icon = controlBounds(window, state->away[AWAY_ICON]);
	ASSERT_BOOL_EQ(icon.left == heading.right && icon.top == heading.top, true);
	if (!state->editingAway) {
		return;
	}
	RECT input = controlBounds(window, state->away[AWAY_TEAM]);
	RECT confirm = controlBounds(window, state->away[AWAY_CONFIRM]);
	RECT cancel = controlBounds(window, state->away[AWAY_CANCEL]);
	RECT error = controlBounds(window, state->away[AWAY_ERROR]);
	RECT tabs = controlBounds(window, state->tabs);
	ASSERT_BOOL_EQ(input.left == heading.right && input.top == heading.top, true);
	ASSERT_BOOL_EQ(confirm.left > input.right && cancel.left > confirm.right, true);
	ASSERT_BOOL_EQ(error.left > cancel.right && error.right < tabs.right, true);
	ASSERT_BOOL_EQ(error.top < cancel.bottom, true);
}

static void checkAwayHeading(HWND window, WindowState *state, const wchar_t *expected) {
	HDC dc = GetDC(window);
	ASSERT_BOOL_EQ(dc != NULL, true);
	HGDIOBJ old = SelectObject(dc, state->headingFont);
	SIZE extent = {0};
	ASSERT_BOOL_EQ(GetTextExtentPoint32W(dc, expected, (int)wcslen(expected), &extent), true);
	SelectObject(dc, old);
	ASSERT_BOOL_EQ(ReleaseDC(window, dc), true);
	RECT heading = controlBounds(window, state->headings[1]);
	RECT note = controlBounds(window, state->notes[1]);
	int width = extent.cx + scaled(state, 4);
	int maximum = note.right - note.left - scaled(state, 27);
	ASSERT_INT_EQ(heading.right - heading.left, width < maximum ? width : maximum);
	if (width <= maximum) {
		wchar_t text[2 * FIXTURE_TEXT + 128] = {0};
		ASSERT_BOOL_EQ(GetWindowTextW(state->headings[1], text, 2 * FIXTURE_TEXT + 128) > 0, true);
		ASSERT_INT_EQ(wcscmp(text, expected), 0);
	}
	checkAwayLayout(window, state);
}

static void checkHeadingResize(HWND window, WindowState *state) {
	char tactic[FIXTURE_TEXT];
	strcpy(tactic, state->data->teams[1].tactic);
	memset(state->data->teams[1].tactic, 'W', 120);
	state->data->teams[1].tactic[120] = 0;
	ASSERT_BOOL_EQ(setHeadings(state), true);
	wchar_t expected[2 * FIXTURE_TEXT + 128];
	wchar_t wideTactic[FIXTURE_TEXT];
	ASSERT_BOOL_EQ(widen(state->data->teams[1].tactic, wideTactic, FIXTURE_TEXT), true);
	ASSERT_BOOL_EQ(
		swprintf(
			expected, 2 * FIXTURE_TEXT + 128, L"Away: Bristol City - %ls [Formation unknown]", wideTactic
		) > 0,
		true
	);
	static const UINT resolutions[] = {96, 144};
	for (unsigned i = 0; i < sizeof resolutions / sizeof resolutions[0]; ++i) {
		state->dpi = resolutions[i];
		ASSERT_BOOL_EQ(setFonts(state), true);
		ASSERT_BOOL_EQ(SetWindowPos(window, NULL, 0, 0, 900, 900, SWP_NOMOVE | SWP_NOZORDER), true);
		layout(window, state);
		checkAwayHeading(window, state, expected);
		// Simulate text altered by the ellipsis control before expanding it.
		ASSERT_BOOL_EQ(SetWindowTextW(state->headings[1], L"Away..."), true);
		ASSERT_BOOL_EQ(SetWindowPos(window, NULL, 0, 0, 4000, 900, SWP_NOMOVE | SWP_NOZORDER), true);
		layout(window, state);
		checkAwayHeading(window, state, expected);
	}
	strcpy(state->data->teams[1].tactic, tactic);
	state->dpi = 96;
	ASSERT_BOOL_EQ(setFonts(state), true);
	ASSERT_BOOL_EQ(setHeadings(state), true);
	ASSERT_BOOL_EQ(SetWindowPos(window, NULL, 0, 0, 900, 600, SWP_NOMOVE | SWP_NOZORDER), true);
	layout(window, state);
}

static void checkAwayIcon(HWND window, WindowState *state) {
	HDC dc = GetDC(window);
	ASSERT_BOOL_EQ(dc != NULL, true);
	if (!dc) {
		return;
	}
	HDC memory = CreateCompatibleDC(dc);
	HBITMAP bitmap = CreateCompatibleBitmap(dc, 54, 54);
	ASSERT_BOOL_EQ(memory && bitmap, true);
	if (!memory || !bitmap) {
		if (memory) {
			DeleteDC(memory);
		}
		if (bitmap) {
			DeleteObject(bitmap);
		}
		ReleaseDC(window, dc);
		return;
	}
	HGDIOBJ old = SelectObject(memory, bitmap);
	ASSERT_BOOL_EQ(old != NULL, true);
	UINT originalDpi = state->dpi;
	static const UINT resolutions[] = {96, 120, 144, 192};
	for (unsigned i = 0; old && i < sizeof resolutions / sizeof resolutions[0]; ++i) {
		state->dpi = resolutions[i];
		int side = scaled(state, 27);
		DRAWITEMSTRUCT draw = {
			.hwndItem = state->away[AWAY_ICON], .hDC = memory, .rcItem = {0, 0, side, side}};
		ASSERT_BOOL_EQ(windowProcedure(window, WM_DRAWITEM, 0, (LPARAM)&draw) == TRUE, true);
		COLORREF normal[54 * 54];
		unsigned blue = 0;
		unsigned blended = 0;
		COLORREF background = GetSysColor(COLOR_WINDOW);
		for (int y = 0; y < side; ++y) {
			for (int x = 0; x < side; ++x) {
				COLORREF pixel = GetPixel(memory, x, y);
				normal[y * side + x] = pixel;
				blue += pixel == RGB_BLUE;
				blended += pixel != RGB_BLUE && pixel != background && pixel != CLR_INVALID;
			}
		}
		ASSERT_BOOL_EQ(blue > 50 && blended > 20, true);
		ASSERT_BOOL_EQ(GetPixel(memory, 0, 0) == background, true);
		ASSERT_INT_EQ(state->editIconSize, side - 2 * scaled(state, 3));
		ASSERT_BOOL_EQ(state->editIconColor == RGB_BLUE, true);
		ASSERT_BOOL_EQ(state->editIconBackground == background, true);
		BITMAP image = {0};
		ASSERT_INT_EQ(GetObjectW(state->editIcon, sizeof image, &image), sizeof image);
		ASSERT_BOOL_EQ(
			image.bmWidth == state->editIconSize && image.bmHeight == state->editIconSize, true
		);
		HBITMAP cached = state->editIcon;
		ASSERT_BOOL_EQ(drawEditIcon(state, &draw), true);
		ASSERT_BOOL_EQ(state->editIcon == cached, true);

		draw.itemState = ODS_SELECTED;
		ASSERT_BOOL_EQ(drawEditIcon(state, &draw), true);
		ASSERT_BOOL_EQ(state->editIcon == cached, true);
		for (int y = 1; y < side; ++y) {
			for (int x = 1; x < side; ++x) {
				ASSERT_BOOL_EQ(GetPixel(memory, x, y) == normal[(y - 1) * side + x - 1], true);
			}
		}
		draw.itemState = ODS_FOCUS;
		ASSERT_BOOL_EQ(drawEditIcon(state, &draw), true);
		ASSERT_BOOL_EQ(state->editIcon == cached, true);
		unsigned focus = 0;
		for (int x = 1; x < side - 1; ++x) {
			focus += GetPixel(memory, x, 1) != normal[side + x];
		}
		ASSERT_BOOL_EQ(focus > 0, true);

		draw.itemState = ODS_DISABLED;
		ASSERT_BOOL_EQ(drawEditIcon(state, &draw), true);
		ASSERT_BOOL_EQ(state->editIcon != cached, true);
		ASSERT_BOOL_EQ(state->editIconColor == GetSysColor(COLOR_GRAYTEXT), true);
		unsigned gray = 0;
		blue = 0;
		for (int y = 0; y < side; ++y) {
			for (int x = 0; x < side; ++x) {
				COLORREF pixel = GetPixel(memory, x, y);
				gray += pixel == GetSysColor(COLOR_GRAYTEXT);
				blue += pixel == RGB_BLUE;
			}
		}
		ASSERT_BOOL_EQ(gray > 50 && blue == 0, true);
		cached = state->editIcon;
		draw.itemState = 0;
		ASSERT_BOOL_EQ(drawEditIcon(state, &draw), true);
		ASSERT_BOOL_EQ(state->editIcon != cached && state->editIconColor == RGB_BLUE, true);

		cached = state->editIcon;
		state->editIconBackground ^= 1;
		ASSERT_BOOL_EQ(drawEditIcon(state, &draw), true);
		ASSERT_BOOL_EQ(state->editIcon != cached && state->editIconBackground == background, true);
		draw.rcItem.bottom = 0;
		ASSERT_BOOL_EQ(drawEditIcon(state, &draw), false);
		ASSERT_BOOL_EQ(state->editIcon != NULL, true);
	}
	state->dpi = originalDpi;
	if (old) {
		SelectObject(memory, old);
	}
	ASSERT_BOOL_EQ(DeleteObject(bitmap), true);
	ASSERT_BOOL_EQ(DeleteDC(memory), true);
	ASSERT_BOOL_EQ(ReleaseDC(window, dc), true);
}

static void checkAwayEditor(HWND window, WindowState *state) {
	wchar_t heading[2048] = {0};
	wchar_t text[2048] = {0};
	changeTab(window, state, 0);
	checkHeadingResize(window, state);
	layout(window, state);
	checkAwayLayout(window, state);
	checkAwayIcon(window, state);
	ASSERT_BOOL_EQ(GetWindowTextW(state->headings[1], heading, 2048) > 0, true);
	ASSERT_BOOL_EQ(wcsstr(heading, L"Bristol City") != NULL, true);
	checkAwayHeading(window, state, heading);
	ASSERT_BOOL_EQ(files_write(state->savedFilename, "original saved fixture", 22), true);
	wchar_t saved[FIXTURE_EXPORT_FILENAME_LENGTH];
	wcscpy(saved, state->savedFilename);
	const wchar_t *originalPath = state->path;
	clickAway(window, state, AWAY_ICON);
	ASSERT_BOOL_EQ(state->editingAway, true);
	ASSERT_BOOL_EQ(GetWindowTextW(state->headings[1], text, 2048) == 6, true);
	ASSERT_INT_EQ(wcscmp(text, L"Away: "), 0);
	checkAwayHeading(window, state, L"Away: ");
	ASSERT_BOOL_EQ(GetWindowTextW(state->away[AWAY_TEAM], text, 2048) > 0, true);
	ASSERT_INT_EQ(wcscmp(text, L"Bristol City"), 0);
	ASSERT_BOOL_EQ(SendMessageW(state->away[AWAY_TEAM], EM_GETLIMITTEXT, 0, 0) == 255, true);
	checkAwayLayout(window, state);
	state->dpi = 144;
	ASSERT_BOOL_EQ(SetWindowPos(window, NULL, 0, 0, 1350, 900, SWP_NOMOVE | SWP_NOZORDER), true);
	ASSERT_BOOL_EQ(setFonts(state), true);
	layout(window, state);
	checkAwayLayout(window, state);
	checkAwayHeading(window, state, L"Away: ");
	state->dpi = 96;
	ASSERT_BOOL_EQ(SetWindowPos(window, NULL, 0, 0, 900, 600, SWP_NOMOVE | SWP_NOZORDER), true);
	ASSERT_BOOL_EQ(setFonts(state), true);
	layout(window, state);
	ASSERT_BOOL_EQ(SetWindowTextW(state->away[AWAY_TEAM], L"Not submitted"), true);
	MSG key = {.hwnd = state->away[AWAY_TEAM], .message = WM_KEYUP, .wParam = VK_ESCAPE};
	ASSERT_BOOL_EQ(!handleKey(window, state, &key) && state->editingAway, true);
	key.message = WM_KEYDOWN;
	ASSERT_BOOL_EQ(handleKey(window, state, &key), true);
	ASSERT_BOOL_EQ(!state->editingAway && replacementCalls == 0, true);
	ASSERT_BOOL_EQ(
		GetWindowTextW(state->headings[1], text, 2048) > 0 && wcscmp(text, heading) == 0, true
	);
	checkAwayHeading(window, state, heading);
	ASSERT_INT_EQ(wcscmp(state->savedFilename, saved) == 0 && state->path == originalPath, true);
	clickAway(window, state, AWAY_ICON);
	key.wParam = VK_RETURN;
	static const wchar_t *const invalid[] = {L"", L" \t", L"1\nteam=2"};
	for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		ASSERT_BOOL_EQ(SetWindowTextW(state->away[AWAY_TEAM], invalid[i]), true);
		ASSERT_BOOL_EQ(handleKey(window, state, &key), true);
		ASSERT_BOOL_EQ(!state->replacementThread && replacementCalls == 0 && state->editingAway, true);
		ASSERT_BOOL_EQ(GetWindowTextLengthW(state->away[AWAY_ERROR]) > 0, true);
	}
	ASSERT_BOOL_EQ(paths_absolute(saved, expectedInput, HARNESS_PATH_LENGTH), true);
	replacementRelease = CreateEventW(NULL, TRUE, FALSE, NULL);
	ASSERT_BOOL_EQ(replacementRelease != NULL, true);
	expectedTeam = L"Aston Villa";
	ASSERT_BOOL_EQ(SetWindowTextW(state->away[AWAY_TEAM], expectedTeam), true);
	replacementResult = 1;
	int errors = errorMessages;
	unsigned simulations = simulationCalls;
	clickAway(window, state, AWAY_CONFIRM);
	ASSERT_BOOL_EQ(state->replacementThread != NULL, true);
	HANDLE worker = state->replacementThread;
	ASSERT_BOOL_EQ(!IsWindowEnabled(state->away[AWAY_CONFIRM]), true);
	ASSERT_BOOL_EQ(!IsWindowEnabled(state->away[AWAY_CANCEL]), true);
	ASSERT_BOOL_EQ(!IsWindowEnabled(state->simulation[SIM_SUBMIT]), true);
	clickAway(window, state, AWAY_CONFIRM);
	clickAway(window, state, AWAY_CANCEL);
	clickSimulate(window, state);
	ASSERT_BOOL_EQ(state->replacementThread == worker && state->editingAway, true);
	ASSERT_BOOL_EQ(simulationCalls == simulations, true);
	ASSERT_BOOL_EQ(windowProcedure(window, WM_CLOSE, 0, 0), 0);
	ASSERT_BOOL_EQ(IsWindow(window) && errorMessages == ++errors, true);
	ASSERT_BOOL_EQ(SetEvent(replacementRelease), true);
	ASSERT_BOOL_EQ(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0, true);
	ASSERT_BOOL_EQ(windowProcedure(window, WM_TIMER, REPLACE_TIMER, 0) == 0, true);
	ASSERT_BOOL_EQ(replacementCalls == 1 && !state->replacementThread && state->editingAway, true);
	ASSERT_BOOL_EQ(GetWindowTextW(state->away[AWAY_ERROR], text, 2048) > 0, true);
	ASSERT_INT_EQ(wcscmp(text, L"error: that is the home team"), 0);
	ASSERT_INT_EQ(strcmp(state->data->teams[1].name, "Bristol City"), 0);
	ASSERT_INT_EQ(wcscmp(state->savedFilename, saved) == 0 && state->path == originalPath, true);
	ASSERT_BOOL_EQ(fileMissing(replacementOutput), true);
	HDC dc = GetDC(state->away[AWAY_ERROR]);
	ASSERT_BOOL_EQ(
		windowProcedure(window, WM_CTLCOLORSTATIC, (WPARAM)dc, (LPARAM)state->away[AWAY_ERROR]) ==
			(LRESULT)GetSysColorBrush(COLOR_WINDOW),
		true
	);
	ASSERT_BOOL_EQ(GetTextColor(dc) == RGB(255, 0, 0), true);
	ASSERT_BOOL_EQ(ReleaseDC(state->away[AWAY_ERROR], dc), true);
	ASSERT_BOOL_EQ(
		IsWindowEnabled(state->away[AWAY_CONFIRM]) && IsWindowEnabled(state->away[AWAY_CANCEL]), true
	);
	checkAwayLayout(window, state);
	changeTab(window, state, 1);
	changeTab(window, state, 0);

	replacementResult = 0;
	replacementLoadFailure = true;
	expectedTeam = L"Luton Town";
	ASSERT_BOOL_EQ(SetWindowTextW(state->away[AWAY_TEAM], expectedTeam), true);
	clickAway(window, state, AWAY_CONFIRM);
	ASSERT_BOOL_EQ(state->replacementThread != NULL, true);
	ASSERT_BOOL_EQ(WaitForSingleObject(state->replacementThread, 5000) == WAIT_OBJECT_0, true);
	checkReplacement(window, state);
	ASSERT_BOOL_EQ(state->editingAway && strcmp(state->data->teams[1].name, "Bristol City"), 0);
	ASSERT_BOOL_EQ(GetWindowTextW(state->away[AWAY_ERROR], text, 2048) > 0, true);
	ASSERT_INT_EQ(wcscmp(text, L"cannot read replacement fixture"), 0);
	ASSERT_BOOL_EQ(fileMissing(replacementOutput), true);
	replacementLoadFailure = false;
	for (unsigned i = 0; i < 2; ++i) {
		expectedTeam = i ? L"677" : L"Luton Town";
		ASSERT_BOOL_EQ(SetWindowTextW(state->away[AWAY_TEAM], expectedTeam), true);
		if (i) {
			ASSERT_BOOL_EQ(paths_absolute(state->path, expectedInput, HARNESS_PATH_LENGTH), true);
		}
		wchar_t previous[HARNESS_PATH_LENGTH];
		wcscpy(previous, state->replacementPath);
		clickAway(window, state, AWAY_CONFIRM);
		ASSERT_BOOL_EQ(state->replacementThread != NULL, true);
		if (i) {
			changeTab(window, state, 1);
		}
		ASSERT_BOOL_EQ(WaitForSingleObject(state->replacementThread, 5000) == WAIT_OBJECT_0, true);
		checkReplacement(window, state);
		ASSERT_BOOL_EQ(!state->replacementThread && !state->editingAway && state->temporary, true);
		ASSERT_BOOL_EQ(!state->savedFilename[0] && !state->missing, true);
		ASSERT_INT_EQ(wcscmp(state->path, replacementOutput), 0);
		if (previous[0]) {
			ASSERT_BOOL_EQ(fileMissing(previous), true);
		}
		checkContents(state->path, "{\"replacement\":true}");
		checkContents(saved, "original saved fixture");
		firstLine(state->metadata, L"This fixture object is not saved. Click here to export it");
		changeTab(window, state, 0);
		ASSERT_BOOL_EQ(GetWindowTextW(state->headings[1], text, 2048) > 0, true);
		ASSERT_INT_EQ(wcscmp(text, L"Away: Luton Town - New tactic [4-2-3-1]"), 0);
		checkAwayHeading(window, state, L"Away: Luton Town - New tactic [4-2-3-1]");
		ASSERT_BOOL_EQ(ListView_GetItemCount(state->lists[1]) == FIXTURE_SLOTS + 1, true);
		ASSERT_BOOL_EQ(
			GetWindowLongPtrW(state->pitches[1], GWLP_USERDATA) == (LONG_PTR)&state->data->teams[1], true
		);
		ASSERT_BOOL_EQ(IsWindowEnabled(state->simulation[SIM_SUBMIT]), true);
		clickAway(window, state, AWAY_ICON);
		clickAway(window, state, AWAY_CANCEL);
		ASSERT_BOOL_EQ(GetWindowTextW(state->headings[1], text, 2048) > 0, true);
		ASSERT_INT_EQ(wcscmp(text, L"Away: Luton Town - New tactic [4-2-3-1]"), 0);
		checkAwayHeading(window, state, L"Away: Luton Town - New tactic [4-2-3-1]");
		if (!i) {
			clickAway(window, state, AWAY_ICON);
		}
	}
	ASSERT_BOOL_EQ(replacementCalls == 4, true);
	for (unsigned exported = 0; exported < 2; ++exported) {
		if (exported) {
			exportFixture(window, state);
			ASSERT_BOOL_EQ(!state->temporary, true);
			ASSERT_INT_EQ(wcscmp(state->savedFilename, L"aston-villa-v-luton-town-12345.json"), 0);
			checkContents(state->savedFilename, "{\"replacement\":true}");
		}
		ASSERT_BOOL_EQ(
			paths_absolute(
				exported ? state->savedFilename : state->path, expectedInput, HARNESS_PATH_LENGTH
			),
			true
		);
		simulationResult = 0;
		clickSimulate(window, state);
		ASSERT_BOOL_EQ(state->simulationThread != NULL, true);
		ASSERT_BOOL_EQ(!IsWindowEnabled(state->away[AWAY_ICON]), true);
		clickAway(window, state, AWAY_ICON);
		ASSERT_BOOL_EQ(!state->editingAway, true);
		ASSERT_BOOL_EQ(WaitForSingleObject(state->simulationThread, 5000) == WAIT_OBJECT_0, true);
		checkSimulation(window, state);
		ASSERT_BOOL_EQ(IsWindowEnabled(state->away[AWAY_ICON]), true);
	}
	ASSERT_BOOL_EQ(errorMessages == errors, true);
	ASSERT_BOOL_EQ(CloseHandle(replacementRelease), true);
	ASSERT_BOOL_EQ(DeleteFileW(saved), true);
	ASSERT_BOOL_EQ(DeleteFileW(state->savedFilename), true);
	ASSERT_BOOL_EQ(DeleteFileW(state->replacementPath), true);
	state->replacementPath[0] = 0;
}

static void checkFonts(WindowState *state) {
	static const UINT resolutions[] = {96, 144};
	for (unsigned available = 0; available < 2; ++available) {
		calibriInstalled = available != 0;
		for (unsigned i = 0; i < sizeof resolutions / sizeof resolutions[0]; ++i) {
			state->dpi = resolutions[i];
			ASSERT_BOOL_EQ(setFonts(state), true);
			LOGFONTW heading = {0};
			LOGFONTW body = {0};
			ASSERT_INT_EQ(GetObjectW(state->headingFont, sizeof heading, &heading), sizeof heading);
			ASSERT_INT_EQ(GetObjectW(state->font, sizeof body, &body), sizeof body);
			ASSERT_INT_EQ(wcscmp(heading.lfFaceName, calibriInstalled ? L"Calibri" : L"Segoe UI"), 0);
			ASSERT_INT_EQ(wcscmp(body.lfFaceName, L"Segoe UI"), 0);
			ASSERT_INT_EQ(heading.lfHeight, -MulDiv(13, (int)state->dpi, 72));
			ASSERT_INT_EQ(heading.lfWeight, FW_SEMIBOLD);
			ASSERT_INT_EQ(body.lfHeight, -MulDiv(10, (int)state->dpi, 72));
			for (unsigned side = 0; side < 2; ++side) {
				ASSERT_BOOL_EQ(
					(HFONT)SendMessageW(state->headings[side], WM_GETFONT, 0, 0) == state->headingFont, true
				);
			}
		}
	}
	calibriInstalled = false;
	state->dpi = 96;
	ASSERT_BOOL_EQ(setFonts(state), true);
}

static void checkSimulationForm(FixtureData *data, const wchar_t *path) {
	INITCOMMONCONTROLSEX controls = {.dwSize = sizeof controls,
		.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_UPDOWN_CLASS | ICC_PROGRESS_CLASS};
	ASSERT_BOOL_EQ(InitCommonControlsEx(&controls), true);
	HINSTANCE instance = GetModuleHandleW(NULL);
	WNDCLASSW definition = {.lpfnWndProc = pitchProcedure,
		.hInstance = instance,
		.cbWndExtra = sizeof(HFONT),
		.lpszClassName = pitchClass};
	ASSERT_BOOL_EQ(RegisterClassW(&definition), true);
	HWND window =
		CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 900, 600, NULL, NULL, instance, NULL);
	ASSERT_BOOL_EQ(window != NULL, true);
	WindowState state = {.data = data, .path = path, .filename = L"temp-fixture.json", .dpi = 96};
	SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)&state);
	ASSERT_BOOL_EQ(createControls(window, &state), true);
	checkFonts(&state);
	state.taskbarCreated = RegisterWindowMessageW(L"TaskbarButtonCreated");
	ASSERT_BOOL_EQ(state.taskbarCreated != 0 && state.taskbar == NULL && taskbarCreates == 0, true);
	ASSERT_BOOL_EQ(TabCtrl_GetItemCount(state.tabs) == 2, true);
	wchar_t text[256] = {0};
	TCITEMW tab = {.mask = TCIF_TEXT, .pszText = text, .cchTextMax = 256};
	ASSERT_BOOL_EQ(SendMessageW(state.tabs, TCM_GETITEMW, 0, (LPARAM)&tab), true);
	ASSERT_INT_EQ(wcscmp(text, L"Squads"), 0);
	ASSERT_BOOL_EQ(SendMessageW(state.tabs, TCM_GETITEMW, 1, (LPARAM)&tab), true);
	ASSERT_INT_EQ(wcscmp(text, L"Simulation"), 0);
	int minimum = 0;
	int maximum = 0;
	SendMessageW(state.simulation[SIM_SPINNER], UDM_GETRANGE32, (WPARAM)&minimum, (LPARAM)&maximum);
	ASSERT_BOOL_EQ(minimum == 1 && maximum == 7, true);
	ASSERT_BOOL_EQ(GetWindowTextW(state.simulation[SIM_THREADS], text, 256) == 1, true);
	ASSERT_INT_EQ(wcscmp(text, L"7"), 0);
	ASSERT_BOOL_EQ(GetWindowTextW(state.simulation[SIM_SEEDS], text, 256) == 4, true);
	ASSERT_INT_EQ(wcscmp(text, L"keep"), 0);
	ASSERT_BOOL_EQ(
		SendMessageW(state.simulation[SIM_DETAILED], BM_GETCHECK, 0, 0) == BST_UNCHECKED, true
	);
	ASSERT_BOOL_EQ(SendMessageW(state.simulation[SIM_SEEDS], EM_GETLIMITTEXT, 0, 0) == 255, true);
	ASSERT_BOOL_EQ(GetWindowTextLengthW(state.simulation[SIM_SUMMARY]) == 0, true);
	ASSERT_BOOL_EQ(!controlVisible(state.progress), true);
	ASSERT_BOOL_EQ(GetWindowLongW(state.tabs, GWL_STYLE) & WS_CLIPSIBLINGS, true);
	checkControlColors(window, state.headings[0], WM_CTLCOLORSTATIC);
	checkControlColors(window, state.notes[0], WM_CTLCOLORSTATIC);
	checkControlColors(window, state.simulation[SIM_THREADS_LABEL], WM_CTLCOLORSTATIC);
	checkControlColors(window, state.simulation[SIM_DETAILED], WM_CTLCOLORBTN);
	changeTab(window, &state, 0);
	changeTab(window, &state, 1);
	layout(window, &state);
	checkPageLayout(window, &state);
	state.dpi = 144;
	ASSERT_BOOL_EQ(SetWindowPos(window, NULL, 0, 0, 1350, 900, SWP_NOMOVE | SWP_NOZORDER), true);
	ASSERT_BOOL_EQ(setFonts(&state), true);
	layout(window, &state);
	checkPageLayout(window, &state);
	state.dpi = 96;
	ASSERT_BOOL_EQ(SetWindowPos(window, NULL, 0, 0, 900, 600, SWP_NOMOVE | SWP_NOZORDER), true);
	ASSERT_BOOL_EQ(setFonts(&state), true);
	layout(window, &state);

	int errors = errorMessages;
	static const wchar_t *const invalidThreads[] = {L"", L"0", L"8", L"-1", L"abc", L"4294967296"};
	for (unsigned i = 0; i < sizeof invalidThreads / sizeof invalidThreads[0]; ++i) {
		ASSERT_BOOL_EQ(SetWindowTextW(state.simulation[SIM_THREADS], invalidThreads[i]), true);
		clickSimulate(window, &state);
		ASSERT_BOOL_EQ(state.simulationThread == NULL && errorMessages == ++errors, true);
	}
	ASSERT_BOOL_EQ(SetWindowTextW(state.simulation[SIM_THREADS], L"2"), true);
	static const wchar_t *const invalidSeeds[] = {L"", L"abc", L"1\nthreads=7", L"1;2"};
	for (unsigned i = 0; i < sizeof invalidSeeds / sizeof invalidSeeds[0]; ++i) {
		ASSERT_BOOL_EQ(SetWindowTextW(state.simulation[SIM_SEEDS], invalidSeeds[i]), true);
		clickSimulate(window, &state);
		ASSERT_BOOL_EQ(state.simulationThread == NULL && errorMessages == ++errors, true);
	}
	ASSERT_BOOL_EQ(simulationCalls == 0, true);
	simulationRelease = CreateEventW(NULL, TRUE, FALSE, NULL);
	ASSERT_BOOL_EQ(simulationRelease != NULL, true);
	expectedProgressWindow = window;
	static const wchar_t *const patterns[] = {L"1", L"42", L"1-1000", L"1,3,5", L"1,4,7-70", L"keep"};
	for (unsigned i = 0; i < sizeof patterns / sizeof patterns[0]; ++i) {
		if (i == 4) {
			wcscpy(state.savedFilename, L"saved fixture.json");
		}
		ASSERT_BOOL_EQ(
			paths_absolute(
				state.savedFilename[0] ? state.savedFilename : path, expectedInput, HARNESS_PATH_LENGTH
			),
			true
		);
		expectedSeeds = patterns[i];
		expectedThreads = i % 2 ? L"7" : L"1";
		expectedDetailed = i % 2 != 0;
		simulationResult = i == 5 ? 1 : 0;
		ASSERT_BOOL_EQ(SetWindowTextW(state.simulation[SIM_SEEDS], expectedSeeds), true);
		ASSERT_BOOL_EQ(SetWindowTextW(state.simulation[SIM_THREADS], expectedThreads), true);
		SendMessageW(
			state.simulation[SIM_DETAILED], BM_SETCHECK, expectedDetailed ? BST_CHECKED : BST_UNCHECKED, 0
		);
		ASSERT_BOOL_EQ(ResetEvent(simulationRelease), true);
		clickSimulate(window, &state);
		ASSERT_BOOL_EQ(state.simulationThread != NULL, true);
		checkSimulationSummary(&state, L"n = 0: home 0 (0.0%) / draw 0 (0.0%) / away 0 (0.0%)");
		if (i == 0) {
			ASSERT_BOOL_EQ(taskbarStateCalls == 0 && state.taskbar == NULL, true);
			ASSERT_BOOL_EQ(windowProcedure(window, state.taskbarCreated, 0, 0) == 0, true);
		}
		ASSERT_BOOL_EQ(state.taskbar != NULL && taskbarState == TBPF_INDETERMINATE, true);
		HANDLE worker = state.simulationThread;
		ASSERT_BOOL_EQ(controlVisible(state.progress), true);
		ASSERT_BOOL_EQ(SendMessageW(state.progress, PBM_GETPOS, 0, 0) == 0, true);
		checkPageLayout(window, &state);
		simulationProgress(window, 0, 1000, 0, 0);
		ASSERT_BOOL_EQ(SendMessageW(state.progress, PBM_GETRANGE, FALSE, 0) == 1000, true);
		ASSERT_BOOL_EQ(
			taskbarState == TBPF_NORMAL && taskbarCompleted == 0 && taskbarTotal == 1000, true
		);
		checkSimulationSummary(&state, L"n = 0: home 0 (0.0%) / draw 0 (0.0%) / away 0 (0.0%)");
		simulationProgress(window, 1, 1000, 1, 0);
		checkSimulationSummary(&state, L"n = 1: home 1 (100.0%) / draw 0 (0.0%) / away 0 (0.0%)");
		simulationProgress(window, 2, 1000, 1, 1);
		checkSimulationSummary(&state, L"n = 2: home 1 (50.0%) / draw 1 (50.0%) / away 0 (0.0%)");
		simulationProgress(window, 3, 1000, 1, 1);
		checkSimulationSummary(&state, L"n = 3: home 1 (33.3%) / draw 1 (33.3%) / away 1 (33.3%)");
		simulationProgress(window, 250, 1000, 100, 50);
		ASSERT_BOOL_EQ(SendMessageW(state.progress, PBM_GETPOS, 0, 0) == 250, true);
		ASSERT_BOOL_EQ(
			taskbarState == TBPF_NORMAL && taskbarCompleted == 250 && taskbarTotal == 1000, true
		);
		checkSimulationSummary(
			&state, L"n = 250: home 100 (40.0%) / draw 50 (20.0%) / away 100 (40.0%)"
		);
		unsigned stateCalls = taskbarStateCalls;
		unsigned valueCalls = taskbarValueCalls;
		ASSERT_BOOL_EQ(windowProcedure(window, HARNESS_PROGRESS_MESSAGE, 1001, 1000) == 0, true);
		ASSERT_BOOL_EQ(windowProcedure(window, HARNESS_PROGRESS_MESSAGE, 0, 0) == 0, true);
		ASSERT_BOOL_EQ(windowProcedure(window, HARNESS_PROGRESS_MESSAGE, 0, -1) == 0, true);
		ASSERT_BOOL_EQ(
			windowProcedure(window, HARNESS_PROGRESS_MESSAGE, 0, (LPARAM)INT_MAX + 1) == 0, true
		);
		simulationProgress(window, 250, 1000, 251, 0);
		simulationProgress(window, 250, 1000, 100, 151);
		ASSERT_BOOL_EQ(SendMessageW(state.progress, PBM_GETPOS, 0, 0) == 250, true);
		ASSERT_BOOL_EQ(taskbarStateCalls == stateCalls && taskbarValueCalls == valueCalls, true);
		checkSimulationSummary(
			&state, L"n = 250: home 100 (40.0%) / draw 50 (20.0%) / away 100 (40.0%)"
		);
		unsigned creates = taskbarCreates;
		unsigned releases = taskbarReleases;
		ASSERT_BOOL_EQ(windowProcedure(window, state.taskbarCreated, 0, 0) == 0, true);
		ASSERT_BOOL_EQ(taskbarCreates == creates + 1 && taskbarReleases == releases + 1, true);
		ASSERT_BOOL_EQ(taskbarInitializations == taskbarCreates, true);
		ASSERT_BOOL_EQ(
			taskbarState == TBPF_NORMAL && taskbarCompleted == 250 && taskbarTotal == 1000, true
		);
		if (i == 0) {
			taskbarValueResult = E_FAIL;
			simulationProgress(window, 500, 1000, 200, 100);
			ASSERT_BOOL_EQ(state.taskbar == NULL && state.simulationThread == worker, true);
			ASSERT_BOOL_EQ(SendMessageW(state.progress, PBM_GETPOS, 0, 0) == 500, true);
			checkSimulationSummary(
				&state, L"n = 500: home 200 (40.0%) / draw 100 (20.0%) / away 200 (40.0%)"
			);
			taskbarValueResult = S_OK;
			ASSERT_BOOL_EQ(windowProcedure(window, state.taskbarCreated, 0, 0) == 0, true);
			ASSERT_BOOL_EQ(taskbarCompleted == 500 && taskbarTotal == 1000, true);
		}
		ASSERT_BOOL_EQ(!IsWindowEnabled(state.simulation[SIM_SUBMIT]), true);
		ASSERT_BOOL_EQ(WaitForSingleObject(worker, 0) == WAIT_TIMEOUT, true);
		clickSimulate(window, &state);
		ASSERT_BOOL_EQ(state.simulationThread == worker, true);
		ASSERT_BOOL_EQ(windowProcedure(window, WM_CLOSE, 0, 0) == 0, true);
		ASSERT_BOOL_EQ(IsWindow(window) && errorMessages == ++errors, true);
		changeTab(window, &state, 0);
		ASSERT_BOOL_EQ(controlVisible(state.progress), true);
		changeTab(window, &state, 1);
		ASSERT_BOOL_EQ(controlVisible(state.progress), true);
		ASSERT_BOOL_EQ(SetWindowPos(window, NULL, 0, 0, 1000, 700, SWP_NOMOVE | SWP_NOZORDER), true);
		layout(window, &state);
		checkPageLayout(window, &state);
		simulationProgress(window, 750, 1000, 375, 125);
		checkSimulationSummary(
			&state, L"n = 750: home 375 (50.0%) / draw 125 (16.7%) / away 250 (33.3%)"
		);
		if (!simulationResult) {
			ASSERT_BOOL_EQ(
				PostMessageW(
					window,
					HARNESS_PROGRESS_MESSAGE,
					(WPARAM)(UINT64_C(500) << 32 | 1000),
					(LPARAM)(UINT64_C(250) << 32 | 1000)
				),
				true
			);
		}
		ASSERT_BOOL_EQ(SetEvent(simulationRelease), true);
		ASSERT_BOOL_EQ(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0, true);
		ASSERT_BOOL_EQ(windowProcedure(window, WM_TIMER, SIMULATION_TIMER, 0) == 0, true);
		ASSERT_BOOL_EQ(state.simulationThread == NULL, true);
		ASSERT_BOOL_EQ(taskbarState == TBPF_NOPROGRESS, true);
		ASSERT_BOOL_EQ(!controlVisible(state.progress), true);
		checkSimulationSummary(
			&state,
			simulationResult ? L"n = 750: home 375 (50.0%) / draw 125 (16.7%) / away 250 (33.3%)"
											 : L"n = 1000: home 500 (50.0%) / draw 250 (25.0%) / away 250 (25.0%)"
		);
		checkPageLayout(window, &state);
		stateCalls = taskbarStateCalls;
		valueCalls = taskbarValueCalls;
		ASSERT_BOOL_EQ(windowProcedure(window, HARNESS_PROGRESS_MESSAGE, 1, 10) == 0, true);
		ASSERT_BOOL_EQ(
			SendMessageW(state.progress, PBM_GETPOS, 0, 0) == (simulationResult ? 750 : 1000), true
		);
		ASSERT_BOOL_EQ(taskbarStateCalls == stateCalls && taskbarValueCalls == valueCalls, true);
		changeTab(window, &state, 0);
		changeTab(window, &state, 1);
		checkSimulationSummary(
			&state,
			simulationResult ? L"n = 750: home 375 (50.0%) / draw 125 (16.7%) / away 250 (33.3%)"
											 : L"n = 1000: home 500 (50.0%) / draw 250 (25.0%) / away 250 (25.0%)"
		);
		ASSERT_BOOL_EQ(IsWindowEnabled(state.simulation[SIM_SUBMIT]), true);
		ASSERT_BOOL_EQ(simulationCalls == i + 1, true);
		ASSERT_BOOL_EQ(GetWindowTextW(state.simulation[SIM_STATUS], text, 256) > 0, true);
		ASSERT_BOOL_EQ(wcsstr(text, simulationResult ? L"failed" : L"complete") != NULL, true);
		if (simulationResult) {
			++errors;
		}
		ASSERT_BOOL_EQ(errorMessages == errors, true);
		ASSERT_BOOL_EQ(GetWindowTextW(state.simulation[SIM_SEEDS], text, 256) > 0, true);
		ASSERT_INT_EQ(wcscmp(text, expectedSeeds), 0);
	}
	taskbarCreateResult = E_FAIL;
	ASSERT_BOOL_EQ(windowProcedure(window, state.taskbarCreated, 0, 0) == 0, true);
	ASSERT_BOOL_EQ(state.taskbar == NULL, true);
	taskbarCreateResult = S_OK;
	taskbarInitResult = E_FAIL;
	ASSERT_BOOL_EQ(windowProcedure(window, state.taskbarCreated, 0, 0) == 0, true);
	ASSERT_BOOL_EQ(state.taskbar == NULL, true);
	taskbarInitResult = S_OK;
	taskbarStateResult = E_FAIL;
	ASSERT_BOOL_EQ(windowProcedure(window, state.taskbarCreated, 0, 0) == 0, true);
	ASSERT_BOOL_EQ(state.taskbar == NULL, true);
	taskbarStateResult = S_OK;
	checkAwayEditor(window, &state);
	ASSERT_BOOL_EQ(windowProcedure(window, state.taskbarCreated, 0, 0) == 0, true);
	ASSERT_BOOL_EQ(state.taskbar != NULL && taskbarState == TBPF_NOPROGRESS, true);
	releaseTaskbar(&state);
	ASSERT_BOOL_EQ(state.taskbar == NULL && taskbarReleases == taskbarInitializations, true);
	ASSERT_BOOL_EQ(CloseHandle(simulationRelease), true);
	ASSERT_BOOL_EQ(state.editIcon != NULL, true);
	HBITMAP cached = state.editIcon;
	ASSERT_BOOL_EQ(windowProcedure(window, WM_DESTROY, 0, 0) == 0, true);
	ASSERT_BOOL_EQ(state.editIcon == NULL, true);
	BITMAP image;
	ASSERT_INT_EQ(GetObjectW(cached, sizeof image, &image), 0);
	MSG quit;
	ASSERT_BOOL_EQ(PeekMessageW(&quit, NULL, WM_QUIT, WM_QUIT, PM_REMOVE), true);
	ASSERT_BOOL_EQ(DestroyWindow(window), true);
	ASSERT_BOOL_EQ(DeleteObject(state.font), true);
	ASSERT_BOOL_EQ(DeleteObject(state.headingFont), true);
	ASSERT_BOOL_EQ(UnregisterClassW(pitchClass, instance), true);

	static const unsigned long limits[] = {1, HARNESS_MAX_THREADS};
	for (unsigned i = 0; i < sizeof limits / sizeof limits[0]; ++i) {
		threadLimit = limits[i];
		window =
			CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 900, 600, NULL, NULL, instance, NULL);
		ASSERT_BOOL_EQ(window != NULL, true);
		state = (WindowState){0};
		ASSERT_BOOL_EQ(createSimulationControls(window, &state), true);
		SendMessageW(state.simulation[SIM_SPINNER], UDM_GETRANGE32, (WPARAM)&minimum, (LPARAM)&maximum);
		ASSERT_BOOL_EQ(minimum == 1 && maximum == (int)threadLimit, true);
		ASSERT_BOOL_EQ(
			SendMessageW(state.simulation[SIM_SPINNER], UDM_GETPOS32, 0, 0) == (LRESULT)threadLimit, true
		);
		ASSERT_BOOL_EQ(DestroyWindow(window), true);
	}
}

TEST(exports_fixtures_and_runs_simulations) {
	wchar_t original[HARNESS_PATH_LENGTH] = {0};
	wchar_t temporary[HARNESS_PATH_LENGTH] = {0};
	wchar_t directory[HARNESS_PATH_LENGTH] = {0};
	wchar_t outputDirectory[HARNESS_PATH_LENGTH] = {0};
	wchar_t source[HARNESS_PATH_LENGTH] = {0};
	DWORD length = GetCurrentDirectoryW(HARNESS_PATH_LENGTH, original);
	ASSERT_BOOL_EQ(length > 0 && length < HARNESS_PATH_LENGTH, true);
	if (!length || length >= HARNESS_PATH_LENGTH) {
		return;
	}
	length = GetTempPathW(HARNESS_PATH_LENGTH, temporary);
	ASSERT_BOOL_EQ(length > 0 && length < HARNESS_PATH_LENGTH, true);
	if (!length || length >= HARNESS_PATH_LENGTH) {
		return;
	}
	UINT unique = GetTempFileNameW(temporary, L"fme", 0, directory);
	ASSERT_BOOL_EQ(unique != 0, true);
	if (!unique) {
		return;
	}
	ASSERT_BOOL_EQ(DeleteFileW(directory), true);
	BOOL created = CreateDirectoryW(directory, NULL);
	ASSERT_BOOL_EQ(created, true);
	if (!created) {
		return;
	}
	bool joined = paths_join(outputDirectory, HARNESS_PATH_LENGTH, directory, L"exports");
	ASSERT_BOOL_EQ(joined, true);
	if (!joined) {
		goto removeDirectory;
	}
	created = CreateDirectoryW(outputDirectory, NULL);
	ASSERT_BOOL_EQ(created, true);
	if (!created) {
		goto removeDirectory;
	}
	joined = paths_join(source, HARNESS_PATH_LENGTH, directory, L"temp-fixture.json");
	ASSERT_BOOL_EQ(joined, true);
	if (!joined) {
		goto removeOutputDirectory;
	}
	bool written = files_write(source, "{\"fixture\":true}", 16);
	ASSERT_BOOL_EQ(written, true);
	if (!written) {
		goto removeOutputDirectory;
	}
	BOOL changed = SetCurrentDirectoryW(outputDirectory);
	ASSERT_BOOL_EQ(changed, true);
	if (!changed) {
		goto removeSource;
	}
	HMODULE richEdit = LoadLibraryW(L"Msftedit.dll");
	ASSERT_BOOL_EQ(richEdit != NULL, true);
	if (!richEdit) {
		goto restoreDirectory;
	}
	HWND window =
		CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 800, 400, NULL, NULL, NULL, NULL);
	ASSERT_BOOL_EQ(window != NULL, true);
	if (!window) {
		goto freeRichEdit;
	}
	FixtureData *data = calloc(1, sizeof *data);
	ASSERT_BOOL_EQ(data != NULL, true);
	if (!data) {
		goto destroyWindow;
	}
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
	ASSERT_BOOL_EQ(state.metadata != NULL, true);
	SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)&state);
	ASSERT_BOOL_EQ(setMetadata(&state), true);
	firstLine(state.metadata, L"Viewing fixture: temp-fixture.json");
	checkFormat(state.metadata, 17, 34, CFE_BOLD, GetSysColor(COLOR_WINDOWTEXT));

	state.temporary = true;
	ASSERT_BOOL_EQ(setMetadata(&state), true);
	firstLine(state.metadata, L"This fixture object is not saved. Click here to export it");
	LONG linkStart = (LONG)wcslen(unsavedPrefix);
	LONG linkEnd = linkStart + (LONG)wcslen(exportLink);
	checkFormat(state.metadata, 0, linkStart, CFE_BOLD, RGB(255, 0, 0));
	checkFormat(state.metadata, linkStart, linkEnd, CFE_LINK | CFE_UNDERLINE, RGB_BLUE);
	checkFormat(state.metadata, linkEnd, linkEnd + 13, 0, GetSysColor(COLOR_WINDOWTEXT));

	ENLINK link = {.nmhdr = {.hwndFrom = state.metadata, .code = EN_LINK}, .msg = WM_LBUTTONDOWN};
	ASSERT_BOOL_EQ(windowProcedure(window, WM_NOTIFY, 0, (LPARAM)&link) == 0, true);
	ASSERT_BOOL_EQ(state.temporary, true);
	link.msg = WM_LBUTTONUP;
	ASSERT_BOOL_EQ(windowProcedure(window, WM_NOTIFY, 0, (LPARAM)&link) == 1, true);
	ASSERT_BOOL_EQ(!state.temporary && !state.missing && errorMessages == 0, true);
	ASSERT_INT_EQ(wcscmp(state.savedFilename, L"aston-villa-v-bristol-city-12345.json"), 0);
	firstLine(state.metadata, L"Viewing fixture: aston-villa-v-bristol-city-12345.json");
	checkContents(state.savedFilename, "{\"fixture\":true}");
	checkContents(source, "{\"fixture\":true}");

	ASSERT_BOOL_EQ(files_write(state.savedFilename, "existing", 8), true);
	state.temporary = true;
	ASSERT_BOOL_EQ(setMetadata(&state), true);
	ASSERT_BOOL_EQ(windowProcedure(window, WM_NOTIFY, 0, (LPARAM)&link) == 1, true);
	ASSERT_BOOL_EQ(state.temporary && !state.missing && errorMessages == 1, true);
	firstLine(state.metadata, L"This fixture object is not saved. Click here to export it");
	checkContents(state.savedFilename, "existing");
	ASSERT_BOOL_EQ(DeleteFileW(state.savedFilename), true);

	ASSERT_BOOL_EQ(DeleteFileW(source), true);
	ASSERT_BOOL_EQ(windowProcedure(window, WM_NOTIFY, 0, (LPARAM)&link) == 1, true);
	ASSERT_BOOL_EQ(state.temporary && state.missing && errorMessages == 1, true);
	firstLine(state.metadata, L"Fixture cannot be found; restart the app");
	checkFormat(state.metadata, 0, (LONG)wcslen(missingFixture), CFE_BOLD, RGB(255, 0, 0));
	ASSERT_BOOL_EQ(setMetadata(&state), true);
	firstLine(state.metadata, L"Fixture cannot be found; restart the app");
	ASSERT_BOOL_EQ(windowProcedure(window, WM_NOTIFY, 0, (LPARAM)&link) == 1, true);
	ASSERT_BOOL_EQ(errorMessages == 1, true);

	checkSimulationForm(data, source);
	free(data);
destroyWindow:
	ASSERT_BOOL_EQ(DestroyWindow(window), true);
freeRichEdit:
	ASSERT_BOOL_EQ(FreeLibrary(richEdit), true);
restoreDirectory:
	ASSERT_BOOL_EQ(SetCurrentDirectoryW(original), true);
removeSource:
	if (!fileMissing(source)) {
		ASSERT_BOOL_EQ(DeleteFileW(source), true);
	}
removeOutputDirectory:
	ASSERT_BOOL_EQ(RemoveDirectoryW(outputDirectory), true);
removeDirectory:
	ASSERT_BOOL_EQ(RemoveDirectoryW(directory), true);
}

void registerFixtureWindowTests(void) {
	test_run(
		"fixture window exports fixtures, replaces opponents and runs simulations",
		test_exports_fixtures_and_runs_simulations
	);
}
