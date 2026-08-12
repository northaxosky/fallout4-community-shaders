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
option(FO4CS_HOST_TESTS_ONLY "Configure host tests without the plugin target" OFF)

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

execute_process(
	COMMAND git rev-parse HEAD
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	OUTPUT_VARIABLE BUILD_GIT_SHA
	OUTPUT_STRIP_TRAILING_WHITESPACE
	ERROR_QUIET
)
if(NOT BUILD_GIT_SHA)
	set(BUILD_GIT_SHA "unknown")
endif()

if(NOT FO4CS_HOST_TESTS_ONLY)
	# Dear-Modding-FO4/commonlibf4 ships only xmake.lua, so generate a CMake shim in the build tree.
	set(COMMONLIBF4_SOURCE_DIR "${CMAKE_SOURCE_DIR}/extern/CommonLibF4")
	set(COMMONLIBF4_SHIM_DIR "${CMAKE_BINARY_DIR}/CommonLibF4-shim")
	configure_file(
		${CMAKE_SOURCE_DIR}/cmake/CommonLibF4.cmake.txt
		${COMMONLIBF4_SHIM_DIR}/CMakeLists.txt
		@ONLY
	)
	add_subdirectory(${COMMONLIBF4_SHIM_DIR} CommonLibF4 EXCLUDE_FROM_ALL)
endif()

