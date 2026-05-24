cmake_minimum_required(VERSION 3.21)

project(
	FO4CommunityShaders
	LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)

option(TRACY_SUPPORT "Enable Tracy profiler instrumentation" OFF)

file(STRINGS "${CMAKE_SOURCE_DIR}/version.txt" FO4CS_VERSION LIMIT_COUNT 1)
string(STRIP "${FO4CS_VERSION}" FO4CS_VERSION)
if(NOT FO4CS_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
	message(FATAL_ERROR "version.txt must contain MAJOR.MINOR.PATCH")
endif()
set(FO4CS_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(FO4CS_VERSION_MINOR "${CMAKE_MATCH_2}")
set(FO4CS_VERSION_PATCH "${CMAKE_MATCH_3}")

# Dear-Modding-FO4/commonlibf4 ships only xmake.lua, so generate a CMake shim in the build tree.
set(COMMONLIBF4_SOURCE_DIR "${CMAKE_SOURCE_DIR}/extern/CommonLibF4")
set(COMMONLIBF4_SHIM_DIR "${CMAKE_BINARY_DIR}/CommonLibF4-shim")
configure_file(
	${CMAKE_SOURCE_DIR}/cmake/CommonLibF4.cmake.txt
	${COMMONLIBF4_SHIM_DIR}/CMakeLists.txt
	@ONLY
)
add_subdirectory(${COMMONLIBF4_SHIM_DIR} CommonLibF4 EXCLUDE_FROM_ALL)

# vcpkg packages
find_package(directxtk CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
if(TRACY_SUPPORT)
	find_package(Tracy CONFIG REQUIRED)
endif()

# Header-only INTERFACE targets. FidelityFX-SDK is heavier and is added by features that need it.
add_subdirectory(cmake/Streamline)
add_subdirectory(cmake/XeSS)

# Single unified DLL target.
add_library(FO4CommunityShaders SHARED)

# Core sources.
file(GLOB_RECURSE CORE_SOURCES
	LIST_DIRECTORIES false
	CONFIGURE_DEPENDS
	"${CMAKE_SOURCE_DIR}/src/*.cpp"
	"${CMAKE_SOURCE_DIR}/src/*.h"
)
target_sources(FO4CommunityShaders PRIVATE ${CORE_SOURCES})

include(Common)
include(FidelityFX)
configure_xse_plugin(FO4CommunityShaders ${FO4CS_VERSION_MAJOR} ${FO4CS_VERSION_MINOR} ${FO4CS_VERSION_PATCH})

target_link_libraries(FO4CommunityShaders PRIVATE
	debug ${CMAKE_SOURCE_DIR}/include/detours/Debug/detours.lib
	optimized ${CMAKE_SOURCE_DIR}/include/detours/Release/detours.lib
	Microsoft::DirectXTK
	Microsoft::DirectXTex
	Microsoft::DirectX-Headers
	d3d12.lib
	magic_enum::magic_enum
	d3dcompiler.lib
	imgui::imgui
	tomlplusplus::tomlplusplus
	$<$<BOOL:${TRACY_SUPPORT}>:Tracy::TracyClient>
)

if(TRACY_SUPPORT)
	target_compile_definitions(FO4CommunityShaders PRIVATE TRACY_SUPPORT TRACY_ENABLE)
endif()

# Features attach their own sources to FO4CommunityShaders via target_sources.
# Lifecycle dependencies are declared through cs::Feature::GetDependencies().
register_feature(MotionVectorFixes)
register_feature(Upscaling)
register_feature(FrameGeneration)
register_feature(RenderDoc)
register_feature(PerformanceOverlay)
register_feature(ScreenSpaceShadows)
register_feature(ScreenSpaceGI)
register_feature(Imagespace)
register_feature(ShaderCatalog)
register_feature(ShaderReplacement)

enable_testing()
set(_shader_roundtrip_script "${CMAKE_SOURCE_DIR}/scripts/verify-shader-roundtrip.ps1")
add_test(
	NAME ShaderRoundtrip
	COMMAND pwsh -NoProfile -ExecutionPolicy Bypass -Command
		"if ($env:FXC_PATH) { & '${_shader_roundtrip_script}' -FxcPath $env:FXC_PATH } else { & '${_shader_roundtrip_script}' }"
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(ShaderRoundtrip PROPERTIES
	LABELS "shader;parity"
	TIMEOUT 300
)
