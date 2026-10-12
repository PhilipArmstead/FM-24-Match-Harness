// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// What the harness DLL (inside fm.exe) and the runner (fm24ctl) agree on.
//
// The runner writes a job file of `key=value` lines next to the DLL, signals HARNESS_EVENT_GO and
// waits for HARNESS_EVENT_DONE; the DLL writes a one-line outcome to the result file.
//
// Bump HARNESS_PROTOCOL to inject a rebuilt DLL while an older copy is still loaded in fm.exe.
// Windows identifies a loaded DLL by its path, so a new build needs a new file name; the old copy
// keeps waiting on its own, differently named events and never sees the new jobs.
#pragma once

#define HARNESS_PROTOCOL L"9"
#define HARNESS_DLL_NAME (L"fm24_harness_p" HARNESS_PROTOCOL L".dll")
#define HARNESS_EVENT_GO (L"Local\\FM24Harness_Go_" HARNESS_PROTOCOL)
#define HARNESS_EVENT_DONE (L"Local\\FM24Harness_Done_" HARNESS_PROTOCOL)

#define HARNESS_JOB_FILE L"job.txt"
#define HARNESS_RESULT_FILE L"result.txt"
#define HARNESS_LOG_FILE L"harness.log"

// Longest path (in UTF-16 units) either side handles; job files carry full paths.
enum { HARNESS_PATH_LENGTH = 1024 };

// Most worker threads a run job may use.
enum { HARNESS_MAX_THREADS = 16 };
