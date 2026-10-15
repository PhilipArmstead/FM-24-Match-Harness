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

	CHECK(DestroyWindow(window));
	CHECK(FreeLibrary(richEdit));
	free(data);
	CHECK(SetCurrentDirectoryW(original));
	CHECK(RemoveDirectoryW(outputDirectory));
	CHECK(RemoveDirectoryW(directory));
	return 0;
}
