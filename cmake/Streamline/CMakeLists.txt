cmake_minimum_required(VERSION 3.21)

add_library(Streamline INTERFACE)

target_include_directories(
  Streamline SYSTEM INTERFACE
  ${CMAKE_SOURCE_DIR}/extern/Streamline/include
  ${CMAKE_SOURCE_DIR}/extern/Streamline)

set(STREAMLINE_TRUST_SOURCE
  "${CMAKE_SOURCE_DIR}/extern/Streamline/source/core/sl.security/projectTrust.cpp")
set(STREAMLINE_PHYSICAL_PATH_SOURCE
  "${CMAKE_SOURCE_DIR}/extern/Streamline/source/core/sl.security/physicalFilePath.cpp")
target_sources(Streamline INTERFACE "${STREAMLINE_TRUST_SOURCE}" "${STREAMLINE_PHYSICAL_PATH_SOURCE}")
set_source_files_properties("${STREAMLINE_TRUST_SOURCE}" "${STREAMLINE_PHYSICAL_PATH_SOURCE}"
  DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTIES
  SKIP_PRECOMPILE_HEADERS ON)
target_link_libraries(Streamline INTERFACE bcrypt.lib psapi.lib)
target_compile_definitions(Streamline INTERFACE NOMINMAX)

unset(FO4CS_STREAMLINE_TRUST_CONFIG CACHE)
find_program(STREAMLINE_PWSH NAMES pwsh REQUIRED)
set(STREAMLINE_TRUST_HEADER "${CMAKE_CURRENT_BINARY_DIR}/projectTrust.generated.h")
add_custom_command(
  OUTPUT "${STREAMLINE_TRUST_HEADER}"
  COMMAND "${STREAMLINE_PWSH}" -NoProfile -File
    "${CMAKE_SOURCE_DIR}/extern/Streamline/tools/project-release.ps1"
    -Mode GeneratePublicHeader -OutputPath "${STREAMLINE_TRUST_HEADER}"
  DEPENDS
    "${CMAKE_SOURCE_DIR}/extern/Streamline/config/project-release.psd1"
    "${CMAKE_SOURCE_DIR}/extern/Streamline/tools/project-release.ps1"
  COMMENT "Generating Streamline public release trust configuration"
  VERBATIM)
add_custom_target(StreamlineTrustConfig DEPENDS "${STREAMLINE_TRUST_HEADER}")
add_dependencies(Streamline StreamlineTrustConfig)
set_property(SOURCE "${STREAMLINE_TRUST_SOURCE}"
  DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY COMPILE_DEFINITIONS
  "SL_PROJECT_TRUST_CONFIG_HEADER=\"${STREAMLINE_TRUST_HEADER}\"")

if(MSVC)
  target_compile_options(Streamline INTERFACE /wd5103)
endif()
