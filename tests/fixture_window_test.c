// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../ui/constants.h"
#include "files.h"
#include "paths.h"
#include "protocol.h"
#include "test.h"
#include "tests.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <time.h>
#include <windows.h>


static int errorMessages;
static unsigned discardPrompts;
static int discardAnswer = IDNO;
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
static HRESULT WINAPI testShellItem(PCWSTR path, IBindCtx *context, REFIID interfaceId, void **out);

#define time testTime
#define MessageBoxW testMessageBox
#define CoCreateInstance testCoCreateInstance
#define EnumFontFamiliesExW testEnumFontFamiliesExW
#define SHCreateItemFromParsingName testShellItem
#include "../src/ui/fixture_window.c" // NOLINT(bugprone-suspicious-include): test internal UI callbacks.


#undef time
#undef MessageBoxW
#undef CoCreateInstance
#undef EnumFontFamiliesExW
#undef SHCreateItemFromParsingName

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
	if ((type & MB_ICONMASK) == MB_ICONWARNING) {
		ASSERT_BOOL_EQ((type & MB_YESNO) == MB_YESNO && (type & MB_DEFBUTTON2) != 0, true);
		++discardPrompts;
		return discardAnswer;
	}
	++errorMessages;
	return IDOK;
}

static const wchar_t *expectedSeeds;
static const wchar_t *expectedThreads;
static wchar_t expectedInput[HARNESS_PATH_LENGTH];
static bool expectedDetailed;
static bool expectedVenueInput;
static bool expectedPlayerInput;
static wchar_t simulationTemporary[HARNESS_PATH_LENGTH];
static unsigned simulationCalls;
static int simulationResult;
static HANDLE simulationRelease;
static HWND expectedProgressWindow;
static unsigned long threadLimit = 7;
static HANDLE replacementRelease;
static unsigned replacementCalls;
static int replacementResult;
static bool replacementLoadFailure;
static unsigned expectedSide = 1;
static const FixtureData *replacementInput;
static const wchar_t *expectedTeam;
static wchar_t replacementOutput[HARNESS_PATH_LENGTH];
static unsigned teamLists;
static bool teamListFailure;
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
static bool dialogSave;
static bool dialogBinary;
static unsigned dialogCreates;
static unsigned dialogReleases;
static unsigned shellReleases;
static HRESULT dialogCreateResult = S_OK;
static HRESULT dialogShowResult = HRESULT_FROM_WIN32(ERROR_CANCELLED);
static const wchar_t *dialogPath;
static const wchar_t *dialogFilename;
static HWND dialogOwner;
static IShellItem folderItem;
static IShellItem selectedItem;
static const wchar_t *fileLoadPath;
static const wchar_t *fileSaveInput;
static const wchar_t *fileSaveOutput;
static bool fileFailure;
static HANDLE fileRelease;
static unsigned fileLoads;
static unsigned fileSaves;
static bool binaryReplacement;

static ULONG STDMETHODCALLTYPE testDialogRelease(IFileDialog *dialog) {
	ASSERT_BOOL_EQ(dialog != NULL, true);
	++dialogReleases;
	return 0;
}

