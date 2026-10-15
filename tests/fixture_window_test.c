// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "files.h"
#include "paths.h"
#include "protocol.h"

#include <time.h>
#include <windows.h>


static int errorMessages;
static time_t testTime(time_t *out);
static int testMessageBox(HWND window, const wchar_t *text, const wchar_t *caption, UINT type);

#define time testTime
#define MessageBoxW testMessageBox
#include "../src/ui/fixture_window.c" // NOLINT(bugprone-suspicious-include): test internal UI callbacks.


#undef time
#undef MessageBoxW

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
static unsigned long threadLimit = 7;

unsigned long commands_defaultThreads(void) {
	return threadLimit;
}

int commands_run(int argc, wchar_t **argv) {
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

bool fixtureLoad_open(const wchar_t *path, FixtureData *out, char *error, size_t errorSize) {
	(void)path;
	(void)out;
	(void)snprintf(error, errorSize, "fixture loading is not used by this summary check");
	return false;
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
}

static void clickSimulate(HWND window, WindowState *state) {
	CHECK(
		windowProcedure(
			window, WM_COMMAND, MAKEWPARAM(0, BN_CLICKED), (LPARAM)state->simulation[SIM_SUBMIT]
		) == 0
	);
}

static void checkSimulationForm(FixtureData *data, const wchar_t *path) {
	INITCOMMONCONTROLSEX controls = {
		.dwSize = sizeof controls, .dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_UPDOWN_CLASS};
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
	changeTab(window, &state, 0);
	changeTab(window, &state, 1);
	layout(window, &state);
	state.dpi = 144;
	CHECK(setFonts(&state));
	layout(window, &state);
	state.dpi = 96;
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
		HANDLE worker = state.simulationThread;
		CHECK(!IsWindowEnabled(state.simulation[SIM_SUBMIT]));
		CHECK(WaitForSingleObject(worker, 0) == WAIT_TIMEOUT);
		clickSimulate(window, &state);
		CHECK(state.simulationThread == worker);
		CHECK(windowProcedure(window, WM_CLOSE, 0, 0) == 0);
		CHECK(IsWindow(window) && errorMessages == ++errors);
		changeTab(window, &state, 0);
		changeTab(window, &state, 1);
		CHECK(SetEvent(simulationRelease));
		CHECK(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0);
		CHECK(windowProcedure(window, WM_TIMER, SIMULATION_TIMER, 0) == 0);
		CHECK(state.simulationThread == NULL);
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

int main(void) {
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