# vcpkg packages
find_package(directxtk CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)
find_package(directxmath CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
set(FO4CS_NLOHMANN_JSON_INCLUDE_DIR
	"${CMAKE_SOURCE_DIR}/extern/Streamline/external/json/include")
find_path(MICROSOFT_DETOURS_INCLUDE_DIR "detours/detours.h" REQUIRED)
find_library(MICROSOFT_DETOURS_LIBRARY_RELEASE
	NAMES detours
	PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
	PATH_SUFFIXES lib
	NO_DEFAULT_PATH
	REQUIRED
)
find_library(MICROSOFT_DETOURS_LIBRARY_DEBUG
	NAMES detours
	PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
	PATH_SUFFIXES debug/lib
	NO_DEFAULT_PATH
	REQUIRED
)
if(TRACY_SUPPORT)
	find_package(Tracy CONFIG REQUIRED)
endif()

# Lightweight INTERFACE targets. FidelityFX-SDK is heavier and is added by features that need it.
add_subdirectory(cmake/Streamline)
add_subdirectory(cmake/XeSS)
if(NOT FO4CS_HOST_TESTS_ONLY)
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

	target_include_directories(FO4CommunityShaders BEFORE PRIVATE
		"${MICROSOFT_DETOURS_INCLUDE_DIR}"
	)
	target_include_directories(FO4CommunityShaders SYSTEM PRIVATE
		"${FO4CS_NLOHMANN_JSON_INCLUDE_DIR}"
	)
	target_link_libraries(FO4CommunityShaders PRIVATE
		debug ${CMAKE_SOURCE_DIR}/include/detours/Debug/detours.lib
		optimized ${CMAKE_SOURCE_DIR}/include/detours/Release/detours.lib
		debug ${MICROSOFT_DETOURS_LIBRARY_DEBUG}
		optimized ${MICROSOFT_DETOURS_LIBRARY_RELEASE}
		Microsoft::DirectXTK
		Microsoft::DirectXTex
		Microsoft::DirectX-Headers
		bcrypt.lib
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
	# Registration order is lifecycle order.
	register_feature(MotionVectorFixes)
	register_feature(Upscaling)
	register_feature(FrameGeneration)
	register_feature(RenderDoc)
	register_feature(PerformanceOverlay)
	register_feature(Imagespace)
	register_feature(ScreenSpaceShadows)
	register_feature(ScreenSpaceGI)
	register_feature(WetnessEffects)
endif()

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
	features/ScreenSpaceGI/src/ScreenSpaceGILifecycle.h
)
configure_host_test(FeatureStateTests)
target_include_directories(FeatureStateTests PRIVATE
	"${CMAKE_SOURCE_DIR}/features/ScreenSpaceGI/src"
)

add_test(NAME FeatureState COMMAND FeatureStateTests)
set_tests_properties(FeatureState PROPERTIES LABELS "state;unit")

add_executable(ScreenSpaceShadowsMathTests
	tests/ScreenSpaceShadowsMathTests.cpp
	features/ScreenSpaceShadows/src/ScreenSpaceShadowsMath.h
	features/ScreenSpaceShadows/src/SssInjectionMode.h
)
configure_host_test(ScreenSpaceShadowsMathTests)
target_include_directories(ScreenSpaceShadowsMathTests PRIVATE
	"${CMAKE_SOURCE_DIR}/features/ScreenSpaceShadows/src"
)

add_test(NAME ScreenSpaceShadowsMath COMMAND ScreenSpaceShadowsMathTests)
set_tests_properties(ScreenSpaceShadowsMath PROPERTIES LABELS "render;unit")

add_executable(LogThrottleTests
	tests/LogThrottleTests.cpp
	include/LogThrottle.h
)
configure_host_test(LogThrottleTests)
target_include_directories(LogThrottleTests PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(LogThrottleTests PRIVATE spdlog::spdlog)

add_test(NAME LogThrottle COMMAND LogThrottleTests)
set_tests_properties(LogThrottle PROPERTIES LABELS "logging;unit")

if(NOT FO4CS_HOST_TESTS_ONLY)
	add_executable(ProjectionMathTests
		tests/ProjectionMathTests.cpp
	)
	configure_host_test(ProjectionMathTests)
	target_link_libraries(ProjectionMathTests PRIVATE CommonLibF4::CommonLibF4 Microsoft::DirectXMath)

	add_test(NAME ProjectionMath COMMAND ProjectionMathTests)
	set_tests_properties(ProjectionMath PROPERTIES LABELS "render;unit")
endif()

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

add_test(
	NAME ShaderCompile
	COMMAND ShaderCompileTests
		"${CMAKE_SOURCE_DIR}/features/ScreenSpaceGI/Shaders"
		"${CMAKE_SOURCE_DIR}/package/F4SE/Plugins/FO4CommunityShaders/Shaders"
)
set_tests_properties(ShaderCompile PROPERTIES LABELS "shader;unit")

add_executable(PixelShaderSwapTests
	tests/PixelShaderSwapTests.cpp
	src/Render/PixelShaderSwapModel.cpp
	src/Render/PixelShaderSwapBroker.h
	src/Render/ShaderVariantResolver.cpp
	src/Render/ShaderVariantResolver.h
	src/Utils/CSSha1.cpp
	src/Utils/CSSha1.h
)
configure_host_test(PixelShaderSwapTests)
target_compile_definitions(
	PixelShaderSwapTests PRIVATE
		FO4CS_CURRENT_PIXEL_SHADER_TESTING=1
		FO4CS_ENGINE_LOOKUP_TESTING=1
)
target_link_libraries(PixelShaderSwapTests PRIVATE bcrypt.lib)

add_test(NAME PixelShaderSwap COMMAND PixelShaderSwapTests)
set_tests_properties(PixelShaderSwap PROPERTIES LABELS "render;shader;unit")

if(NOT FO4CS_HOST_TESTS_ONLY)
	add_executable(ShaderInjectionRegistrationTests
		tests/ShaderInjectionRegistrationTests.cpp
		src/Render/ShaderInjection.cpp
		src/Render/PixelShaderSwapModel.cpp
		src/Utils/CSSha1.cpp
	)
	configure_host_test(ShaderInjectionRegistrationTests)
	target_precompile_headers(ShaderInjectionRegistrationTests PRIVATE
		"${CMAKE_SOURCE_DIR}/include/PCH.h"
	)
	target_compile_definitions(ShaderInjectionRegistrationTests PRIVATE
		_WINDOWS
		_AMD64_
		_UNICODE
		COMMONLIB_RUNTIMECOUNT=3
	)
	target_include_directories(ShaderInjectionRegistrationTests BEFORE PRIVATE
		"${MICROSOFT_DETOURS_INCLUDE_DIR}"
		"${CMAKE_BINARY_DIR}/FO4CommunityShaders"
		"${CMAKE_SOURCE_DIR}/include"
		"${CMAKE_SOURCE_DIR}/src"
		"${CMAKE_SOURCE_DIR}/tests"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/include"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/lib/commonlib-shared/include"
	)
	target_include_directories(ShaderInjectionRegistrationTests SYSTEM PRIVATE
		"${FO4CS_NLOHMANN_JSON_INCLUDE_DIR}"
	)
	target_link_libraries(ShaderInjectionRegistrationTests PRIVATE
		CommonLibF4::CommonLibF4
		Microsoft::DirectXTK
		Microsoft::DirectX-Headers
		bcrypt.lib
		imgui::imgui
		magic_enum::magic_enum
		spdlog::spdlog
		tomlplusplus::tomlplusplus
	)

	add_test(
		NAME ShaderInjectionRegistration
		COMMAND ShaderInjectionRegistrationTests
	)
	add_test(
		NAME ShaderInjectionBaselineOwnership
		COMMAND ShaderInjectionRegistrationTests --baseline-ownership
	)
	set_tests_properties(ShaderInjectionRegistration PROPERTIES
		LABELS "render;shader;unit"
	)
	set_tests_properties(ShaderInjectionBaselineOwnership PROPERTIES
		LABELS "render;shader;unit"
	)
endif()
