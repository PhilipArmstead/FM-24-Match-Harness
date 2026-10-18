// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FM24_TESTS_H
#define FM24_TESTS_H

#include "buffer.h"

void fixtureTest_config(ByteBuffer *out);

void registerJobTests(void);
void registerDllTests(void);
void registerFixtureDataTests(void);
void registerFixtureBaselineTests(const char *path);
void registerRunnerMainTests(void);
void registerRunnerTests(void);
void registerFixtureExportTests(void);
void registerFixtureWindowTests(void);
void registerFixtureLoadTests(void);
void registerPlayerInjectTests(void);

#endif
