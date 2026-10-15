// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture_window.h"

#include "fixture_data.h"
#include "fixture_export.h"
#include "fixture_load.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <windows.h>

// Common controls require the Win32 types above.
#include <commctrl.h>
#include <richedit.h>

enum { WM_FIXTURE_LAYOUT = WM_APP + 1 };

typedef struct WindowState {
	FixtureData *data;
	const wchar_t *path;
	const wchar_t *filename;
	wchar_t savedFilename[FIXTURE_EXPORT_FILENAME_LENGTH];
	HWND metadata;
	HWND tabs;
	HWND headings[2];
	HWND notes[2];
	HWND pitches[2];
	HWND lists[2];
	HFONT font;
	HFONT headingFont;
	UINT dpi;
	int metadataHeight;
	bool layoutPending;
	bool failed;
	bool temporary;
	bool missing;
} WindowState;

static const wchar_t windowClass[] = L"FM24FixtureViewer";
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
	MessageBoxW(window, message, L"FM24 fixture viewer", MB_OK | MB_ICONERROR);
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
		"NOTE: Condition / sharpness are percentages. Fatigue is signed jadedness (0 = fresh), not a "
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
		data->officials[4][0] ? data->officials[4] : "Not stored / unassigned",
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
				RGB(0, 0, 255)
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
		if (!setText(state->headings[side], text)) {
			return false;
		}
		if (!setText(
					state->notes[side],
					team->lineupKnown ? "Stored starting XI; pitch numbers are tactic slots."
														: "AI / unknown control: assignments unmapped. Squad listed below; AI "
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

static bool setFonts(WindowState *state) {
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
		L"Segoe UI"
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
	int tabHeight = scaled(state, 30);
	MoveWindow(state->metadata, margin, margin, width, state->metadataHeight, FALSE);
	SendMessageW(state->metadata, EM_REQUESTRESIZE, 0, 0);
	MoveWindow(state->metadata, margin, margin, width, state->metadataHeight, TRUE);
	int top = margin + state->metadataHeight + margin;
	MoveWindow(state->tabs, margin, top, width, tabHeight, TRUE);
	top += tabHeight + margin;
	int panelHeight = (bounds.bottom - top - 2 * margin) / 2;
	for (unsigned i = 0; i < 2; ++i) {
		int y = top + (int)i * (panelHeight + margin);
		int header = scaled(state, 27);
		int note = scaled(state, 22);
		MoveWindow(state->headings[i], margin, y, width, header, TRUE);
		MoveWindow(state->notes[i], margin, y + header, width, note, TRUE);
		int height = panelHeight - header - note;
		int pitchWidth = scaled(state, 220);
		MoveWindow(state->pitches[i], margin, y + header + note, pitchWidth, height, TRUE);
		int offset = pitchWidth + margin;
		MoveWindow(state->lists[i], margin + offset, y + header + note, width - offset, height, TRUE);
	}
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
		WS_CHILD | WS_VISIBLE | WS_TABSTOP,
		0,
		0,
		0,
		0,
		window,
		NULL,
		instance,
		NULL
	);
	if (!state->metadata || !state->tabs) {
		return false;
	}
	SendMessageW(state->metadata, EM_SETLIMITTEXT, 8192, 0);
	SendMessageW(state->metadata, EM_SETEVENTMASK, 0, ENM_REQUESTRESIZE | ENM_LINK);
	TCITEMW tab = {.mask = TCIF_TEXT, .pszText = L"Starting XI"};
	if (SendMessageW(state->tabs, TCM_INSERTITEMW, 0, (LPARAM)&tab) == -1) {
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
	return setFonts(state) && setMetadata(state) && setHeadings(state) && fillList(state, 0) &&
				 fillList(state, 1);
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
		case WM_GETMINMAXINFO: {
			MINMAXINFO *limits = (MINMAXINFO *)lParam;
			limits->ptMinTrackSize.x = scaled(state, 900);
			limits->ptMinTrackSize.y = scaled(state, 600);
			return 0;
		}
		case WM_DPICHANGED: {
			state->dpi = HIWORD(wParam);
			if (!setFonts(state) || !setMetadata(state)) {
				showError(window, L"Cannot update window fonts or summary.");
				state->failed = true;
				DestroyWindow(window);
				return 0;
			}
			const RECT *bounds = (const RECT *)lParam;
			SetWindowPos(
				window,
				NULL,
				bounds->left,
				bounds->top,
				bounds->right - bounds->left,
				bounds->bottom - bounds->top,
				SWP_NOZORDER | SWP_NOACTIVATE
			);
			return 0;
		}
		case WM_NOTIFY: return metadataNotification(window, state, (const NMHDR *)lParam);
		case WM_DESTROY: PostQuitMessage(state->failed ? 1 : 0); return 0;
		default: return DefWindowProcW(window, message, wParam, lParam);
	}
}
// NOLINTEND(performance-no-int-to-ptr)

static int runMessageLoop(HWND window, WindowState *state) {
	ShowWindow(window, SW_SHOW);
	MSG message;
	BOOL status;
	while ((status = GetMessageW(&message, NULL, 0, 0)) > 0) {
		if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN &&
				message.hwnd == state->metadata && state->temporary && !state->missing) {
			exportFixture(window, state);
			continue;
		}
		if (message.message == WM_KEYDOWN && message.wParam == VK_TAB) {
			HWND focus = GetFocus();
			HWND next =
				GetNextDlgTabItem(window, focus == window ? NULL : focus, GetKeyState(VK_SHIFT) < 0);
			if (next) {
				SetFocus(next);
				continue;
			}
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
	INITCOMMONCONTROLSEX controls = {
		.dwSize = sizeof controls, .dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES};
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
	HWND window = NULL;
	if (ok) {
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
			L"FM24 fixture viewer",
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
