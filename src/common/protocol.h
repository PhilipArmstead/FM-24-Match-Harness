// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

// What the harness DLL (inside fm.exe) and the runner (fm24ctl) agree on.
//
// The runner connects to HARNESS_PIPE_NAME, writes a job of `key=value` lines as one message and
// reads the DLL's one-line outcome as the reply. The pipe has a single instance, so the DLL serves
// one runner at a time and the others wait for it. Jobs carry full paths, so a runner works with
// a DLL loaded from any folder; the DLL writes harness.log next to itself.
//
// Bump HARNESS_PROTOCOL (a dotted version: 1.0.0 -> 1.0.1) to inject a rebuilt DLL while an older
// copy is still loaded in fm.exe.
// Windows identifies a loaded DLL by its path, so a new build needs a new file name; the old copy
// keeps serving its own, differently named pipe and never sees the new jobs. A second copy of the
// same version (from another folder) cannot create the pipe and stays idle.
#pragma once

#define HARNESS_PROTOCOL L"1.1.9"
#define HARNESS_DLL_NAME (L"fm24-harness-" HARNESS_PROTOCOL L".dll")
#define HARNESS_PIPE_NAME (L"\\\\.\\pipe\\FM24Harness_" HARNESS_PROTOCOL)

#define HARNESS_LOG_FILE L"harness.log"

// Longest job message and result line (in bytes) either side accepts.
enum { HARNESS_JOB_LENGTH = 16384, HARNESS_RESULT_LENGTH = 512 };

// Longest path (in UTF-16 units) either side handles; job files carry full paths.
enum { HARNESS_PATH_LENGTH = 1024 };

// Most worker threads a run job may use.
enum { HARNESS_MAX_THREADS = 16 };

// Optional run-job snapshots to the x64 viewer (low/high 32 bits):
// wParam = completed/home wins, lParam = total/draws. Away wins = completed - home wins - draws.
enum { HARNESS_PROGRESS_MESSAGE = 0x8002 }; // WM_APP + 2
