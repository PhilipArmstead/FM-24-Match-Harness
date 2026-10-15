# SPDX-License-Identifier: GPL-3.0-or-later
file(SHA256 "${DLL}" dll_before)
file(SHA256 "${RUNNER}" runner_before)

execute_process(
	COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --config "${CONFIG}"
		--prefix "${BUILD_DIR}/release-check" --component Runtime
	COMMAND_ERROR_IS_FATAL ANY
)

foreach(binary IN ITEMS "${DLL}" "${RUNNER}")
	get_filename_component(name "${binary}" NAME)
	execute_process(
		COMMAND "${STRIP}" --strip-unneeded "${BUILD_DIR}/release-check/${name}"
		COMMAND_ERROR_IS_FATAL ANY
	)
	execute_process(
		COMMAND "${OBJDUMP}" -h "${BUILD_DIR}/release-check/${name}"
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
	if(binary STREQUAL RUNNER AND NOT sections MATCHES "\\.rsrc")
		message(FATAL_ERROR "${name}: missing application resources")
	endif()
endforeach()

file(SHA256 "${DLL}" dll_after)
file(SHA256 "${RUNNER}" runner_after)
if(NOT dll_before STREQUAL dll_after OR NOT runner_before STREQUAL runner_after)
	message(FATAL_ERROR "Installing stripped copies modified the build outputs")
endif()
