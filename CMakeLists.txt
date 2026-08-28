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

if(NOT FO4CS_HOST_TESTS_ONLY)
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
	configure_xse_plugin(FO4CommunityShaders ${FO4CS_VERSION_MAJOR} ${FO4CS_VERSION_MINOR} ${FO4CS_VERSION_PATCH})

	target_include_directories(FO4CommunityShaders BEFORE PRIVATE
		"${MICROSOFT_DETOURS_INCLUDE_DIR}"
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
	register_feature(RenderDoc)
	register_feature(PerformanceOverlay)
	register_feature(MotionVectorFixes)
	register_feature(Upscaling)
	register_feature(ScreenSpaceShadows)
	register_feature(ScreenSpaceGI)
	register_feature(WetnessEffects)

	# Upscaling backends: Streamline headers are interface-only, FidelityFX builds from source.
	add_subdirectory(cmake/Streamline)
	include(FidelityFX-SDK)
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

# shared by the plugin and host tests
set(FO4CS_SHADER_CACHE_SOURCES
	src/Utils/ShaderCache/ByteCodec.h
	src/Utils/ShaderCache/CacheRecord.cpp
	src/Utils/ShaderCache/CacheRecord.h
	src/Utils/ShaderCache/CacheStorage.cpp
	src/Utils/ShaderCache/CacheStorage.h
	src/Utils/ShaderCache/CompilerIdentity.cpp
	src/Utils/ShaderCache/CompilerIdentity.h
	src/Utils/ShaderCache/DependencyTrace.cpp
	src/Utils/ShaderCache/DependencyTrace.h
	src/Utils/ShaderCache/RevalidationContext.cpp
	src/Utils/ShaderCache/RevalidationContext.h
	src/Utils/ShaderCache/ShaderCache.cpp
	src/Utils/ShaderCache/ShaderCache.h
	src/Utils/ShaderCache/ShaderRecipe.cpp
	src/Utils/ShaderCache/ShaderRecipe.h
	src/Utils/ShaderCache/SourceCompile.cpp
	src/Utils/ShaderCache/SourceCompile.h
)

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

add_executable(ThemeDeltaTests
	tests/ThemeDeltaTests.cpp
	src/Menu/ThemeDelta.cpp
	src/Menu/ThemeDelta.h
	src/Settings/FeatureConfig.cpp
	src/Settings/FeatureConfig.h
)
configure_host_test(ThemeDeltaTests)
target_link_libraries(ThemeDeltaTests PRIVATE imgui::imgui tomlplusplus::tomlplusplus)

add_test(
	NAME ThemeDelta
	COMMAND ThemeDeltaTests "${CMAKE_SOURCE_DIR}/package/F4SE/Plugins/FO4CommunityShaders/FO4CommunityShaders.toml"
)
set_tests_properties(ThemeDelta PROPERTIES LABELS "configuration;menu;unit")

add_executable(ScreenSpaceShadowsMathTests
	tests/ScreenSpaceShadowsMathTests.cpp
	features/ScreenSpaceShadows/src/ScreenSpaceShadowsMath.h
)
configure_host_test(ScreenSpaceShadowsMathTests)
target_include_directories(ScreenSpaceShadowsMathTests PRIVATE
	"${CMAKE_SOURCE_DIR}/features/ScreenSpaceShadows/src"
)

add_test(NAME ScreenSpaceShadowsMath COMMAND ScreenSpaceShadowsMathTests)
set_tests_properties(ScreenSpaceShadowsMath PROPERTIES LABELS "render;unit")

add_executable(WetnessEffectsMathTests
	tests/WetnessEffectsMathTests.cpp
	features/WetnessEffects/src/WetnessMath.h
	src/FeatureBuffer.h
)
configure_host_test(WetnessEffectsMathTests)
target_include_directories(WetnessEffectsMathTests PRIVATE
	"${CMAKE_SOURCE_DIR}/features/WetnessEffects/src"
	"${CMAKE_SOURCE_DIR}/src"
)

add_test(
	NAME WetnessEffectsMath
	COMMAND WetnessEffectsMathTests
		"${CMAKE_SOURCE_DIR}/features/WetnessEffects/Shaders"
		"${CMAKE_SOURCE_DIR}/features/WetnessEffects/src/WetnessEffects.cpp"
)
set_tests_properties(WetnessEffectsMath PROPERTIES LABELS "render;shader;unit")

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
	add_executable(UpscalingPublicationTests
		tests/UpscalingPublicationTests.cpp
		features/Upscaling/src/UpscalingPublication.h
		src/Render/RendererContext.cpp
		src/Render/RendererContext.h
	)
	configure_host_test(UpscalingPublicationTests)
	target_precompile_headers(UpscalingPublicationTests PRIVATE
		"${CMAKE_SOURCE_DIR}/include/PCH.h"
	)
	target_compile_definitions(UpscalingPublicationTests PRIVATE
		_WINDOWS
		_AMD64_
		_UNICODE
		COMMONLIB_RUNTIMECOUNT=3
	)
	target_include_directories(UpscalingPublicationTests BEFORE PRIVATE
		"${MICROSOFT_DETOURS_INCLUDE_DIR}"
		"${CMAKE_BINARY_DIR}/FO4CommunityShaders"
		"${CMAKE_SOURCE_DIR}/features/Upscaling/src"
		"${CMAKE_SOURCE_DIR}/include"
		"${CMAKE_SOURCE_DIR}/src"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/include"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/lib/commonlib-shared/include"
	)
	target_link_libraries(UpscalingPublicationTests PRIVATE
		CommonLibF4::CommonLibF4
		Microsoft::DirectXTK
		Microsoft::DirectX-Headers
		bcrypt.lib
		d3d11.lib
		d3dcompiler.lib
		imgui::imgui
		magic_enum::magic_enum
		spdlog::spdlog
		tomlplusplus::tomlplusplus
	)

	add_test(NAME UpscalingPublication COMMAND UpscalingPublicationTests)
	set_tests_properties(UpscalingPublication PROPERTIES LABELS "render;unit")

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

add_executable(ScreenSpaceGIHistoryTests
	tests/ScreenSpaceGIHistoryTests.cpp
	features/ScreenSpaceGI/src/ScreenSpaceGIHistory.h
)
configure_host_test(ScreenSpaceGIHistoryTests)
target_include_directories(ScreenSpaceGIHistoryTests PRIVATE "${CMAKE_SOURCE_DIR}/features/ScreenSpaceGI/src")

add_test(NAME ScreenSpaceGIHistory COMMAND ScreenSpaceGIHistoryTests)
set_tests_properties(ScreenSpaceGIHistory PROPERTIES LABELS "render;unit")

add_executable(SharedDataDeclarationTests
	tests/SharedDataDeclarationTests.cpp
)
configure_host_test(SharedDataDeclarationTests)

add_test(
	NAME SharedDataDeclaration
	COMMAND SharedDataDeclarationTests "${CMAKE_SOURCE_DIR}/package/Shaders"
)
set_tests_properties(SharedDataDeclaration PROPERTIES LABELS "render;shader;unit")

if(NOT FO4CS_HOST_TESTS_ONLY)
	include(ShaderStage)
	set(FO4CS_SHADER_STAGE_DIR "${CMAKE_BINARY_DIR}/ShaderStage/Shaders")
	add_shader_stage(ShaderStage
		DESTINATION "${FO4CS_SHADER_STAGE_DIR}"
		DIRECTORIES
			"${CMAKE_SOURCE_DIR}/package/Shaders"
			"${CMAKE_SOURCE_DIR}/features/ScreenSpaceGI/Shaders"
			"${CMAKE_SOURCE_DIR}/features/ScreenSpaceShadows/Shaders"
			"${CMAKE_SOURCE_DIR}/features/Upscaling/Shaders"
			"${CMAKE_SOURCE_DIR}/features/WetnessEffects/Shaders"
		FILES
			"${CMAKE_SOURCE_DIR}/tests/shaders/SharedDataProbe.hlsl"
	)

	add_executable(ShaderCompileTests
		tests/ShaderCompileTests.cpp
		tests/generated/VertexShaderCompilePermutations.cpp
		tests/generated/VertexShaderCompilePermutations.h
		src/Render/ShaderInjection.cpp
		src/Render/ShaderInjectionCompileRequest.cpp
		src/Render/ShaderInjectionStaticFamilyRegistrations.cpp
		src/Render/ShaderInjectionStaticFamilyRegistrations.h
		src/Render/ShaderInjectionVariantFactory.cpp
		src/Render/ShaderInjectionVariantFactory.h
		src/Render/PixelShaderSwapModel.cpp
		src/Utils/CSSha1.cpp
		src/Utils/ShaderCompile.cpp
		src/Utils/ShaderCompile.h
	)
	configure_host_test(ShaderCompileTests)
	target_precompile_headers(ShaderCompileTests PRIVATE
		"${CMAKE_SOURCE_DIR}/include/PCH.h"
	)
	target_compile_definitions(ShaderCompileTests PRIVATE
		_WINDOWS
		_AMD64_
		_UNICODE
		COMMONLIB_RUNTIMECOUNT=3
	)
	target_include_directories(ShaderCompileTests BEFORE PRIVATE
		"${MICROSOFT_DETOURS_INCLUDE_DIR}"
		"${CMAKE_BINARY_DIR}/FO4CommunityShaders"
		"${CMAKE_SOURCE_DIR}/include"
		"${CMAKE_SOURCE_DIR}/src"
		"${CMAKE_SOURCE_DIR}/tests"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/include"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/lib/commonlib-shared/include"
	)
	target_link_libraries(ShaderCompileTests PRIVATE
		CommonLibF4::CommonLibF4
		Microsoft::DirectXTK
		Microsoft::DirectX-Headers
		bcrypt.lib
		d3dcompiler.lib
		imgui::imgui
		magic_enum::magic_enum
		spdlog::spdlog
		tomlplusplus::tomlplusplus
	)
	add_dependencies(ShaderCompileTests ShaderStage)

	add_test(
		NAME ShaderCompile
		COMMAND ShaderCompileTests
			"${FO4CS_SHADER_STAGE_DIR}"
	)
	set_tests_properties(ShaderCompile PROPERTIES LABELS "shader;unit")

	add_executable(ShaderCacheBenchmarkTests
		tests/ShaderCacheBenchmarkTests.cpp
		src/Render/ShaderInjection.cpp
		src/Render/ShaderInjectionCompileRequest.cpp
		src/Render/ShaderInjectionStaticFamilyRegistrations.cpp
		src/Render/ShaderInjectionStaticFamilyRegistrations.h
		src/Render/ShaderInjectionVariantFactory.cpp
		src/Render/ShaderInjectionVariantFactory.h
		src/Render/PixelShaderSwapModel.cpp
		src/Utils/CSSha1.cpp
		src/Utils/CSSha256.cpp
		src/Utils/CSSha256.h
		src/Utils/ShaderCompile.cpp
		src/Utils/ShaderCompile.h
		${FO4CS_SHADER_CACHE_SOURCES}
	)
	configure_host_test(ShaderCacheBenchmarkTests)
	target_precompile_headers(ShaderCacheBenchmarkTests PRIVATE
		"${CMAKE_SOURCE_DIR}/include/PCH.h"
	)
	target_compile_definitions(ShaderCacheBenchmarkTests PRIVATE
		_WINDOWS
		_AMD64_
		_UNICODE
		COMMONLIB_RUNTIMECOUNT=3
	)
	target_include_directories(ShaderCacheBenchmarkTests BEFORE PRIVATE
		"${MICROSOFT_DETOURS_INCLUDE_DIR}"
		"${CMAKE_BINARY_DIR}/FO4CommunityShaders"
		"${CMAKE_SOURCE_DIR}/include"
		"${CMAKE_SOURCE_DIR}/src"
		"${CMAKE_SOURCE_DIR}/tests"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/include"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/lib/commonlib-shared/include"
	)
	target_link_libraries(ShaderCacheBenchmarkTests PRIVATE
		CommonLibF4::CommonLibF4
		Microsoft::DirectXTK
		Microsoft::DirectX-Headers
		bcrypt.lib
		d3dcompiler.lib
		imgui::imgui
		magic_enum::magic_enum
		spdlog::spdlog
		tomlplusplus::tomlplusplus
	)
	add_dependencies(ShaderCacheBenchmarkTests ShaderStage)

	add_test(
		NAME ShaderCacheBenchmark
		COMMAND ShaderCacheBenchmarkTests "${FO4CS_SHADER_STAGE_DIR}"
	)
	set_tests_properties(ShaderCacheBenchmark PROPERTIES LABELS "shader;cache;benchmark")
endif()

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

add_executable(ShaderCacheTests
	tests/ShaderCacheTests.cpp
	src/Utils/CSSha256.cpp
	src/Utils/CSSha256.h
	src/Utils/ShaderCompile.cpp
	src/Utils/ShaderCompile.h
	${FO4CS_SHADER_CACHE_SOURCES}
)
configure_host_test(ShaderCacheTests)
target_link_libraries(ShaderCacheTests PRIVATE bcrypt.lib d3dcompiler.lib)

add_test(NAME ShaderCache COMMAND ShaderCacheTests)
set_tests_properties(ShaderCache PROPERTIES LABELS "shader;cache;unit")

if(NOT FO4CS_HOST_TESTS_ONLY)
	add_executable(ShaderInjectionRegistrationTests
		tests/ShaderInjectionRegistrationTests.cpp
		src/Render/ShaderInjection.cpp
		src/Render/ShaderInjectionCompileRequest.cpp
		src/Render/ShaderInjectionStaticFamilyRegistrations.cpp
		src/Render/ShaderInjectionStaticFamilyRegistrations.h
		src/Render/ShaderInjectionVariantFactory.cpp
		src/Render/ShaderInjectionVariantFactory.h
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
	target_link_libraries(ShaderInjectionRegistrationTests PRIVATE
		CommonLibF4::CommonLibF4
		Microsoft::DirectXTK
		Microsoft::DirectX-Headers
		bcrypt.lib
		d3d11.lib
		d3dcompiler.lib
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
