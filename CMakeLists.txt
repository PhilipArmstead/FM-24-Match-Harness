cmake_minimum_required(VERSION 3.20)
project(fm24_harness C RC)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

# The protocol version is part of the DLL's file name (see src/common/protocol.h). Changing it
# re-runs the configure step, so the DLL is renamed without touching this file.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS src/common/protocol.h)
file(STRINGS src/common/protocol.h protocol_line REGEX "define HARNESS_PROTOCOL L\"[0-9.]+\"")
string(REGEX MATCH "L\"([0-9.]+)\"" protocol_match "${protocol_line}")
set(HARNESS_PROTOCOL "${CMAKE_MATCH_1}")

add_compile_definitions(__USE_MINGW_ANSI_STDIO=1 WIN32_LEAN_AND_MEAN)

set(warnings -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wconversion -Wno-sign-conversion)

add_library(common STATIC
	src/common/buffer.c
	src/common/files.c
	src/common/tactic_names.c
)
target_include_directories(common PUBLIC src/common)
target_compile_options(common PRIVATE ${warnings})

add_library(harness SHARED
	src/dll/config_writer.c
	src/dll/database.c
	src/dll/fixture.c
	src/dll/game.c
	src/dll/job.c
	src/dll/job_build.c
	src/dll/job_inspect.c
	src/dll/job_replace.c
	src/dll/job_run.c
	src/dll/labels.c
	src/dll/log.c
	src/dll/main.c
	src/dll/match.c
	src/dll/match_stats.c
	src/dll/memory.c
	src/dll/output.c
	src/dll/profiler.c
	src/dll/rng.c
	src/dll/seed_build.c
	src/dll/site_lookup.c
	src/dll/site_members.c
	src/dll/stream.c
	src/dll/tactic_flags.c
	src/dll/trace.c
)
set_target_properties(harness PROPERTIES PREFIX "" OUTPUT_NAME "fm24-harness-${HARNESS_PROTOCOL}")
target_link_libraries(harness PRIVATE common)
target_compile_options(harness PRIVATE ${warnings})
target_link_options(harness PRIVATE -static -static-libgcc)

add_executable(fm24ctl
	src/runner/client.c
	src/runner/commands.c
	src/runner/fixture_data.c
	src/runner/fixture_load.c
	src/runner/inject.c
	src/runner/json_reader.c
	src/runner/main.c
	src/runner/match_config.c
	src/runner/paths.c
	src/runner/probe.c
	src/runner/probe_patterns.c
	src/ui/fixture_export.c
	src/ui/fixture_window.c
	src/ui/viewer.rc
)
target_include_directories(fm24ctl PRIVATE src/runner src/ui)
target_link_libraries(fm24ctl PRIVATE common comctl32 user32 gdi32)
target_compile_definitions(fm24ctl PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_options(fm24ctl PRIVATE ${warnings})
target_link_options(fm24ctl PRIVATE -municode -static -static-libgcc)

option(FM24_BUILD_TESTS "Build regression checks" OFF)
if(FM24_BUILD_TESTS)
	enable_testing()
	add_executable(fixture_data_test tests/fixture_data_test.c
		src/runner/fixture_data.c src/runner/json_reader.c src/runner/match_config.c)
	target_include_directories(fixture_data_test PRIVATE src/runner)
	target_link_libraries(fixture_data_test PRIVATE common)
	target_compile_options(fixture_data_test PRIVATE ${warnings})
	add_test(NAME fixture_data COMMAND fixture_data_test)
	add_executable(runner_main_test tests/runner_main_test.c)
	target_include_directories(runner_main_test PRIVATE src/common src/runner src/ui)
	target_compile_options(runner_main_test PRIVATE ${warnings})
	add_test(NAME runner_main COMMAND runner_main_test)
	add_executable(fixture_export_test tests/fixture_export_test.c src/ui/fixture_export.c)
	target_include_directories(fixture_export_test PRIVATE src/runner src/ui)
	target_compile_options(fixture_export_test PRIVATE ${warnings})
	add_test(NAME fixture_export COMMAND fixture_export_test)
	add_executable(fixture_window_test tests/fixture_window_test.c
		src/ui/fixture_export.c src/runner/fixture_data.c src/runner/json_reader.c
		src/runner/match_config.c src/runner/paths.c)
	target_include_directories(fixture_window_test PRIVATE src/runner src/ui)
	target_link_libraries(fixture_window_test PRIVATE common comctl32 user32 gdi32)
	target_compile_definitions(fixture_window_test PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
	target_compile_options(fixture_window_test PRIVATE ${warnings})
	add_test(NAME fixture_window COMMAND fixture_window_test)
endif()
