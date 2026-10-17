# SPDX-License-Identifier: GPL-3.0-or-later
# DLL is the stripped copy fm24ctl embeds; the release installs only RUNNER.
file(SHA256 "${DLL}" dll_before)
file(SHA256 "${RUNNER}" runner_before)

execute_process(
	COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --config "${CONFIG}"
		--prefix "${BUILD_DIR}/release-check" --component Runtime
	COMMAND_ERROR_IS_FATAL ANY
)

get_filename_component(runner_name "${RUNNER}" NAME)
set(installed_runner "${BUILD_DIR}/release-check/${runner_name}")
execute_process(
	COMMAND "${STRIP}" --strip-unneeded "${installed_runner}"
	COMMAND_ERROR_IS_FATAL ANY
)

foreach(binary IN ITEMS "${DLL}" "${installed_runner}")
	get_filename_component(name "${binary}" NAME)
	execute_process(
		COMMAND "${OBJDUMP}" -h "${binary}"
		OUTPUT_VARIABLE sections
		COMMAND_ERROR_IS_FATAL ANY
	)
	if(sections MATCHES "\\.debug_" OR NOT sections MATCHES "\\.pdata" OR
			NOT sections MATCHES "\\.xdata")
		message(FATAL_ERROR "${name}: expected stripped binary with Windows unwind metadata")
	endif()
	if(binary STREQUAL DLL AND sections MATCHES "\\.edata")
		message(FATAL_ERROR "${name}: internal DLL symbols must not be exported")
	endif()
	if(binary STREQUAL installed_runner)
		# The embedded DLL lives in .rsrc, so the section must be at least as large as the DLL.
		file(SIZE "${DLL}" dll_size)
		string(REGEX MATCH "\\.rsrc +([0-9a-f]+)" rsrc_match "${sections}")
		if(NOT rsrc_match)
			message(FATAL_ERROR "${name}: missing application resources")
		endif()
		math(EXPR rsrc_size "0x${CMAKE_MATCH_1}")
		if(rsrc_size LESS dll_size)
			message(FATAL_ERROR "${name}: the harness DLL is not embedded")
		endif()
	endif()
endforeach()

file(SHA256 "${DLL}" dll_after)
file(SHA256 "${RUNNER}" runner_after)
if(NOT dll_before STREQUAL dll_after OR NOT runner_before STREQUAL runner_after)
	message(FATAL_ERROR "Installing stripped copies modified the build outputs")
endif()
