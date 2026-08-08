# Regenerates Plugin.h at build time so the embedded git identity tracks HEAD
# rather than the last CMake configure. Run via cmake -P.

execute_process(
	COMMAND git describe --tags --dirty --always
	WORKING_DIRECTORY "${SOURCE_DIR}"
	OUTPUT_VARIABLE BUILD_DESCRIBE
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET
)
if(NOT BUILD_DESCRIBE)
	set(BUILD_DESCRIBE "unknown")
endif()

execute_process(
	COMMAND git rev-parse HEAD
	WORKING_DIRECTORY "${SOURCE_DIR}"
	OUTPUT_VARIABLE BUILD_GIT_SHA
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET
)
if(NOT BUILD_GIT_SHA)
	set(BUILD_GIT_SHA "unknown")
endif()

configure_file("${IN_FILE}" "${OUT_FILE}.tmp" @ONLY)

# Only replace on change, so an unchanged HEAD does not force an LTCG rebuild.
execute_process(
	COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${OUT_FILE}.tmp" "${OUT_FILE}"
)
file(REMOVE "${OUT_FILE}.tmp")
