// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "config_writer.h"
#include "database.h"
#include "files.h"
#include "job.h"
#include "log.h"
#include "match.h"
#include "trace.h"

void jobs_inspect(const Job *job, const wchar_t *directory) {
	ByteBuffer input = {0};
	size_t seedOffset = 0;
	if (!files_read(job->input, &input) || !match_seedOffset(input.data, input.length, &seedOffset)) {
		job_writeResult(directory, "error: cannot read input or invalid FM24 binary header");
		buffer_free(&input);
		return;
	}
	uint8_t *match = match_deserialize(&input);
	buffer_free(&input);
	if (!match) {
		job_writeResult(directory, "error: the game could not decode the binary fixture");
		return;
	}
	Serialised serialised = {0};
	bool ok = trace_serialize(match, &serialised, true);
	match_delete(match);
	if (ok) {
		PeopleIndex people;
		(void)database_loadPeople(&people);
		ok = configWriter_write(
			job->output,
			&serialised,
			"Decoded binary fixture (date annotation unavailable)",
			&people,
			false
		);
		database_freePeople(&people);
	}
	trace_free(&serialised);
	if (ok) {
		job_writeResult(directory, "ok: decoded fixture without playing it");
	} else {
		log_message("inspect: serialise or JSON write failed");
		job_writeResult(directory, "error: cannot export decoded fixture; see harness.log");
	}
}