static HRESULT STDMETHODCALLTYPE
testDialogOptions(IFileDialog *dialog, FILEOPENDIALOGOPTIONS options) {
	(void)dialog;
	ASSERT_BOOL_EQ(
		options == (FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR |
								 (dialogSave ? FOS_OVERWRITEPROMPT | FOS_STRICTFILETYPES : FOS_FILEMUSTEXIST)),
		true
	);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE
testDialogTypes(IFileDialog *dialog, UINT count, const COMDLG_FILTERSPEC *types) {
	(void)dialog;
	ASSERT_INT_EQ((int)count, dialogSave ? 2 : 3);
	if (!dialogSave) {
		ASSERT_INT_EQ(wcscmp(types[0].pszSpec, L"*.bin;*.json"), 0);
		++types;
	}
	ASSERT_INT_EQ(wcscmp(types[0].pszSpec, L"*.json"), 0);
	ASSERT_INT_EQ(wcscmp(types[1].pszSpec, L"*.bin"), 0);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE testDialogTypeIndex(IFileDialog *dialog, UINT index) {
	(void)dialog;
	ASSERT_INT_EQ((int)index, dialogSave && dialogBinary ? 2 : 1);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE testDialogTitle(IFileDialog *dialog, LPCWSTR title) {
	(void)dialog;
	ASSERT_INT_EQ(wcscmp(title, dialogSave ? L"Save fixture as" : L"Open fixture"), 0);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE testDialogExtension(IFileDialog *dialog, LPCWSTR extension) {
	(void)dialog;
	ASSERT_INT_EQ(wcscmp(extension, dialogBinary ? L"bin" : L"json"), 0);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE testDialogFilename(IFileDialog *dialog, LPCWSTR filename) {
	(void)dialog;
	ASSERT_BOOL_EQ(dialogFilename != NULL, true);
	if (dialogFilename) {
		ASSERT_INT_EQ(wcscmp(filename, dialogFilename), 0);
	}
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE testDialogFolder(IFileDialog *dialog, IShellItem *folder) {
	(void)dialog;
	ASSERT_BOOL_EQ(folder == &folderItem, true);
	return S_OK;
}

static HRESULT STDMETHODCALLTYPE testDialogShow(IFileDialog *dialog, HWND owner) {
	(void)dialog;
	ASSERT_BOOL_EQ(owner == dialogOwner, true);
	return dialogShowResult;
}

static HRESULT STDMETHODCALLTYPE testDialogResult(IFileDialog *dialog, IShellItem **out) {
	(void)dialog;
	*out = &selectedItem;
	return S_OK;
}

static ULONG STDMETHODCALLTYPE testShellRelease(IShellItem *item) {
	ASSERT_BOOL_EQ(item == &folderItem || item == &selectedItem, true);
	++shellReleases;
	return 0;
}

static HRESULT STDMETHODCALLTYPE testShellPath(IShellItem *item, SIGDN kind, LPWSTR *out) {
	ASSERT_BOOL_EQ(item == &selectedItem && kind == SIGDN_FILESYSPATH, true);
	size_t size = (wcslen(dialogPath) + 1) * sizeof(wchar_t);
	*out = CoTaskMemAlloc(size);
	if (!*out) {
		return E_OUTOFMEMORY;
	}
	memcpy(*out, dialogPath, size);
	return S_OK;
}

static HRESULT WINAPI
testShellItem(PCWSTR path, IBindCtx *context, REFIID interfaceId, void **out) {
	wchar_t directory[HARNESS_PATH_LENGTH];
	ASSERT_BOOL_EQ(GetCurrentDirectoryW(HARNESS_PATH_LENGTH, directory) > 0, true);
	ASSERT_INT_EQ(wcscmp(path, directory), 0);
	ASSERT_BOOL_EQ(context == NULL && IsEqualGUID(interfaceId, &IID_IShellItem), true);
	*out = &folderItem;
	return S_OK;
}

static IFileDialog *testFileDialog(void) {
	static IFileDialogVtbl methods = {
		.Release = testDialogRelease,
		.SetOptions = testDialogOptions,
		.SetFileTypes = testDialogTypes,
		.SetFileTypeIndex = testDialogTypeIndex,
		.SetTitle = testDialogTitle,
		.SetDefaultExtension = testDialogExtension,
		.SetFileName = testDialogFilename,
		.SetFolder = testDialogFolder,
		.Show = testDialogShow,
		.GetResult = testDialogResult,
	};
	static IFileDialog dialog = {.lpVtbl = &methods};
	static IShellItemVtbl shellMethods = {
		.Release = testShellRelease,
		.GetDisplayName = testShellPath,
	};
	folderItem.lpVtbl = &shellMethods;
	selectedItem.lpVtbl = &shellMethods;
	return &dialog;
}

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
	if (IsEqualGUID(classId, &CLSID_FileOpenDialog) || IsEqualGUID(classId, &CLSID_FileSaveDialog)) {
		ASSERT_BOOL_EQ(
			IsEqualGUID(classId, dialogSave ? &CLSID_FileSaveDialog : &CLSID_FileOpenDialog), true
		);
		ASSERT_BOOL_EQ(IsEqualGUID(interfaceId, &IID_IFileDialog), true);
		ASSERT_BOOL_EQ(outer == NULL && context == CLSCTX_INPROC_SERVER, true);
		++dialogCreates;
		*out = SUCCEEDED(dialogCreateResult) ? testFileDialog() : NULL;
		return dialogCreateResult;
	}
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
	bool editedInput = expectedVenueInput || expectedPlayerInput;
	ASSERT_BOOL_EQ(progressWindow == (uintptr_t)expectedProgressWindow, true);
	ASSERT_BOOL_EQ(argc == (expectedDetailed ? 7 : 6) + (int)editedInput, true);
	ASSERT_INT_EQ(wcscmp(argv[1], L"run"), 0);
	if (editedInput) {
		ASSERT_BOOL_EQ(wcscmp(argv[2], expectedInput) != 0, true);
		wcscpy(simulationTemporary, argv[2]);
		wchar_t output[HARNESS_PATH_LENGTH];
		wcscpy(output, expectedInput);
		wcscpy(wcsrchr(output, L'.'), L".csv");
		ASSERT_INT_EQ(wcscmp(argv[4], output), 0);
		ByteBuffer json = {0};
		ASSERT_BOOL_EQ(files_read(argv[2], &json), true);
		if (expectedVenueInput) {
			char width[32], half[32];
			(void)snprintf(width, sizeof width, "\"u32\":%u", fixtureData_fromFloat(72.5f));
			(void)snprintf(half, sizeof half, "\"u32\":%u", fixtureData_fromFloat(36.25f));
			ASSERT_BOOL_EQ(strstr(buffer_string(&json), width) != NULL, true);
			ASSERT_BOOL_EQ(strstr(buffer_string(&json), half) != NULL, true);
		}
		if (expectedPlayerInput) {
			FixtureData *data = malloc(sizeof *data);
			ASSERT_BOOL_EQ(data != NULL, true);
			if (data) {
				char error[256];
				ASSERT_BOOL_EQ(
					fixtureData_read(buffer_string(&json), json.length, data, error, sizeof error), true
				);
				ASSERT_BOOL_EQ(data->teams[0].players[0].morale.value == 20, true);
				ASSERT_BOOL_EQ(data->teams[0].players[1].morale.value == 12, true);
				const FixturePlayer *player = &data->teams[1].players[11];
				ASSERT_BOOL_EQ(
					player->condition.value == 10000 && player->sharpness[0].value == 10000 &&
						player->sharpness[1].value == 10000 && player->fatigue.value == 0,
					true
				);
				free(data);
			}
		}
		buffer_free(&json);
	} else {
		ASSERT_INT_EQ(wcscmp(argv[2], expectedInput), 0);
	}
	ASSERT_INT_EQ(wcscmp(argv[3], expectedSeeds), 0);
	unsigned option = editedInput ? 5 : 4;
	ASSERT_INT_EQ(wcscmp(argv[option], L"--threads"), 0);
	ASSERT_INT_EQ(wcscmp(argv[option + 1], expectedThreads), 0);
	if (expectedDetailed) {
		ASSERT_INT_EQ(wcscmp(argv[option + 2], L"--detailed"), 0);
	}
	ASSERT_BOOL_EQ(argv[argc] == NULL, true);
	++simulationCalls;
	ASSERT_BOOL_EQ(WaitForSingleObject(simulationRelease, 5000) == WAIT_OBJECT_0, true);
	return simulationResult;
}

int commands_replaceTeamWithResult(int argc, wchar_t **argv, char *result, size_t resultSize) {
	ASSERT_BOOL_EQ(argc == 5 && argv[argc] == NULL, true);
	ASSERT_INT_EQ(wcscmp(argv[1], expectedSide ? L"replace-away" : L"replace-home"), 0);
	ASSERT_INT_EQ(wcscmp(argv[2], binaryReplacement ? argv[4] : expectedInput), 0);
	ASSERT_INT_EQ(wcscmp(argv[3], expectedTeam), 0);
	ASSERT_BOOL_EQ(files_hasExtension(argv[4], L".json"), true);
	wcscpy(replacementOutput, argv[4]);
	++replacementCalls;
	ASSERT_BOOL_EQ(WaitForSingleObject(replacementRelease, 5000) == WAIT_OBJECT_0, true);
	(void)snprintf(
		result,
		resultSize,
		"%s",
		replacementResult
			? (expectedSide ? "error: that is the home team" : "error: that is the away team")
			: "ok"
	);
	if (!replacementResult) {
		ASSERT_BOOL_EQ(files_write(argv[4], "{\"replacement\":true}", 20), true);
	}
	return replacementResult;
}

int commands_listTeamsWithResult(int argc, wchar_t **argv, char *result, size_t resultSize) {
	ASSERT_BOOL_EQ(argc == 3 && argv[argc] == NULL, true);
	ASSERT_INT_EQ(wcscmp(argv[1], L"list-teams"), 0);
	++teamLists;
	if (teamListFailure) {
		(void)snprintf(result, resultSize, "error: FM24 is not running");
		return 1;
	}
	static const char list[] =
		"677\tLuton Town\n12\tAston Villa\r\n99\tUnited\n691\tNorwich City\n98\tUnited\nbad\n";
	ASSERT_BOOL_EQ(files_write(argv[2], list, sizeof list - 1), true);
	(void)snprintf(result, resultSize, "ok: 5 teams");
	return 0;
}

bool fixtureLoad_open(const wchar_t *path, FixtureData *out, char *error, size_t errorSize) {
	if (fileLoadPath) {
		ASSERT_INT_EQ(wcscmp(path, fileLoadPath), 0);
		++fileLoads;
		ASSERT_BOOL_EQ(WaitForSingleObject(fileRelease, 5000) == WAIT_OBJECT_0, true);
		if (fileFailure) {
			(void)snprintf(error, errorSize, "Cannot read the selected fixture.");
			return false;
		}
		memset(out, 0, sizeof *out);
		strcpy(out->teams[0].name, "Opened home");
		strcpy(out->teams[1].name, "Opened away");
		return true;
	}
	ASSERT_INT_EQ(wcscmp(path, replacementOutput), 0);
	if (replacementLoadFailure) {
		(void)snprintf(error, errorSize, "cannot read replacement fixture");
		return false;
	}
	if (replacementInput) {
		*out = *replacementInput;
	} else {
		memset(out, 0, sizeof *out);
		strcpy(out->teams[0].name, "Aston Villa");
	}
	FixtureTeam *team = &out->teams[expectedSide];
	memset(team, 0, sizeof *team);
	strcpy(team->name, expectedSide ? "Luton Town" : "Norwich City");
	strcpy(team->tactic, "New tactic");
	strcpy(team->formation, "4-2-3-1");
	team->playerCount = (FixtureNumber){.value = 1, .known = true};
	strcpy(team->players[0].name, "New player");
	team->slots[0].position = (FixtureNumber){.value = 1, .known = true};
	return true;
}

bool fixtureLoad_save(const wchar_t *input, const wchar_t *output, char *error, size_t errorSize) {
	ASSERT_INT_EQ(wcscmp(input, fileSaveInput), 0);
	if (binaryReplacement) {
		ASSERT_BOOL_EQ(files_hasExtension(output, L".json"), true);
		return files_write(output, "{\"decoded\":true}", 16);
	}
	ASSERT_INT_EQ(wcscmp(output, fileSaveOutput), 0);
	++fileSaves;
	ASSERT_BOOL_EQ(WaitForSingleObject(fileRelease, 5000) == WAIT_OBJECT_0, true);
	if (fileFailure) {
		(void)snprintf(error, errorSize, "Cannot save the fixture.");
		return false;
	}
	return CopyFileW(input, output, FALSE) != 0;
}

bool fixtureLoad_saveVenue(
	const wchar_t *input,
	const wchar_t *output,
	const FixtureNumber values[VENUE_FIELD_COUNT],
	char *error,
	size_t errorSize
) {
	if (!values) {
		return fixtureLoad_save(input, output, error, errorSize);
	}
	ByteBuffer json = {0}, edited = {0};
	bool ok =
		files_read(input, &json) &&
		fixtureData_writeVenue(buffer_string(&json), json.length, values, &edited, error, errorSize) &&
		files_write(output, edited.data, edited.length);
	buffer_free(&edited);
	buffer_free(&json);
	return ok;
}

bool fixtureLoad_saveEdits(
	const wchar_t *input,
	const wchar_t *output,
	const FixtureNumber values[VENUE_FIELD_COUNT],
	const FixturePlayerEdits *players,
	char *error,
	size_t errorSize
) {
	if (!fixtureData_hasPlayerEdits(players)) {
		return fixtureLoad_saveVenue(input, output, values, error, errorSize);
	}
	ByteBuffer json = {0}, edited = {0}, venue = {0};
	bool ok =
		files_read(input, &json) &&
		fixtureData_writePlayers(buffer_string(&json), json.length, players, &edited, error, errorSize);
	if (ok && values) {
		ok = fixtureData_writeVenue(
			buffer_string(&edited), edited.length, values, &venue, error, errorSize
		);
	}
	const ByteBuffer *result = values ? &venue : &edited;
	ok = ok && files_write(output, result->data, result->length);
	buffer_free(&venue);
	buffer_free(&edited);
	buffer_free(&json);
	return ok;
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

static void checkNotes(HWND metadata) {
	wchar_t text[8192] = {0};
	GETTEXTEX request = {.cb = sizeof text, .codepage = 1200};
	ASSERT_BOOL_EQ(SendMessageW(metadata, EM_GETTEXTEX, (WPARAM)&request, (LPARAM)text) > 0, true);
	const wchar_t *note = wcsstr(text, L"\rNOTE: ");
	const wchar_t *controlNote = wcsstr(text, L"\rNOTE 2: ");
	ASSERT_BOOL_EQ(note && controlNote && note < controlNote, true);
	if (!note || !controlNote) {
		return;
	}
	ASSERT_INT_EQ(
		wcscmp(
			controlNote,
			L"\rNOTE 2: AI/unknown control: assignments unmapped. Squad listed below; AI may replace "
			L"this tactic."
		),
		0
	);
	LONG start = (LONG)(note + 1 - text);
	checkFormat(metadata, start, start + 5, CFE_BOLD, GetSysColor(COLOR_WINDOWTEXT));
	start = (LONG)(controlNote + 1 - text);
	checkFormat(metadata, start, start + 7, CFE_BOLD, GetSysColor(COLOR_WINDOWTEXT));
	checkFormat(metadata, start + 7, (LONG)wcslen(text), 0, GetSysColor(COLOR_WINDOWTEXT));
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
		ASSERT_BOOL_EQ(controlVisible(state->fileButtons[i]), true);
	}
	for (unsigned i = 0; i < 2; ++i) {
		ASSERT_BOOL_EQ(controlVisible(state->headings[i]), tab == 0);
		ASSERT_BOOL_EQ(controlVisible(state->pitches[i]), tab == 0);
		ASSERT_BOOL_EQ(controlVisible(state->lists[i]), tab == 0);
		for (unsigned button = 0; button < PLAYER_BUTTON_COUNT; ++button) {
			ASSERT_BOOL_EQ(controlVisible(state->playerButtons[i][button]), tab == 0);
		}
	}
	for (unsigned i = 0; i < SIM_CONTROL_COUNT; ++i) {
		ASSERT_BOOL_EQ(controlVisible(state->simulation[i]), tab == 2);
	}
	for (unsigned i = 0; i < VENUE_FIELD_COUNT; ++i) {
		ASSERT_BOOL_EQ(controlVisible(state->venueLabels[i]), tab == 1);
		ASSERT_BOOL_EQ(controlVisible(state->venueInputs[i]), tab == 1);
	}
	for (unsigned side = 0; side < 2; ++side) {
		for (unsigned i = 0; i < TEAM_CONTROL_COUNT; ++i) {
			ASSERT_BOOL_EQ(
				controlVisible(state->teamControls[side][i]),
				tab == 0 && (i == TEAM_ICON ? !state->editingTeam[side] : state->editingTeam[side]) &&
					(i != TEAM_ERROR || GetWindowTextLengthW(state->teamControls[side][i]) > 0)
			);
		}
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

static void checkFileButtonLayout(HWND window, const WindowState *state) {
	RECT metadata = controlBounds(window, state->metadata);
	RECT text = {0};
	SendMessageW(state->metadata, EM_GETRECT, 0, (LPARAM)&text);
	MapWindowPoints(state->metadata, window, (POINT *)&text, 2);
	RECT previous = text;
	for (unsigned i = 0; i < 2; ++i) {
		RECT button = controlBounds(window, state->fileButtons[i]);
		ASSERT_BOOL_EQ(button.left > previous.right && button.right < metadata.right, true);
		ASSERT_BOOL_EQ(button.top > metadata.top && button.bottom < metadata.bottom, true);
		ASSERT_INT_EQ(button.right - button.left, scaled(state, 78));
		ASSERT_INT_EQ(button.bottom - button.top, scaled(state, 26));
		ASSERT_BOOL_EQ(
			(HFONT)SendMessageW(state->fileButtons[i], WM_GETFONT, 0, 0) == state->font, true
		);
		previous = button;
	}
}

static void checkPageLayout(HWND window, const WindowState *state) {
	checkFileButtonLayout(window, state);
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
		state->playerButtons[0][PLAYER_HAPPINESS],
		state->playerButtons[0][PLAYER_CONDITION],
		state->playerButtons[1][PLAYER_HAPPINESS],
		state->playerButtons[1][PLAYER_CONDITION],
		state->lists[0],
		state->lists[1],
		state->simulation[SIM_THREADS_LABEL],
		state->simulation[SIM_SEEDS_LABEL],
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
	RECT seedLabel = controlBounds(window, state->simulation[SIM_SEEDS_LABEL]);
	ASSERT_BOOL_EQ(seedLabel.right - seedLabel.left == scaled(state, 180), true);
	ASSERT_BOOL_EQ(seedLabel.right < seeds.left, true);
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

static void clickTeam(HWND window, WindowState *state, unsigned side, unsigned control) {
	ASSERT_BOOL_EQ(
		windowProcedure(
			window, WM_COMMAND, MAKEWPARAM(0, BN_CLICKED), (LPARAM)state->teamControls[side][control]
		) == 0,
		true
	);
}

static void checkTeamLayout(HWND window, WindowState *state, unsigned side) {
	RECT heading = controlBounds(window, state->headings[side]);
	RECT icon = controlBounds(window, state->teamControls[side][TEAM_ICON]);
	HDC dc = GetDC(window);
	ASSERT_BOOL_EQ(dc != NULL, true);
	if (dc) {
		HGDIOBJ old = SelectObject(dc, state->headingFont);
		TEXTMETRICW metrics = {0};
		ASSERT_BOOL_EQ(GetTextMetricsW(dc, &metrics), true);
		int offset = icon.top + icon.bottom - 2 * heading.top - metrics.tmHeight;
		ASSERT_BOOL_EQ(offset >= -1 && offset <= 1, true);
		SelectObject(dc, old);
		ASSERT_BOOL_EQ(ReleaseDC(window, dc), true);
	}
	ASSERT_BOOL_EQ(icon.left == heading.right, true);
	ASSERT_INT_EQ(icon.bottom - icon.top, scaled(state, 27));
	if (!state->editingTeam[side]) {
		return;
	}
	RECT input = controlBounds(window, state->teamControls[side][TEAM_INPUT]);
	RECT confirm = controlBounds(window, state->teamControls[side][TEAM_CONFIRM]);
	RECT cancel = controlBounds(window, state->teamControls[side][TEAM_CANCEL]);
	RECT error = controlBounds(window, state->teamControls[side][TEAM_ERROR]);
	RECT tabs = controlBounds(window, state->tabs);
	ASSERT_BOOL_EQ(input.left == heading.right && input.top == heading.top, true);
	ASSERT_BOOL_EQ(confirm.left > input.right && cancel.left > confirm.right, true);
	ASSERT_BOOL_EQ(error.left > cancel.right && error.right < tabs.right, true);
	ASSERT_BOOL_EQ(error.top < cancel.bottom, true);
}

static void
checkTeamHeading(HWND window, WindowState *state, unsigned side, const wchar_t *expected) {
	HDC dc = GetDC(window);
	ASSERT_BOOL_EQ(dc != NULL, true);
	HGDIOBJ old = SelectObject(dc, state->headingFont);
	SIZE extent = {0};
	ASSERT_BOOL_EQ(GetTextExtentPoint32W(dc, expected, (int)wcslen(expected), &extent), true);
	SelectObject(dc, old);
	ASSERT_BOOL_EQ(ReleaseDC(window, dc), true);
	RECT heading = controlBounds(window, state->headings[side]);
	RECT pitch = controlBounds(window, state->pitches[side]);
	RECT list = controlBounds(window, state->lists[side]);
	int width = extent.cx + scaled(state, 4);
	int maximum = list.right - pitch.left - scaled(state, 27 + PLAYER_BUTTON_COUNT * 126);
	ASSERT_INT_EQ(heading.right - heading.left, width < maximum ? width : maximum);
	if (width <= maximum) {
		wchar_t text[2 * FIXTURE_TEXT + 128] = {0};
		ASSERT_BOOL_EQ(GetWindowTextW(state->headings[side], text, 2 * FIXTURE_TEXT + 128) > 0, true);
		ASSERT_INT_EQ(wcscmp(text, expected), 0);
	}
	checkTeamLayout(window, state, side);
}

static void checkHeadingResize(HWND window, WindowState *state, unsigned side) {
	char tactic[FIXTURE_TEXT];
	strcpy(tactic, state->data->teams[side].tactic);
	memset(state->data->teams[side].tactic, 'W', 120);
	state->data->teams[side].tactic[120] = 0;
	ASSERT_BOOL_EQ(setHeadings(state), true);
	wchar_t expected[2 * FIXTURE_TEXT + 128];
	wchar_t wideTactic[FIXTURE_TEXT];
	wchar_t wideTeam[FIXTURE_TEXT];
	wchar_t wideFormation[FIXTURE_TEXT];
	ASSERT_BOOL_EQ(widen(state->data->teams[side].tactic, wideTactic, FIXTURE_TEXT), true);
	ASSERT_BOOL_EQ(widen(state->data->teams[side].name, wideTeam, FIXTURE_TEXT), true);
	ASSERT_BOOL_EQ(
		widen(
			state->data->teams[side].formation[0] ? state->data->teams[side].formation
																						: "Formation unknown",
			wideFormation,
			FIXTURE_TEXT
		),
		true
	);
	ASSERT_BOOL_EQ(
		swprintf(
			expected,
			2 * FIXTURE_TEXT + 128,
			L"%ls: %ls - %ls [%ls]",
			side ? L"Away" : L"Home",
			wideTeam,
			wideTactic,
			wideFormation
		) > 0,
		true
	);
	static const UINT resolutions[] = {96, 144};
	for (unsigned i = 0; i < sizeof resolutions / sizeof resolutions[0]; ++i) {
		state->dpi = resolutions[i];
		ASSERT_BOOL_EQ(setFonts(state), true);
		ASSERT_BOOL_EQ(SetWindowPos(window, NULL, 0, 0, 900, 900, SWP_NOMOVE | SWP_NOZORDER), true);
		layout(window, state);
		checkTeamHeading(window, state, side, expected);
		// Simulate text altered by the ellipsis control before expanding it.
		ASSERT_BOOL_EQ(SetWindowTextW(state->headings[side], L"Away..."), true);
		ASSERT_BOOL_EQ(SetWindowPos(window, NULL, 0, 0, 4000, 900, SWP_NOMOVE | SWP_NOZORDER), true);
		layout(window, state);
		checkTeamHeading(window, state, side, expected);
	}
	strcpy(state->data->teams[side].tactic, tactic);
	state->dpi = 96;
	ASSERT_BOOL_EQ(setFonts(state), true);
	ASSERT_BOOL_EQ(setHeadings(state), true);
	ASSERT_BOOL_EQ(SetWindowPos(window, NULL, 0, 0, 900, 600, SWP_NOMOVE | SWP_NOZORDER), true);
	layout(window, state);
}

static void checkTeamIcon(HWND window, WindowState *state, unsigned teamSide) {
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
		DRAWITEMSTRUCT draw = {.hwndItem = state->teamControls[teamSide][TEAM_ICON],
			.hDC = memory,
			.rcItem = {0, 0, side, side}};
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
		// Small sizes are mostly anti-aliased edges, so scale the coverage check with the icon.
		ASSERT_BOOL_EQ(blue > 0 && blended > 0 && blue + blended > (unsigned)(side * side / 10), true);
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
		unsigned drawn = 0;
		blue = 0;
		for (int y = 0; y < side; ++y) {
			for (int x = 0; x < side; ++x) {
				COLORREF pixel = GetPixel(memory, x, y);
				gray += pixel == GetSysColor(COLOR_GRAYTEXT);
				blue += pixel == RGB_BLUE;
				drawn += pixel != background && pixel != CLR_INVALID;
			}
		}
		ASSERT_BOOL_EQ(gray > 0 && blue == 0 && drawn > (unsigned)(side * side / 10), true);
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

static void finishTeamLoad(HWND window, WindowState *state) {
	ASSERT_BOOL_EQ(state->teamsThread != NULL, true);
	ASSERT_BOOL_EQ(WaitForSingleObject(state->teamsThread, 5000) == WAIT_OBJECT_0, true);
	ASSERT_BOOL_EQ(windowProcedure(window, WM_TIMER, TEAMS_TIMER, 0) == 0, true);
	ASSERT_BOOL_EQ(state->teamsThread == NULL, true);
}

// Types `text` into the side's picker and checks the listed labels, in order.
static void checkTeamFilter(
	HWND window,
	WindowState *state,
	unsigned side,
	const wchar_t *text,
	const wchar_t *const *expected,
	unsigned count
) {
	HWND picker = state->teamControls[side][TEAM_INPUT];
	wchar_t label[64];
	ASSERT_BOOL_EQ(SetWindowTextW(picker, text), true);
	ASSERT_BOOL_EQ(
		windowProcedure(window, WM_COMMAND, MAKEWPARAM(0, CBN_EDITCHANGE), (LPARAM)picker) == 0, true
	);
	ASSERT_BOOL_EQ(SendMessageW(picker, CB_GETCOUNT, 0, 0) == (LRESULT)count, true);
	for (unsigned i = 0; i < count; ++i) {
		ASSERT_BOOL_EQ(SendMessageW(picker, CB_GETLBTEXT, i, (LPARAM)label) > 0, true);
		ASSERT_INT_EQ(wcscmp(label, expected[i]), 0);
	}
	ASSERT_BOOL_EQ(GetWindowTextW(picker, label, 64) >= 0 && wcscmp(label, text) == 0, true);
}

static void checkTeamPicker(HWND window, WindowState *state, unsigned side) {
	ASSERT_BOOL_EQ(state->teamCount == 5 && !state->filteringTeams, true);
	// The list loads filtered by the current name: the home team (Aston Villa) is listed.
	HWND picker = state->teamControls[side][TEAM_INPUT];
	ASSERT_BOOL_EQ(SendMessageW(picker, CB_GETCOUNT, 0, 0) == (side ? 0 : 1), true);
	static const wchar_t *const all[] = {L"Aston Villa (12)",
		L"Luton Town (677)",
		L"Norwich City (691)",
		L"United (98)",
		L"United (99)"};
	checkTeamFilter(window, state, side, L"", all, 5);
	// Names starting with the text come first; IDs match too.
	static const wchar_t *const u[] = {L"United (98)", L"United (99)", L"Luton Town (677)"};
	checkTeamFilter(window, state, side, L"U", u, 3);
	static const wchar_t *const id[] = {L"Luton Town (677)"};
	checkTeamFilter(window, state, side, L"67", id, 1);
	checkTeamFilter(window, state, side, L"Bristol", NULL, 0);
	wchar_t picked[16];
	ASSERT_BOOL_EQ(pickedTeamId(state, L"United (99)", picked, 16), true);
	ASSERT_INT_EQ(wcscmp(picked, L"99"), 0);
	ASSERT_BOOL_EQ(pickedTeamId(state, L"United", picked, 16), false);
}

static void checkTeamEditor(HWND window, WindowState *state, unsigned side) {
	expectedSide = side;
	replacementInput = state->data;
	unsigned calls = replacementCalls;
	unsigned lists = teamLists;
	wchar_t originalTeam[FIXTURE_TEXT];
	wchar_t opponent[FIXTURE_TEXT];
	ASSERT_BOOL_EQ(widen(state->data->teams[side].name, originalTeam, FIXTURE_TEXT), true);
	ASSERT_BOOL_EQ(widen(state->data->teams[1 - side].name, opponent, FIXTURE_TEXT), true);
	const wchar_t *prefix = side ? L"Away: " : L"Home: ";
	const wchar_t *newTeam = side ? L"Luton Town" : L"Norwich City";
	const wchar_t *newHeading = side ? L"Away: Luton Town - New tactic [4-2-3-1]"
																	 : L"Home: Norwich City - New tactic [4-2-3-1]";
	wchar_t heading[2048] = {0};
	wchar_t text[2048] = {0};
	changeTab(window, state, 0);
	checkHeadingResize(window, state, side);
	layout(window, state);
	checkTeamLayout(window, state, side);
	checkTeamIcon(window, state, side);
	ASSERT_BOOL_EQ(GetWindowTextW(state->headings[side], heading, 2048) > 0, true);
	ASSERT_BOOL_EQ(wcsstr(heading, originalTeam) != NULL, true);
	checkTeamHeading(window, state, side, heading);
	ASSERT_BOOL_EQ(files_write(state->savedFilename, "original saved fixture", 22), true);
	wchar_t saved[FIXTURE_EXPORT_FILENAME_LENGTH];
	wcscpy(saved, state->savedFilename);
	const wchar_t *originalPath = state->path;
	teamListFailure = true;
	clickTeam(window, state, 1 - side, TEAM_ICON);
	ASSERT_BOOL_EQ(state->editingTeam[1 - side], true);
	clickTeam(window, state, side, TEAM_ICON);
	ASSERT_BOOL_EQ(state->editingTeam[side] && !state->editingTeam[1 - side], true);
	finishTeamLoad(window, state);
	ASSERT_BOOL_EQ(teamLists == lists + 1 && !state->teams && state->teamCount == 0, true);
	teamListFailure = false;
	ASSERT_BOOL_EQ(GetWindowTextW(state->headings[side], text, 2048) == 6, true);
	ASSERT_INT_EQ(wcscmp(text, prefix), 0);
	checkTeamHeading(window, state, side, prefix);
	ASSERT_BOOL_EQ(GetWindowTextW(state->teamControls[side][TEAM_INPUT], text, 2048) > 0, true);
	ASSERT_INT_EQ(wcscmp(text, originalTeam), 0);
	HWND teamEdit = FindWindowExW(state->teamControls[side][TEAM_INPUT], NULL, L"Edit", NULL);
	ASSERT_BOOL_EQ(SendMessageW(teamEdit, EM_GETLIMITTEXT, 0, 0) == 255, true);
	checkTeamLayout(window, state, side);
	state->dpi = 144;
	ASSERT_BOOL_EQ(SetWindowPos(window, NULL, 0, 0, 1350, 900, SWP_NOMOVE | SWP_NOZORDER), true);
	ASSERT_BOOL_EQ(setFonts(state), true);
	layout(window, state);
	checkTeamLayout(window, state, side);
	checkTeamHeading(window, state, side, prefix);
	state->dpi = 96;
	ASSERT_BOOL_EQ(SetWindowPos(window, NULL, 0, 0, 900, 600, SWP_NOMOVE | SWP_NOZORDER), true);
	ASSERT_BOOL_EQ(setFonts(state), true);
	layout(window, state);
	ASSERT_BOOL_EQ(SetWindowTextW(state->teamControls[side][TEAM_INPUT], L"Not submitted"), true);
	MSG key = {
		.hwnd = state->teamControls[side][TEAM_INPUT], .message = WM_KEYUP, .wParam = VK_ESCAPE};
	ASSERT_BOOL_EQ(!handleKey(window, state, &key) && state->editingTeam[side], true);
	key.message = WM_KEYDOWN;
	ASSERT_BOOL_EQ(handleKey(window, state, &key), true);
	ASSERT_BOOL_EQ(!state->editingTeam[side] && replacementCalls == calls, true);
	ASSERT_BOOL_EQ(
		GetWindowTextW(state->headings[side], text, 2048) > 0 && wcscmp(text, heading) == 0, true
	);
	checkTeamHeading(window, state, side, heading);
	ASSERT_INT_EQ(wcscmp(state->savedFilename, saved) == 0 && state->path == originalPath, true);
	clickTeam(window, state, side, TEAM_ICON);
	finishTeamLoad(window, state);
	ASSERT_BOOL_EQ(teamLists == lists + 2, true);
	checkTeamPicker(window, state, side);
	key.wParam = VK_RETURN;
	static const wchar_t *const invalid[] = {L"", L" \t", L"1\nteam=2"};
	for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		ASSERT_BOOL_EQ(SetWindowTextW(state->teamControls[side][TEAM_INPUT], invalid[i]), true);
		ASSERT_BOOL_EQ(handleKey(window, state, &key), true);
		ASSERT_BOOL_EQ(
			!state->replacementThread && replacementCalls == calls && state->editingTeam[side], true
		);
		ASSERT_BOOL_EQ(GetWindowTextLengthW(state->teamControls[side][TEAM_ERROR]) > 0, true);
	}
	ASSERT_BOOL_EQ(paths_absolute(saved, expectedInput, HARNESS_PATH_LENGTH), true);
	replacementRelease = CreateEventW(NULL, TRUE, FALSE, NULL);
	ASSERT_BOOL_EQ(replacementRelease != NULL, true);
	expectedTeam = opponent;
	ASSERT_BOOL_EQ(SetWindowTextW(state->teamControls[side][TEAM_INPUT], expectedTeam), true);
	replacementResult = 1;
	int errors = errorMessages;
	unsigned simulations = simulationCalls;
	clickTeam(window, state, side, TEAM_CONFIRM);
	ASSERT_BOOL_EQ(state->replacementThread != NULL, true);
	HANDLE worker = state->replacementThread;
	ASSERT_BOOL_EQ(!IsWindowEnabled(state->teamControls[side][TEAM_CONFIRM]), true);
	ASSERT_BOOL_EQ(!IsWindowEnabled(state->teamControls[side][TEAM_CANCEL]), true);
	ASSERT_BOOL_EQ(!IsWindowEnabled(state->simulation[SIM_SUBMIT]), true);
	ASSERT_BOOL_EQ(!IsWindowEnabled(state->teamControls[1 - side][TEAM_ICON]), true);
	clickTeam(window, state, 1 - side, TEAM_ICON);
	ASSERT_BOOL_EQ(!state->editingTeam[1 - side], true);
	clickTeam(window, state, side, TEAM_CONFIRM);
	clickTeam(window, state, side, TEAM_CANCEL);
	clickSimulate(window, state);
	ASSERT_BOOL_EQ(state->replacementThread == worker && state->editingTeam[side], true);
	ASSERT_BOOL_EQ(simulationCalls == simulations, true);
	ASSERT_BOOL_EQ(windowProcedure(window, WM_CLOSE, 0, 0), 0);
	ASSERT_BOOL_EQ(IsWindow(window) && errorMessages == ++errors, true);
	ASSERT_BOOL_EQ(SetEvent(replacementRelease), true);
	ASSERT_BOOL_EQ(WaitForSingleObject(worker, 5000) == WAIT_OBJECT_0, true);
	ASSERT_BOOL_EQ(windowProcedure(window, WM_TIMER, REPLACE_TIMER, 0) == 0, true);
	ASSERT_BOOL_EQ(
		replacementCalls == calls + 1 && !state->replacementThread && state->editingTeam[side], true
	);
	ASSERT_BOOL_EQ(GetWindowTextW(state->teamControls[side][TEAM_ERROR], text, 2048) > 0, true);
	ASSERT_INT_EQ(
		wcscmp(text, side ? L"error: that is the home team" : L"error: that is the away team"), 0
	);
	ASSERT_BOOL_EQ(widen(state->data->teams[side].name, text, 2048), true);
	ASSERT_INT_EQ(wcscmp(text, originalTeam), 0);
	ASSERT_INT_EQ(wcscmp(state->savedFilename, saved) == 0 && state->path == originalPath, true);
	ASSERT_BOOL_EQ(fileMissing(replacementOutput), true);
	HDC dc = GetDC(state->teamControls[side][TEAM_ERROR]);
	ASSERT_BOOL_EQ(
		windowProcedure(
			window, WM_CTLCOLORSTATIC, (WPARAM)dc, (LPARAM)state->teamControls[side][TEAM_ERROR]
		) == (LRESULT)GetSysColorBrush(COLOR_WINDOW),
		true
	);
	ASSERT_BOOL_EQ(GetTextColor(dc) == RGB(255, 0, 0), true);
	ASSERT_BOOL_EQ(ReleaseDC(state->teamControls[side][TEAM_ERROR], dc), true);
	ASSERT_BOOL_EQ(
		IsWindowEnabled(state->teamControls[side][TEAM_CONFIRM]) &&
			IsWindowEnabled(state->teamControls[side][TEAM_CANCEL]),
		true
	);
	checkTeamLayout(window, state, side);
	changeTab(window, state, 1);
	changeTab(window, state, 0);

	replacementResult = 0;
	replacementLoadFailure = true;
	expectedTeam = newTeam;
	ASSERT_BOOL_EQ(SetWindowTextW(state->teamControls[side][TEAM_INPUT], expectedTeam), true);
	clickTeam(window, state, side, TEAM_CONFIRM);
	ASSERT_BOOL_EQ(state->replacementThread != NULL, true);
	ASSERT_BOOL_EQ(WaitForSingleObject(state->replacementThread, 5000) == WAIT_OBJECT_0, true);
	checkReplacement(window, state);
	ASSERT_BOOL_EQ(state->editingTeam[side], true);
	ASSERT_BOOL_EQ(widen(state->data->teams[side].name, text, 2048), true);
	ASSERT_INT_EQ(wcscmp(text, originalTeam), 0);
	ASSERT_BOOL_EQ(GetWindowTextW(state->teamControls[side][TEAM_ERROR], text, 2048) > 0, true);
	ASSERT_INT_EQ(wcscmp(text, L"cannot read replacement fixture"), 0);
	ASSERT_BOOL_EQ(fileMissing(replacementOutput), true);
	replacementLoadFailure = false;
	for (unsigned i = 0; i < 2; ++i) {
		// The second is picked from the list: its ID is submitted.
		expectedTeam = i ? (side ? L"677" : L"691") : newTeam;
		const wchar_t *typed = i ? (side ? L"Luton Town (677)" : L"Norwich City (691)") : expectedTeam;
		ASSERT_BOOL_EQ(SetWindowTextW(state->teamControls[side][TEAM_INPUT], typed), true);
		if (i) {
			ASSERT_BOOL_EQ(paths_absolute(state->path, expectedInput, HARNESS_PATH_LENGTH), true);
		}
		wchar_t previous[HARNESS_PATH_LENGTH];
		wcscpy(previous, state->replacementPath);
		clickTeam(window, state, side, TEAM_CONFIRM);
		ASSERT_BOOL_EQ(state->replacementThread != NULL, true);
		if (i) {
			changeTab(window, state, 1);
		}
		ASSERT_BOOL_EQ(WaitForSingleObject(state->replacementThread, 5000) == WAIT_OBJECT_0, true);
		checkReplacement(window, state);
		ASSERT_BOOL_EQ(
			!state->replacementThread && !state->editingTeam[side] && state->temporary, true
		);
		ASSERT_BOOL_EQ(!state->savedFilename[0] && !state->missing, true);
		ASSERT_INT_EQ(wcscmp(state->path, replacementOutput), 0);
		if (previous[0]) {
			ASSERT_BOOL_EQ(fileMissing(previous), true);
		}
		checkContents(state->path, "{\"replacement\":true}");
		checkContents(saved, "original saved fixture");
		firstLine(state->metadata, L"This fixture object is not saved. Click here to export it");
		changeTab(window, state, 0);
		ASSERT_BOOL_EQ(GetWindowTextW(state->headings[side], text, 2048) > 0, true);
		ASSERT_INT_EQ(wcscmp(text, newHeading), 0);
		checkTeamHeading(window, state, side, newHeading);
		ASSERT_BOOL_EQ(widen(state->data->teams[1 - side].name, text, 2048), true);
		ASSERT_INT_EQ(wcscmp(text, opponent), 0);
		ASSERT_BOOL_EQ(ListView_GetItemCount(state->lists[side]) == FIXTURE_SLOTS + 1, true);
		ASSERT_BOOL_EQ(
			GetWindowLongPtrW(state->pitches[side], GWLP_USERDATA) == (LONG_PTR)&state->data->teams[side],
			true
		);
		ASSERT_BOOL_EQ(IsWindowEnabled(state->simulation[SIM_SUBMIT]), true);
		clickTeam(window, state, side, TEAM_ICON);
		clickTeam(window, state, side, TEAM_CANCEL);
		ASSERT_BOOL_EQ(GetWindowTextW(state->headings[side], text, 2048) > 0, true);
		ASSERT_INT_EQ(wcscmp(text, newHeading), 0);
		checkTeamHeading(window, state, side, newHeading);
		if (!i) {
			clickTeam(window, state, side, TEAM_ICON);
		}
	}
	ASSERT_BOOL_EQ(
		replacementCalls == calls + 4 && teamLists == lists + 2 && !state->teamsThread, true
	);
	for (unsigned exported = 0; exported < 2; ++exported) {
		if (exported) {
			exportFixture(window, state);
			ASSERT_BOOL_EQ(!state->temporary, true);
			wchar_t expectedFilename[FIXTURE_EXPORT_FILENAME_LENGTH];
			ASSERT_BOOL_EQ(
				fixtureExport_filename(
					state->data, 12345, expectedFilename, FIXTURE_EXPORT_FILENAME_LENGTH
				),
				true
			);
			ASSERT_INT_EQ(wcscmp(state->savedFilename, expectedFilename), 0);
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
		ASSERT_BOOL_EQ(!IsWindowEnabled(state->teamControls[side][TEAM_ICON]), true);
		clickTeam(window, state, side, TEAM_ICON);
		ASSERT_BOOL_EQ(!state->editingTeam[side], true);
		ASSERT_BOOL_EQ(WaitForSingleObject(state->simulationThread, 5000) == WAIT_OBJECT_0, true);
		checkSimulation(window, state);
		ASSERT_BOOL_EQ(IsWindowEnabled(state->teamControls[side][TEAM_ICON]), true);
	}
	ASSERT_BOOL_EQ(errorMessages == errors, true);
	ASSERT_BOOL_EQ(CloseHandle(replacementRelease), true);
	ASSERT_BOOL_EQ(DeleteFileW(saved), true);
	ASSERT_BOOL_EQ(DeleteFileW(state->savedFilename), true);
	ASSERT_BOOL_EQ(DeleteFileW(state->replacementPath), true);
	state->replacementPath[0] = 0;
	replacementInput = NULL;
	freeTeams(state->teams, state->teamCount);
	state->teams = NULL;
	state->teamCount = 0;
}

static void checkFonts(HWND window, WindowState *state) {
	static const UINT resolutions[] = {96, 120, 144, 192};
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
			state->metadataHeight = scaled(state, 110);
			layout(window, state);
			checkFileButtonLayout(window, state);
			checkTeamLayout(window, state, 0);
			checkTeamLayout(window, state, 1);
		}
	}
	calibriInstalled = false;
	state->dpi = 96;
	state->metadataHeight = scaled(state, 110);
	ASSERT_BOOL_EQ(setFonts(state), true);
}

static void checkVenueForm(HWND window, WindowState *state) {
	int errors = errorMessages;
	for (unsigned i = 0; i < VENUE_FIELD_COUNT; ++i) {
		ASSERT_BOOL_EQ(IsWindowEnabled(state->venueInputs[i]), false);
	}
	const uint32_t initial[] = {
		1116156723,
		1121071923,
		3211914117,
		1112666544,
		42000,
		40000,
		30000,
		2000,
		30000,
		8,
		200,
	};
	for (unsigned i = 0; i < VENUE_FIELD_COUNT; ++i) {
		state->data->venueValues[i] = (FixtureNumber){.value = initial[i], .known = true};
	}
	ASSERT_BOOL_EQ(setVenue(state), true);
	changeTab(window, state, 1);
	HWND surface = state->venueInputs[VENUE_SURFACE];
	ASSERT_INT_EQ((int)SendMessageW(surface, CB_GETCOUNT, 0, 0), 2);
	ASSERT_INT_EQ((int)SendMessageW(surface, CB_GETCURSEL, 0, 0), 1);
	ASSERT_INT_EQ((int)SendMessageW(surface, CB_GETITEMDATA, 0, 0), 1);
	ASSERT_INT_EQ((int)SendMessageW(surface, CB_GETITEMDATA, 1, 0), 8);
	FixtureNumber values[VENUE_FIELD_COUNT];
	ASSERT_BOOL_EQ(readVenue(window, state, values), true);
	for (unsigned i = 0; i < VENUE_FIELD_COUNT; ++i) {
		ASSERT_BOOL_EQ(values[i].value == initial[i], true);
		ASSERT_BOOL_EQ(IsWindowEnabled(state->venueInputs[i]), true);
		RECT label = controlBounds(window, state->venueLabels[i]);
		RECT input = controlBounds(window, state->venueInputs[i]);
		ASSERT_BOOL_EQ(label.right < input.left && input.top <= label.top, true);
		ASSERT_BOOL_EQ(
			(HFONT)SendMessageW(state->venueInputs[i], WM_GETFONT, 0, 0) == state->font, true
		);
	}
	static const struct {
		unsigned field;
		const wchar_t *text;
	} invalid[] = {
		{VENUE_WIDTH, L""},
		{VENUE_WIDTH, L"nan"},
		{VENUE_LENGTH, L"0"},
		{VENUE_LONGITUDE, L"-181"},
		{VENUE_LATITUDE, L"91"},
		{VENUE_CAPACITY, L"4294967296"},
		{VENUE_HOME_FANS, L"-1"},
		{VENUE_HOME_FANS, L"1.5"},
		{VENUE_HOME_FANS, L"41000"},
		{VENUE_SEATS, L"42001"},
		{VENUE_CONDITION, L"256"},
	};
	for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		ASSERT_BOOL_EQ(setVenue(state), true);
		ASSERT_BOOL_EQ(SetWindowTextW(state->venueInputs[invalid[i].field], invalid[i].text), true);
		ASSERT_BOOL_EQ(readVenue(window, state, values), false);
		ASSERT_INT_EQ(errorMessages, ++errors);
	}
	ASSERT_BOOL_EQ(setVenue(state), true);
	SendMessageW(surface, CB_SETCURSEL, (WPARAM)-1, 0);
	ASSERT_BOOL_EQ(readVenue(window, state, values), false);
	ASSERT_INT_EQ(errorMessages, ++errors);
	ASSERT_BOOL_EQ(setVenue(state), true);
	SetWindowTextW(state->venueInputs[VENUE_HOME_FANS], L"40000");
	SetWindowTextW(state->venueInputs[VENUE_CONDITION], L"255");
	SetWindowTextW(state->venueInputs[VENUE_WIDTH], L"72.5");
	SetWindowTextW(state->venueInputs[VENUE_LONGITUDE], L"-1.89");
	SendMessageW(surface, CB_SETCURSEL, 0, 0);
	ASSERT_BOOL_EQ(readVenue(window, state, values), true);
	ASSERT_BOOL_EQ(values[VENUE_WIDTH].value == fixtureData_fromFloat(72.5f), true);
	ASSERT_BOOL_EQ(values[VENUE_LONGITUDE].value == fixtureData_fromFloat(-1.89f), true);
	ASSERT_BOOL_EQ(
		values[VENUE_HOME_FANS].value == 40000 && values[VENUE_CONDITION].value == 255, true
	);
	ASSERT_BOOL_EQ(values[VENUE_SURFACE].value == 1, true);
	windowProcedure(
		window, WM_COMMAND, MAKEWPARAM(0, EN_CHANGE), (LPARAM)state->venueInputs[VENUE_WIDTH]
	);
	ASSERT_BOOL_EQ(state->venueDirty, true);
	firstLine(state->metadata, L"Viewing fixture: temp-fixture.json (unsaved venue edits)");
	unsigned prompts = discardPrompts;
	discardAnswer = IDNO;
	ASSERT_BOOL_EQ(closeBlocked(window, state), true);
	discardAnswer = IDYES;
	ASSERT_BOOL_EQ(closeBlocked(window, state), false);
	ASSERT_BOOL_EQ(discardPrompts == prompts + 2, true);
	discardAnswer = IDNO;
	enableVenue(state, FALSE);
	for (unsigned i = 0; i < VENUE_FIELD_COUNT; ++i) {
		ASSERT_BOOL_EQ(IsWindowEnabled(state->venueInputs[i]), false);
	}
	enableVenue(state, TRUE);
	SetWindowTextW(state->venueInputs[VENUE_SEATS], L"42001");
	ASSERT_BOOL_EQ(startFileJob(window, state, L"venue.json", true, false), false);
	ASSERT_BOOL_EQ(state->fileThread == NULL && state->venueDirty, true);
	ASSERT_INT_EQ(errorMessages, ++errors);
	clickSimulate(window, state);
	ASSERT_BOOL_EQ(state->simulationThread == NULL && state->venueDirty, true);
	ASSERT_INT_EQ(errorMessages, ++errors);
	memset(state->data->venueValues, 0, sizeof state->data->venueValues);
	ASSERT_BOOL_EQ(setVenue(state), true);
}

static void checkPlayerControls(HWND window, WindowState *state) {
	FixtureData *original = malloc(sizeof *original);
	ASSERT_BOOL_EQ(original != NULL, true);
	if (!original) {
		return;
	}
	*original = *state->data;
	ByteBuffer json = {0};
	fixtureTest_config(&json);
	char error[256];
	ASSERT_BOOL_EQ(
		fixtureData_read(buffer_string(&json), json.length, state->data, error, sizeof error), true
	);
	buffer_free(&json);
	state->data->teams[0].players[1].name[0] = 0;
	for (unsigned side = 0; side < 2; ++side) {
		ASSERT_BOOL_EQ(fillList(state, side), true);
		HWND list = state->lists[side];
		wchar_t text[32];
		LVCOLUMNW column = {.mask = LVCF_TEXT, .pszText = text, .cchTextMax = 32};
		ASSERT_BOOL_EQ(SendMessageW(list, LVM_GETCOLUMNW, 0, (LPARAM)&column), true);
		ASSERT_INT_EQ(wcscmp(text, L"Select"), 0);
		for (unsigned button = 0; button < PLAYER_BUTTON_COUNT; ++button) {
			ASSERT_BOOL_EQ(IsWindowEnabled(state->playerButtons[side][button]), false);
			RECT heading = controlBounds(window, state->headings[side]);
			RECT bounds = controlBounds(window, state->playerButtons[side][button]);
			ASSERT_INT_EQ(bounds.top, heading.top);
			ASSERT_BOOL_EQ(bounds.left > heading.right, true);
		}
	}
	ASSERT_INT_EQ(ListView_GetItemCount(state->lists[0]), 12);
	ASSERT_INT_EQ(ListView_GetItemCount(state->lists[1]), 12 + FIXTURE_SLOTS);
	ASSERT_BOOL_EQ(ListView_GetItemState(state->lists[0], 1, LVIS_STATEIMAGEMASK) == 0, true);
	ASSERT_BOOL_EQ(ListView_GetItemState(state->lists[1], 0, LVIS_STATEIMAGEMASK) == 0, true);
	NMLISTVIEW unknown = {.hdr = {.hwndFrom = state->lists[1], .code = (UINT)LVN_ITEMCHANGING},
		.iItem = 0,
		.uChanged = LVIF_STATE,
		.uNewState = INDEXTOSTATEIMAGEMASK(2)};
	ASSERT_BOOL_EQ(windowProcedure(window, WM_NOTIFY, 0, (LPARAM)&unknown) == TRUE, true);
	for (unsigned side = 0; side < 2; ++side) {
		unsigned row = side ? FIXTURE_SLOTS : 0;
		ASSERT_BOOL_EQ(ListView_GetCheckState(state->lists[side], row), false);
		ListView_SetCheckState(state->lists[side], row, TRUE);
		NMLISTVIEW selected = {.hdr = {.hwndFrom = state->lists[side], .code = (UINT)LVN_ITEMCHANGED},
			.iItem = (int)row,
			.uChanged = LVIF_STATE,
			.uOldState = INDEXTOSTATEIMAGEMASK(1),
			.uNewState = INDEXTOSTATEIMAGEMASK(2),
			.lParam = 1};
		ASSERT_BOOL_EQ(windowProcedure(window, WM_NOTIFY, 0, (LPARAM)&selected) == 0, true);
		ASSERT_BOOL_EQ(state->selectedPlayers[side][0], true);
		ASSERT_BOOL_EQ(IsWindowEnabled(state->playerButtons[side][0]), true);
		if (!side) {
			ASSERT_BOOL_EQ(IsWindowEnabled(state->playerButtons[1][0]), false);
		}
	}
	state->selectedPlayers[0][11] = true;
	ASSERT_BOOL_EQ(fillList(state, 0), true);
	ASSERT_BOOL_EQ(ListView_EnsureVisible(state->lists[0], 11, FALSE), true);
	int firstVisible = ListView_GetTopIndex(state->lists[0]);
	for (unsigned button = 0; button < PLAYER_BUTTON_COUNT; ++button) {
		ASSERT_BOOL_EQ(
			windowProcedure(
				window, WM_COMMAND, MAKEWPARAM(0, BN_CLICKED), (LPARAM)state->playerButtons[0][button]
			) == 0,
			true
		);
	}
	for (unsigned i = 0; i < 12; ++i) {
		FixturePlayer *player = &state->data->teams[0].players[i];
		bool changed = i == 0 || i == 11;
		ASSERT_BOOL_EQ(player->morale.value == (changed ? 20U : 12U), true);
		ASSERT_BOOL_EQ(player->condition.value == (changed ? 10000U : 9876U), true);
		ASSERT_BOOL_EQ(player->sharpness[0].value == (changed ? 10000U : 8125U), true);
		ASSERT_BOOL_EQ(player->sharpness[1].value == (changed ? 10000U : 5650U), true);
		ASSERT_BOOL_EQ(player->fatigue.value == (changed ? 0U : 65535U), true);
		ASSERT_BOOL_EQ(
			state->playerEdits.players[0][i] ==
				(changed ? FIXTURE_MAX_HAPPINESS | FIXTURE_MAX_CONDITION : 0),
			true
		);
	}
	ASSERT_BOOL_EQ(state->data->teams[1].players[0].morale.value == 12, true);
	ASSERT_BOOL_EQ(ListView_GetCheckState(state->lists[0], 0), true);
	ASSERT_BOOL_EQ(ListView_GetCheckState(state->lists[0], 11), true);
	ASSERT_INT_EQ(ListView_GetTopIndex(state->lists[0]), firstVisible);
	static const wchar_t *const expected[] = {L"20", L"100.00%", L"100.00%", L"0"};
	for (unsigned i = 0; i < 4; ++i) {
		wchar_t text[64];
		LVITEMW cell = {.iSubItem = (int)i + 6, .pszText = text, .cchTextMax = 64};
		ASSERT_BOOL_EQ(SendMessageW(state->lists[0], LVM_GETITEMTEXTW, 11, (LPARAM)&cell) > 0, true);
		ASSERT_INT_EQ(wcscmp(text, expected[i]), 0);
	}
	ASSERT_BOOL_EQ(
		windowProcedure(
			window,
			WM_COMMAND,
			MAKEWPARAM(0, BN_CLICKED),
			(LPARAM)state->playerButtons[1][PLAYER_HAPPINESS]
		) == 0,
		true
	);
	ASSERT_BOOL_EQ(state->data->teams[1].players[0].morale.value == 20, true);
	ASSERT_BOOL_EQ(state->data->teams[1].players[1].morale.value == 12, true);
	firstLine(state->metadata, L"Viewing fixture: temp-fixture.json (unsaved player edits)");
	changeTab(window, state, 1);
	changeTab(window, state, 0);
	ASSERT_BOOL_EQ(ListView_GetCheckState(state->lists[0], 0), true);
	NMLISTVIEW deselected = {.hdr = {.hwndFrom = state->lists[1], .code = (UINT)LVN_ITEMCHANGED},
		.iItem = FIXTURE_SLOTS,
		.uChanged = LVIF_STATE,
		.uOldState = INDEXTOSTATEIMAGEMASK(2),
		.uNewState = INDEXTOSTATEIMAGEMASK(1),
		.lParam = 1};
	ListView_SetCheckState(state->lists[1], FIXTURE_SLOTS, FALSE);
	ASSERT_BOOL_EQ(windowProcedure(window, WM_NOTIFY, 0, (LPARAM)&deselected) == 0, true);
	ASSERT_BOOL_EQ(IsWindowEnabled(state->playerButtons[1][0]), false);
	ASSERT_BOOL_EQ(IsWindowEnabled(state->playerButtons[0][0]), true);
	state->fileThread = GetCurrentThread();
	enablePlayers(state);
	ASSERT_BOOL_EQ(IsWindowEnabled(state->lists[0]), false);
	ASSERT_BOOL_EQ(IsWindowEnabled(state->playerButtons[0][0]), false);
	state->fileThread = NULL;
	enablePlayers(state);
	ASSERT_BOOL_EQ(IsWindowEnabled(state->playerButtons[0][0]), true);
	discardAnswer = IDNO;
	ASSERT_BOOL_EQ(closeBlocked(window, state), true);
	discardAnswer = IDYES;
	ASSERT_BOOL_EQ(closeBlocked(window, state), false);
	discardAnswer = IDNO;
	*state->data = *original;
	free(original);
	memset(&state->playerEdits, 0, sizeof state->playerEdits);
	memset(state->selectedPlayers, 0, sizeof state->selectedPlayers);
	ASSERT_BOOL_EQ(setMetadata(state) && fillList(state, 0) && fillList(state, 1), true);
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
	checkNotes(state.metadata);
	layout(window, &state);
	for (unsigned side = 0; side < 2; ++side) {
		RECT heading = controlBounds(window, state.headings[side]);
		RECT pitch = controlBounds(window, state.pitches[side]);
		RECT list = controlBounds(window, state.lists[side]);
		ASSERT_INT_EQ(pitch.top, heading.bottom);
		ASSERT_INT_EQ(list.top, heading.bottom);
	}
	checkFonts(window, &state);
	checkPlayerControls(window, &state);
	checkVenueForm(window, &state);
	static const wchar_t *const fileLabels[] = {L"Open...", L"Save"};
	for (unsigned i = 0; i < 2; ++i) {
		wchar_t label[16] = {0};
		ASSERT_BOOL_EQ(GetWindowTextW(state.fileButtons[i], label, 16) > 0, true);
		ASSERT_INT_EQ(wcscmp(label, fileLabels[i]), 0);
		ASSERT_BOOL_EQ(GetWindowLongW(state.fileButtons[i], GWL_STYLE) & WS_TABSTOP, true);
		ASSERT_BOOL_EQ(IsWindowEnabled(state.fileButtons[i]), true);
	}
	state.taskbarCreated = RegisterWindowMessageW(L"TaskbarButtonCreated");
	ASSERT_BOOL_EQ(state.taskbarCreated != 0 && state.taskbar == NULL && taskbarCreates == 0, true);
	ASSERT_BOOL_EQ(TabCtrl_GetItemCount(state.tabs) == 3, true);
	wchar_t text[256] = {0};
	TCITEMW tab = {.mask = TCIF_TEXT, .pszText = text, .cchTextMax = 256};
	ASSERT_BOOL_EQ(SendMessageW(state.tabs, TCM_GETITEMW, 0, (LPARAM)&tab), true);
	ASSERT_INT_EQ(wcscmp(text, L"Squads"), 0);
	ASSERT_BOOL_EQ(SendMessageW(state.tabs, TCM_GETITEMW, 1, (LPARAM)&tab), true);
	ASSERT_INT_EQ(wcscmp(text, L"Venue"), 0);
	ASSERT_BOOL_EQ(SendMessageW(state.tabs, TCM_GETITEMW, 2, (LPARAM)&tab), true);
	ASSERT_INT_EQ(wcscmp(text, L"Simulation"), 0);
	int minimum = 0;
	int maximum = 0;
	SendMessageW(state.simulation[SIM_SPINNER], UDM_GETRANGE32, (WPARAM)&minimum, (LPARAM)&maximum);
	ASSERT_BOOL_EQ(minimum == 1 && maximum == 7, true);
	ASSERT_BOOL_EQ(GetWindowTextW(state.simulation[SIM_THREADS], text, 256) == 1, true);
	ASSERT_INT_EQ(wcscmp(text, L"7"), 0);
	ASSERT_BOOL_EQ(GetWindowTextW(state.simulation[SIM_SEEDS_LABEL], text, 256) > 0, true);
	ASSERT_INT_EQ(wcscmp(text, L"Seed pattern (optional):"), 0);
	ASSERT_BOOL_EQ(GetWindowTextW(state.simulation[SIM_SEEDS], text, 256) == 0, true);
	ASSERT_INT_EQ(wcscmp(text, L""), 0);
	ASSERT_BOOL_EQ(
		SendMessageW(state.simulation[SIM_DETAILED], BM_GETCHECK, 0, 0) == BST_UNCHECKED, true
	);
	ASSERT_BOOL_EQ(SendMessageW(state.simulation[SIM_SEEDS], EM_GETLIMITTEXT, 0, 0) == 255, true);
	ASSERT_BOOL_EQ(GetWindowTextLengthW(state.simulation[SIM_SUMMARY]) == 0, true);
	ASSERT_BOOL_EQ(!controlVisible(state.progress), true);
	ASSERT_BOOL_EQ(GetWindowLongW(state.tabs, GWL_STYLE) & WS_CLIPSIBLINGS, true);
	checkControlColors(window, state.headings[0], WM_CTLCOLORSTATIC);
	checkControlColors(window, state.simulation[SIM_THREADS_LABEL], WM_CTLCOLORSTATIC);
	checkControlColors(window, state.simulation[SIM_DETAILED], WM_CTLCOLORBTN);
	changeTab(window, &state, 0);
	changeTab(window, &state, 1);
	changeTab(window, &state, 2);
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
	static const wchar_t *const invalidSeeds[] = {L"abc", L"1\nthreads=7", L"1;2"};
	for (unsigned i = 0; i < sizeof invalidSeeds / sizeof invalidSeeds[0]; ++i) {
		ASSERT_BOOL_EQ(SetWindowTextW(state.simulation[SIM_SEEDS], invalidSeeds[i]), true);
		clickSimulate(window, &state);
		ASSERT_BOOL_EQ(state.simulationThread == NULL && errorMessages == ++errors, true);
	}
	ASSERT_BOOL_EQ(simulationCalls == 0, true);
	simulationRelease = CreateEventW(NULL, TRUE, FALSE, NULL);
	ASSERT_BOOL_EQ(simulationRelease != NULL, true);
	expectedProgressWindow = window;
	static const wchar_t *const patterns[] = {
		L"1", L"42", L"1-1000", L"1,3,5", L"1,4,7-70", L"keep", L""};
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
		expectedSeeds = patterns[i][0] ? patterns[i] : L"keep";
		expectedThreads = i % 2 ? L"7" : L"1";
		expectedDetailed = i % 2 != 0;
		simulationResult = i == 5 ? 1 : 0;
		ASSERT_BOOL_EQ(SetWindowTextW(state.simulation[SIM_SEEDS], patterns[i]), true);
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
		changeTab(window, &state, 2);
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
		changeTab(window, &state, 2);
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
		ASSERT_BOOL_EQ(
			GetWindowTextW(state.simulation[SIM_SEEDS], text, 256) == (int)wcslen(patterns[i]), true
		);
		ASSERT_INT_EQ(wcscmp(text, patterns[i]), 0);
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
	checkTeamEditor(window, &state, 1);
	checkTeamEditor(window, &state, 0);
	static const char venueFixture[] = "{\"format\":\"fm24-match-config/1\",\"fields\":["
																		 "{\"name\":\"venue.pitch.width\",\"u32\":1116156723},"
																		 "{\"name\":\"venue.pitch.half_width\",\"u32\":0}]}";
	wchar_t venuePath[HARNESS_PATH_LENGTH];
	ASSERT_BOOL_EQ(paths_absolute(L"venue-simulation.json", venuePath, HARNESS_PATH_LENGTH), true);
	ASSERT_BOOL_EQ(files_write(venuePath, venueFixture, sizeof venueFixture - 1), true);
	SimulationJob venueJob = {.progressWindow = (uintptr_t)window};
	wcscpy(venueJob.input, venuePath);
	wcscpy(venueJob.seeds, L"keep");
	wcscpy(venueJob.threads, L"1");
	venueJob.venueValues[VENUE_WIDTH] =
		(FixtureNumber){.value = fixtureData_fromFloat(72.5f), .known = true};
	wcscpy(expectedInput, venuePath);
	expectedSeeds = L"keep";
	expectedThreads = L"1";
	expectedDetailed = false;
	expectedVenueInput = true;
	simulationResult = 0;
	ASSERT_BOOL_EQ(SetEvent(simulationRelease), true);
	ASSERT_INT_EQ((int)simulateFixture(&venueJob), 0);
	ASSERT_BOOL_EQ(fileMissing(simulationTemporary), true);
	checkContents(venuePath, venueFixture);
	expectedVenueInput = false;
	ByteBuffer squad = {0};
	fixtureTest_config(&squad);
	ASSERT_BOOL_EQ(files_write(venuePath, squad.data, squad.length), true);
	memset(venueJob.venueValues, 0, sizeof venueJob.venueValues);
	venueJob.playerEdits.players[0][0] = FIXTURE_MAX_HAPPINESS;
	venueJob.playerEdits.players[1][11] = FIXTURE_MAX_CONDITION;
	expectedPlayerInput = true;
	ASSERT_INT_EQ((int)simulateFixture(&venueJob), 0);
	ASSERT_BOOL_EQ(fileMissing(simulationTemporary), true);
	checkContents(venuePath, buffer_string(&squad));
	expectedPlayerInput = false;
	buffer_free(&squad);
	ASSERT_BOOL_EQ(DeleteFileW(venuePath), true);
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
	// WM_QUIT is only retrieved once no other posted messages are queued, and the system posts
	// some of its own (e.g. WM_DWMNCRENDERINGCHANGED) to the test's windows, so drain those first.
	MSG quit = {0};
	while (PeekMessageW(&quit, NULL, 0, 0, PM_REMOVE) && quit.message != WM_QUIT) {
	}
	ASSERT_BOOL_EQ(quit.message == WM_QUIT, true);
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

static void clickFile(HWND window, WindowState *state, bool save) {
	dialogSave = save;
	ASSERT_BOOL_EQ(
		windowProcedure(
			window,
			WM_COMMAND,
			MAKEWPARAM(0, BN_CLICKED),
			(LPARAM)state->fileButtons[save ? FILE_SAVE_BUTTON : FILE_OPEN_BUTTON]
		) == 0,
		true
	);
}

static void finishFile(HWND window, WindowState *state) {
	ASSERT_BOOL_EQ(state->fileThread != NULL, true);
	if (!state->fileThread) {
		return;
	}
	ASSERT_BOOL_EQ(SetEvent(fileRelease), true);
	ASSERT_BOOL_EQ(WaitForSingleObject(state->fileThread, 5000) == WAIT_OBJECT_0, true);
	ASSERT_BOOL_EQ(windowProcedure(window, WM_TIMER, FILE_TIMER, 0) == 0, true);
	ASSERT_BOOL_EQ(state->fileThread == NULL && state->fileJob.data == NULL, true);
	ASSERT_BOOL_EQ(ResetEvent(fileRelease), true);
	for (unsigned i = 0; i < FILE_CONTROL_COUNT; ++i) {
		ASSERT_BOOL_EQ(IsWindowEnabled(state->fileButtons[i]), true);
	}
}

static void checkFileOperations(FixtureData *data) {
	HINSTANCE instance = GetModuleHandleW(NULL);
	WNDCLASSW definition = {.lpfnWndProc = pitchProcedure,
		.hInstance = instance,
		.cbWndExtra = sizeof(HFONT),
		.lpszClassName = pitchClass};
	ASSERT_BOOL_EQ(RegisterClassW(&definition), true);
	HWND window =
		CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 900, 600, NULL, NULL, instance, NULL);
	ASSERT_BOOL_EQ(window != NULL, true);
	wchar_t source[HARNESS_PATH_LENGTH];
	wchar_t json[HARNESS_PATH_LENGTH];
	wchar_t binary[HARNESS_PATH_LENGTH];
	ASSERT_BOOL_EQ(paths_absolute(L"source.json", source, HARNESS_PATH_LENGTH), true);
	ASSERT_BOOL_EQ(paths_absolute(L"saved fixture.json", json, HARNESS_PATH_LENGTH), true);
	ASSERT_BOOL_EQ(paths_absolute(L"saved fixture.bin", binary, HARNESS_PATH_LENGTH), true);
	ASSERT_BOOL_EQ(files_write(source, "{\"fixture\":true}", 16), true);
	WindowState state = {
		.data = data, .path = source, .filename = L"source.json", .dpi = 96, .metadataHeight = 110};
	SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)&state);
	ASSERT_BOOL_EQ(createControls(window, &state), true);
	activeWindow = window;
	dialogOwner = window;
	fileRelease = CreateEventW(NULL, TRUE, FALSE, NULL);
	ASSERT_BOOL_EQ(fileRelease != NULL, true);
	int errors = errorMessages;
	unsigned creates = dialogCreates;
	unsigned releases = dialogReleases;
	unsigned shells = shellReleases;
	unsigned discards = discardPrompts;

	// Cancelling and setup failures must leave the current source untouched.
	dialogShowResult = HRESULT_FROM_WIN32(ERROR_CANCELLED);
	clickFile(window, &state, false);
	ASSERT_BOOL_EQ(state.fileThread == NULL && state.path == source && errorMessages == errors, true);
	ASSERT_BOOL_EQ(dialogCreates == creates + 1 && dialogReleases == releases + 1, true);
	ASSERT_BOOL_EQ(shellReleases == shells + 1, true);
	dialogCreateResult = E_FAIL;
	clickFile(window, &state, false);
	ASSERT_BOOL_EQ(
		state.fileThread == NULL && state.path == source && errorMessages == ++errors, true
	);
	dialogCreateResult = S_OK;
	dialogShowResult = S_OK;
	dialogPath = L"C:\\unsupported.txt";
	clickFile(window, &state, false);
	ASSERT_BOOL_EQ(state.fileThread == NULL && errorMessages == ++errors, true);

	state.temporary = true;
	ASSERT_BOOL_EQ(setMetadata(&state), true);
	dialogPath = source;
	fileLoadPath = source;
	discardAnswer = IDNO;
	clickFile(window, &state, false);
	ASSERT_BOOL_EQ(
		state.fileThread == NULL && state.temporary && discardPrompts == discards + 1, true
	);
	discardAnswer = IDYES;
	fileFailure = true;
	clickFile(window, &state, false);
	ASSERT_BOOL_EQ(state.fileThread != NULL && discardPrompts == discards + 2, true);
	ASSERT_BOOL_EQ(state.path == source && state.temporary, true);
	for (unsigned i = 0; i < FILE_CONTROL_COUNT; ++i) {
		ASSERT_BOOL_EQ(!IsWindowEnabled(state.fileButtons[i]), true);
	}
	ASSERT_BOOL_EQ(!IsWindowEnabled(state.simulation[SIM_SUBMIT]), true);
	clickSimulate(window, &state);
	ASSERT_BOOL_EQ(state.simulationThread == NULL, true);
	ASSERT_BOOL_EQ(windowProcedure(window, WM_CLOSE, 0, 0) == 0, true);
	ASSERT_BOOL_EQ(IsWindow(window) && errorMessages == ++errors, true);
	finishFile(window, &state);
	ASSERT_BOOL_EQ(state.path == source && state.temporary && errorMessages == ++errors, true);
	firstLine(state.metadata, L"This fixture object is not saved. Click here to export it");

	fileFailure = false;
	SetWindowTextW(state.simulation[SIM_SUMMARY], L"Old result");
	clickFile(window, &state, false);
	finishFile(window, &state);
	ASSERT_BOOL_EQ(!state.temporary && !state.missing && errorMessages == errors, true);
	ASSERT_INT_EQ(wcscmp(state.savedFilename, source), 0);
	ASSERT_INT_EQ(wcscmp(state.filename, L"source.json"), 0);
	ASSERT_INT_EQ(strcmp(state.data->teams[0].name, "Opened home"), 0);
	ASSERT_INT_EQ(strcmp(state.data->teams[1].name, "Opened away"), 0);
	ASSERT_INT_EQ(GetWindowTextLengthW(state.simulation[SIM_SUMMARY]), 0);
	ASSERT_BOOL_EQ(discardPrompts == discards + 3 && fileLoads == 2, true);

	dialogFilename = L"source.json";
	dialogPath = json;
	dialogShowResult = HRESULT_FROM_WIN32(ERROR_CANCELLED);
	clickFile(window, &state, true);
	ASSERT_BOOL_EQ(state.fileThread == NULL && wcscmp(state.savedFilename, source) == 0, true);
	dialogShowResult = S_OK;
	fileSaveInput = source;
	fileSaveOutput = json;
	fileFailure = true;
	clickFile(window, &state, true);
	finishFile(window, &state);
	ASSERT_BOOL_EQ(wcscmp(state.savedFilename, source) == 0 && errorMessages == ++errors, true);
	ASSERT_BOOL_EQ(fileMissing(json), true);

	fileFailure = false;
	// A successful save of an edited temporary fixture must survive temporary-file cleanup.
	wcscpy(state.replacementPath, source);
	state.path = state.replacementPath;
	state.savedFilename[0] = 0;
	state.temporary = true;
	dialogFilename = L"source.json";
	clickFile(window, &state, true);
	finishFile(window, &state);
	ASSERT_BOOL_EQ(!state.temporary && !state.missing && state.replacementPath[0] == 0, true);
	ASSERT_BOOL_EQ(fileMissing(source), true);
	ASSERT_INT_EQ(wcscmp(state.savedFilename, json), 0);
	ASSERT_INT_EQ(wcscmp(state.filename, L"saved fixture.json"), 0);
	checkContents(json, "{\"fixture\":true}");

	dialogFilename = L"saved fixture.json";
	dialogPath = binary;
	fileSaveInput = json;
	fileSaveOutput = binary;
	clickFile(window, &state, true);
	finishFile(window, &state);
	ASSERT_INT_EQ(wcscmp(state.savedFilename, binary), 0);
	checkContents(binary, "{\"fixture\":true}");

	// The active binary source determines the default save format and the next open's load path.
	dialogBinary = true;
	dialogFilename = L"saved fixture.bin";
	dialogPath = json;
	fileSaveInput = binary;
	fileSaveOutput = json;
	clickFile(window, &state, true);
	finishFile(window, &state);
	ASSERT_INT_EQ(wcscmp(state.savedFilename, json), 0);
	dialogBinary = false;
	dialogPath = binary;
	fileLoadPath = binary;
	clickFile(window, &state, false);
	finishFile(window, &state);
	ASSERT_INT_EQ(wcscmp(state.savedFilename, binary), 0);
	ASSERT_BOOL_EQ(fileLoads == 3 && fileSaves == 4 && errorMessages == errors, true);
	ASSERT_BOOL_EQ(discardPrompts == discards + 3, true);
	wcscpy(state.replacementPath, binary);
	setActivePath(&state, binary, false);
	ASSERT_BOOL_EQ(state.replacementPath[0] == 0 && !fileMissing(binary), true);

	// Team edits still work after a binary Save As becomes the active source.
	fileLoadPath = NULL;
	fileSaveInput = binary;
	binaryReplacement = true;
	replacementResult = 0;
	replacementLoadFailure = false;
	replacementRelease = CreateEventW(NULL, TRUE, TRUE, NULL);
	ASSERT_BOOL_EQ(replacementRelease != NULL, true);
	for (unsigned side = 0; side < 2; ++side) {
		expectedSide = side;
		expectedTeam = side ? L"Luton Town" : L"Norwich City";
		ReplaceTeamJob replacement = {.side = side};
		wcscpy(replacement.input, binary);
		wcscpy(replacement.team, expectedTeam);
		replacement.data = calloc(1, sizeof *replacement.data);
		ASSERT_BOOL_EQ(replacement.data != NULL, true);
		if (replacement.data) {
			ASSERT_INT_EQ((int)replaceTeam(&replacement), 0);
			ASSERT_INT_EQ(
				strcmp(replacement.data->teams[side].name, side ? "Luton Town" : "Norwich City"), 0
			);
		}
		free(replacement.data);
		removeReplacement(replacement.output);
	}
	ASSERT_BOOL_EQ(CloseHandle(replacementRelease), true);
	binaryReplacement = false;

	wchar_t directory[HARNESS_PATH_LENGTH];
	wchar_t expectedDirectory[HARNESS_PATH_LENGTH];
	wcscpy(expectedDirectory, json);
	*wcsrchr(expectedDirectory, L'\\') = 0;
	ASSERT_BOOL_EQ(GetCurrentDirectoryW(HARNESS_PATH_LENGTH, directory) > 0, true);
	ASSERT_INT_EQ(wcscmp(directory, expectedDirectory), 0);
	ASSERT_BOOL_EQ(dialogCreates == dialogReleases + 1, true);
	activeWindow = NULL;
	fileLoadPath = NULL;
	ASSERT_BOOL_EQ(CloseHandle(fileRelease), true);
	ASSERT_BOOL_EQ(DestroyWindow(window), true);
	ASSERT_BOOL_EQ(DeleteObject(state.font), true);
	ASSERT_BOOL_EQ(DeleteObject(state.headingFont), true);
	ASSERT_BOOL_EQ(UnregisterClassW(pitchClass, instance), true);
	ASSERT_BOOL_EQ(DeleteFileW(json), true);
	ASSERT_BOOL_EQ(DeleteFileW(binary), true);
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
	checkNotes(state.metadata);

	state.temporary = true;
	ASSERT_BOOL_EQ(setMetadata(&state), true);
	firstLine(state.metadata, L"This fixture object is not saved. Click here to export it");
	LONG linkStart = (LONG)wcslen(unsavedPrefix);
	LONG linkEnd = linkStart + (LONG)wcslen(exportLink);
	checkFormat(state.metadata, 0, linkStart, CFE_BOLD, RGB(255, 0, 0));
	checkFormat(state.metadata, linkStart, linkEnd, CFE_LINK | CFE_UNDERLINE, RGB_BLUE);
	checkFormat(state.metadata, linkEnd, linkEnd + 13, 0, GetSysColor(COLOR_WINDOWTEXT));
	checkNotes(state.metadata);

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
	checkFileOperations(data);
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
