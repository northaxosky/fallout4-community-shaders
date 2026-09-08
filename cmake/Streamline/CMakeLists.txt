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

set(FO4CS_STREAMLINE_TRUST_CONFIG "" CACHE FILEPATH
  "Reviewed public-only Streamline release trust configuration; empty trusts NVIDIA only")
if(FO4CS_STREAMLINE_TRUST_CONFIG)
  if(NOT IS_ABSOLUTE "${FO4CS_STREAMLINE_TRUST_CONFIG}" OR
     NOT EXISTS "${FO4CS_STREAMLINE_TRUST_CONFIG}")
    message(FATAL_ERROR "FO4CS_STREAMLINE_TRUST_CONFIG must name an existing absolute public configuration path")
  endif()
  set_property(SOURCE "${STREAMLINE_TRUST_SOURCE}"
    DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY COMPILE_DEFINITIONS
    "SL_PROJECT_TRUST_CONFIG_HEADER=\"${FO4CS_STREAMLINE_TRUST_CONFIG}\"")
endif()

if(MSVC)
  target_compile_options(Streamline INTERFACE /wd5103)
endif()
