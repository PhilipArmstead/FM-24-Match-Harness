// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "log.h"

#include "protocol.h"

#include <stdarg.h>
#include <stdio.h>
#include <windows.h>


static FILE *logFile;

void log_open(const wchar_t *directory) {
	wchar_t path[MAX_PATH];
	if (swprintf(path, MAX_PATH, L"%ls\\%ls", directory, HARNESS_LOG_FILE) > 0) {
		logFile = _wfopen(path, L"ab");
	}
}

void log_message(const char *format, ...) {
	if (!logFile) {
		return;
	}
	SYSTEMTIME time;
	GetLocalTime(&time);
	fprintf(
		logFile, "%02d:%02d:%02d.%03d ", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds
	);
	va_list args;
	va_start(args, format);
	vfprintf(logFile, format, args);
	va_end(args);
	fputc('\n', logFile);
	(void)fflush(logFile);
}
