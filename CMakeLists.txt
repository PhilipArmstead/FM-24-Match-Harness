cmake_minimum_required(VERSION 3.20)
project(fm24_harness C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

# The protocol version is part of the DLL's file name (see src/common/protocol.h).
file(STRINGS src/common/protocol.h protocol_line REGEX "define HARNESS_PROTOCOL L\"[0-9]+\"")
string(REGEX MATCH "[0-9]+" HARNESS_PROTOCOL "${protocol_line}")

add_compile_definitions(__USE_MINGW_ANSI_STDIO=1 WIN32_LEAN_AND_MEAN)

set(warnings -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wconversion -Wno-sign-conversion)

add_library(common STATIC
	src/common/buffer.c
	src/common/files.c
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
	src/dll/job_rebuild.c
	src/dll/job_run.c
	src/dll/job_sensitivity.c
	src/dll/labels.c
	src/dll/log.c
	src/dll/main.c
	src/dll/match.c
	src/dll/match_stats.c
	src/dll/memory.c
	src/dll/output.c
	src/dll/profiler.c
	src/dll/rng.c
	src/dll/site_lookup.c
	src/dll/site_members.c
	src/dll/stream.c
	src/dll/tactic_flags.c
	src/dll/trace.c
)
set_target_properties(harness PROPERTIES PREFIX "" OUTPUT_NAME "fm24_harness_p${HARNESS_PROTOCOL}")
target_link_libraries(harness PRIVATE common)
target_compile_options(harness PRIVATE ${warnings})
target_link_options(harness PRIVATE -static -static-libgcc)

add_executable(fm24ctl
	src/runner/client.c
	src/runner/commands.c
	src/runner/inject.c
	src/runner/json_reader.c
	src/runner/main.c
	src/runner/match_config.c
	src/runner/paths.c
)
target_link_libraries(fm24ctl PRIVATE common)
target_compile_options(fm24ctl PRIVATE ${warnings})
target_link_options(fm24ctl PRIVATE -municode -static -static-libgcc)
