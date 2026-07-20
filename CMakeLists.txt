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

execute_process(
	COMMAND git describe --tags --dirty --always
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	OUTPUT_VARIABLE BUILD_DESCRIBE
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET
)
if(NOT BUILD_DESCRIBE)
	set(BUILD_DESCRIBE "unknown")
endif()

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
find_package(directxmath CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
if(TRACY_SUPPORT)
	find_package(Tracy CONFIG REQUIRED)
endif()

# Lightweight INTERFACE targets. FidelityFX-SDK is heavier and is added by features that need it.
add_subdirectory(cmake/Streamline)
add_subdirectory(cmake/XeSS)
include(Ngx)

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
register_feature(Imagespace)
register_feature(ScreenSpaceShadows)
register_feature(ScreenSpaceGI)
register_feature(ShaderCatalog)
register_feature(ShaderReplacement)

enable_testing()

function(configure_host_test TARGET_NAME)
	target_compile_features(${TARGET_NAME} PRIVATE cxx_std_23)
	target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/src")
	if(MSVC)
		target_compile_options(${TARGET_NAME} PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
		target_link_options(${TARGET_NAME} PRIVATE /WX)
	endif()
endfunction()

add_executable(FeatureConfigTests
	tests/FeatureConfigTests.cpp
	src/Settings/FeatureConfig.cpp
	src/Settings/FeatureConfig.h
	src/Settings/FeatureKeys.h
)
configure_host_test(FeatureConfigTests)
target_link_libraries(FeatureConfigTests PRIVATE tomlplusplus::tomlplusplus)

add_test(NAME FeatureConfig COMMAND FeatureConfigTests)
set_tests_properties(FeatureConfig PROPERTIES LABELS "configuration;unit")

add_test(
	NAME FeatureConfigSeeds
	COMMAND FeatureConfigTests --validate-seeds "${CMAKE_SOURCE_DIR}/package/F4SE/Plugins/FO4CommunityShaders/FO4CommunityShaders.toml"
)
set_tests_properties(FeatureConfigSeeds PROPERTIES LABELS "configuration;package")

add_executable(FeatureStateTests
	tests/FeatureStateTests.cpp
	src/FeatureState.h
)
configure_host_test(FeatureStateTests)

add_test(NAME FeatureState COMMAND FeatureStateTests)
set_tests_properties(FeatureState PROPERTIES LABELS "state;unit")

add_executable(LogThrottleTests
	tests/LogThrottleTests.cpp
	include/LogThrottle.h
)
configure_host_test(LogThrottleTests)
target_include_directories(LogThrottleTests PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(LogThrottleTests PRIVATE spdlog::spdlog)

add_test(NAME LogThrottle COMMAND LogThrottleTests)
set_tests_properties(LogThrottle PROPERTIES LABELS "logging;unit")

add_executable(ProjectionMathTests
	tests/ProjectionMathTests.cpp
)
configure_host_test(ProjectionMathTests)
target_link_libraries(ProjectionMathTests PRIVATE CommonLibF4::CommonLibF4 Microsoft::DirectXMath)

add_test(NAME ProjectionMath COMMAND ProjectionMathTests)
set_tests_properties(ProjectionMath PROPERTIES LABELS "render;unit")

add_executable(FrustumEmbedTests
	tests/FrustumEmbedTests.cpp
	features/ScreenSpaceGI/src/OracleProjectionEmbed.h
)
configure_host_test(FrustumEmbedTests)
target_include_directories(FrustumEmbedTests PRIVATE "${CMAKE_SOURCE_DIR}/features/ScreenSpaceGI/src")

add_test(NAME FrustumEmbed COMMAND FrustumEmbedTests)
set_tests_properties(FrustumEmbed PROPERTIES LABELS "render;unit")

add_executable(ShaderCompileTests
	tests/ShaderCompileTests.cpp
	src/Utils/ShaderCompile.cpp
	src/Utils/ShaderCompile.h
)
configure_host_test(ShaderCompileTests)
target_link_libraries(ShaderCompileTests PRIVATE d3dcompiler.lib)

add_test(NAME ShaderCompile COMMAND ShaderCompileTests "${CMAKE_SOURCE_DIR}/features/ScreenSpaceGI/Shaders")
set_tests_properties(ShaderCompile PROPERTIES LABELS "shader;unit")

set(_shader_roundtrip_script "${CMAKE_SOURCE_DIR}/scripts/shaders/verify-shader-roundtrip.ps1")
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
