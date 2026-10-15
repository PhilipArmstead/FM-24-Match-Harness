// SPDX-FileCopyrightText: © 2026 Phil Armstead <philarmstead@mailbox.org>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdbool.h>


typedef void (*TestFunction)(void);

void test_run(const char *name, TestFunction fn);
int test_finish(void);
void test_assertIntEq(int actual, int expected, const char *expression, const char *file, int line);
void test_assertBoolEq(
	bool actual,
	bool expected,
	const char *expression,
	const char *file,
	int line
);

#define TEST(name) static void test_##name(void)
#define ASSERT_INT_EQ(actual, expected)                                                            \
	test_assertIntEq((actual), (expected), #actual, __FILE__, __LINE__)
#define ASSERT_BOOL_EQ(actual, expected)                                                           \
	test_assertBoolEq((actual), (expected), #actual, __FILE__, __LINE__)
