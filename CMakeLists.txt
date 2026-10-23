cmake_minimum_required(VERSION 3.20)
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "C compiler flags for Release builds")
project(fm24_harness C RC)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release" OR "Release" IN_LIST CMAKE_CONFIGURATION_TYPES)
	include(CheckIPOSupported)
	check_ipo_supported(LANGUAGES C)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()
add_link_options("$<$<CONFIG:Release>:-Wl,--gc-sections>")

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
	src/ui/constants.h
)
target_include_directories(common PUBLIC src/common)
target_compile_options(common PRIVATE ${warnings})

add_library(harness SHARED
	src/dll/config_writer.c
	src/dll/database.c
	src/dll/development_file.c
	src/dll/fixture.c
	src/dll/game.c
	src/dll/job.c
	src/dll/job_build.c
	src/dll/job_inspect.c
	src/dll/job_replace.c
	src/dll/job_teams.c
	src/dll/job_players.c
	src/dll/job_develop.c
	src/dll/job_seed.c
	src/dll/job_player.c
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
	src/dll/freeze.c
)
set_target_properties(harness PROPERTIES PREFIX "" OUTPUT_NAME "fm24-harness-${HARNESS_PROTOCOL}")
target_link_libraries(harness PRIVATE common)
target_compile_options(harness PRIVATE ${warnings})
target_link_options(harness PRIVATE -static -static-libgcc
	"$<$<CONFIG:Release>:-Wl,--exclude-all-symbols>"
)

add_executable(fm24ctl
	src/runner/client.c
	src/runner/commands.c
	src/runner/embedded.c
	src/runner/fixture_data.c
	src/runner/fixture_load.c
	src/runner/inject.c
	src/runner/json_reader.c
	src/runner/main.c
	src/runner/match_config.c
	src/runner/paths.c
	src/runner/player_inject.c
	src/runner/probe.c
	src/runner/probe_patterns.c
	src/ui/fixture_export.c
	src/ui/fixture_window.c
	src/ui/viewer.rc
	"${CMAKE_BINARY_DIR}/harness_dll.rc"
)
# fm24ctl carries a stripped copy of the DLL as a resource and extracts it before injecting (see
# src/runner/embedded.h), so a release is the one executable.
set(embedded_dll "${CMAKE_BINARY_DIR}/harness-embedded.dll")
add_custom_command(
	OUTPUT "${embedded_dll}"
	COMMAND "${CMAKE_STRIP}" --strip-unneeded -o "${embedded_dll}" "$<TARGET_FILE:harness>"
	DEPENDS harness
	VERBATIM
)
file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/harness_dll.rc"
	CONTENT "#include \"resources.h\"\nIDR_HARNESS_DLL RCDATA \"${embedded_dll}\"\n"
)
set_source_files_properties("${CMAKE_BINARY_DIR}/harness_dll.rc" PROPERTIES OBJECT_DEPENDS "${embedded_dll}")
target_include_directories(fm24ctl PRIVATE src/runner src/ui)
target_link_libraries(fm24ctl PRIVATE common comctl32 user32 gdi32 ole32 shell32 uuid)
target_compile_definitions(fm24ctl PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_options(fm24ctl PRIVATE ${warnings})
target_link_options(fm24ctl PRIVATE -municode -static -static-libgcc)

install(TARGETS fm24ctl RUNTIME DESTINATION . COMPONENT Runtime)

option(FM24_BUILD_TESTS "Build regression checks" OFF)
if(FM24_BUILD_TESTS)
	enable_testing()
	add_executable(fm24_tests
		tests/test.c
		tests/job_test.c
		tests/dll_test.c
		tests/fixture_data_test.c
		tests/runner_main_test.c
		tests/runner_test.c
		tests/fixture_export_test.c
		tests/fixture_window_test.c
		tests/fixture_load_test.c
		tests/player_inject_test.c
		src/ui/fixture_export.c
		src/runner/embedded.c
		src/runner/fixture_data.c
		src/runner/json_reader.c
		src/runner/match_config.c
		src/runner/paths.c
		src/runner/player_inject.c
		src/runner/probe_patterns.c
		src/dll/memory.c
		src/dll/site_lookup.c
		src/dll/site_members.c
		src/dll/tactic_flags.c
	)
	target_include_directories(fm24_tests PRIVATE src/runner src/ui)
	target_link_libraries(fm24_tests PRIVATE common comctl32 user32 gdi32 ole32 shell32 uuid)
	target_compile_definitions(fm24_tests PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
	target_compile_options(fm24_tests PRIVATE ${warnings})
	target_link_options(fm24_tests PRIVATE -static -static-libgcc)
	install(TARGETS fm24_tests RUNTIME DESTINATION . COMPONENT Tests)
	foreach(test job dll fixture_data runner runner_main fixture_export fixture_window fixture_load player_inject)
		add_test(NAME ${test} COMMAND fm24_tests ${test})
	endforeach()
	if(CMAKE_BUILD_TYPE STREQUAL "Release" OR "Release" IN_LIST CMAKE_CONFIGURATION_TYPES)
		set(release_configurations)
		if(CMAKE_CONFIGURATION_TYPES)
			set(release_configurations CONFIGURATIONS Release)
		endif()
		add_test(NAME release
			COMMAND "${CMAKE_COMMAND}"
				"-DBUILD_DIR=${CMAKE_BINARY_DIR}"
				"-DCONFIG=$<CONFIG>"
				"-DDLL=${embedded_dll}"
				"-DRUNNER=$<TARGET_FILE:fm24ctl>"
				"-DSTRIP=${CMAKE_STRIP}"
				"-DOBJDUMP=${CMAKE_OBJDUMP}"
				-P "${CMAKE_CURRENT_SOURCE_DIR}/tests/release_test.cmake"
			${release_configurations}
		)
	endif()
endif()
