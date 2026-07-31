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
set_source_files_properties(
	features/ShaderCatalog/src/OrderlyExit.cpp
	PROPERTIES SKIP_PRECOMPILE_HEADERS ON
)

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
find_package(unofficial-sqlite3 CONFIG REQUIRED)
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
	# Lifecycle dependencies are declared through cs::Feature::GetDependencies().
	register_feature(MotionVectorFixes)
	register_feature(Upscaling)
	register_feature(FrameGeneration)
	register_feature(RenderDoc)
	register_feature(PerformanceOverlay)
	register_feature(Imagespace)
	register_feature(ScreenSpaceShadows)
	register_feature(ScreenSpaceGI)
	register_feature(WetnessEffects)
	register_feature(ShaderCatalog)
	register_feature(ShaderReplacement)
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
)
configure_host_test(ScreenSpaceShadowsMathTests)
target_include_directories(ScreenSpaceShadowsMathTests PRIVATE
	"${CMAKE_SOURCE_DIR}/features/ScreenSpaceShadows/src"
)

add_test(NAME ScreenSpaceShadowsMath COMMAND ScreenSpaceShadowsMathTests)
set_tests_properties(ScreenSpaceShadowsMath PROPERTIES LABELS "render;unit")

add_executable(SssDxbcPatchTests
	tests/SssDxbcPatchTests.cpp
	src/Render/DxbcPatchArtifact.cpp
	src/Render/DxbcPatchArtifact.h
	src/Render/PixelShaderSwapModel.cpp
	src/Render/PixelShaderSwapBroker.h
	src/Utils/CSSha1.cpp
	src/Utils/CSSha1.h
	src/Utils/CSSha256.cpp
	src/Utils/CSSha256.h
	features/ScreenSpaceShadows/src/SssDxbcPatchConsumer.h
	features/ScreenSpaceShadows/src/SssDxbcPatchRuntime.cpp
	features/ScreenSpaceShadows/src/SssDxbcPatchRuntime.h
	features/ScreenSpaceShadows/src/SssDxbcPatchResolver.cpp
	features/ScreenSpaceShadows/src/SssDxbcPatchResolver.h
	features/ScreenSpaceShadows/src/SssInjectionMode.h
	features/ScreenSpaceShadows/src/SssMaskBinding.cpp
	features/ScreenSpaceShadows/src/SssMaskBinding.h
)
configure_host_test(SssDxbcPatchTests)
target_compile_definitions(SssDxbcPatchTests PRIVATE
	FO4CS_SSS_DXBC_PATCH_TESTING=1
)
target_include_directories(SssDxbcPatchTests PRIVATE
	"${CMAKE_SOURCE_DIR}/features/ScreenSpaceShadows/src"
)
target_include_directories(SssDxbcPatchTests SYSTEM PRIVATE
	"${FO4CS_NLOHMANN_JSON_INCLUDE_DIR}"
)
target_link_libraries(SssDxbcPatchTests PRIVATE bcrypt.lib)

set(_sss_patch_artifact
	"${CMAKE_SOURCE_DIR}/package/F4SE/Plugins/FO4CommunityShaders/ScreenSpaceShadows/sss-dxbc-patch-plans.json")
set(_sss_default_config
	"${CMAKE_SOURCE_DIR}/package/F4SE/Plugins/FO4CommunityShaders/FO4CommunityShaders.toml")
add_test(
	NAME SssDxbcPatch
	COMMAND SssDxbcPatchTests
		"${_sss_patch_artifact}"
		"${_sss_default_config}"
)
set_tests_properties(SssDxbcPatch PROPERTIES
	LABELS "render;shader;unit"
)
add_test(
	NAME SssDxbcPatchPackage
	COMMAND SssDxbcPatchTests --package
		"${_sss_patch_artifact}"
		"${_sss_default_config}"
)
set_tests_properties(SssDxbcPatchPackage PROPERTIES
	LABELS "shader;package"
)
if(DEFINED ENV{DEVKIT_DIR})
	set(_sss_devkit_project
		"$ENV{DEVKIT_DIR}/config/projects/community-shaders.psd1")
else()
	set(_sss_devkit_project
		"${CMAKE_SOURCE_DIR}/../devkit/config/projects/community-shaders.psd1")
endif()
if(EXISTS "${_sss_devkit_project}")
	add_test(
		NAME SssDxbcPatchDeployContract
		COMMAND pwsh -NoProfile -ExecutionPolicy Bypass
			-File "${CMAKE_SOURCE_DIR}/scripts/shaders/test-sss-dxbc-deploy-contract.ps1"
			-RepoRoot "${CMAKE_SOURCE_DIR}"
			-ProjectConfig "${_sss_devkit_project}"
	)
	set_tests_properties(SssDxbcPatchDeployContract PROPERTIES
		LABELS "shader;package;deploy"
	)
endif()

add_executable(LogThrottleTests
	tests/LogThrottleTests.cpp
	include/LogThrottle.h
)
configure_host_test(LogThrottleTests)
target_include_directories(LogThrottleTests PRIVATE "${CMAKE_SOURCE_DIR}/include")
target_link_libraries(LogThrottleTests PRIVATE spdlog::spdlog)

add_test(NAME LogThrottle COMMAND LogThrottleTests)
set_tests_properties(LogThrottle PROPERTIES LABELS "logging;unit")

# Host-only offline consumer for the producer-published strict-v2 Composite ambient artifact.
# It is deliberately outside every FO4CS_HOST_TESTS_ONLY branch and never links into the plugin.
set(FO4CS_BSDF_COMPOSITE_OFFLINE_SOURCES
	offline/BsdfCompositeAmbientDxbcPatch/Artifact.cpp
	offline/BsdfCompositeAmbientDxbcPatch/Artifact.h
	offline/BsdfCompositeAmbientDxbcPatch/ArtifactInternal.h
	offline/BsdfCompositeAmbientDxbcPatch/CanonicalJson.cpp
	offline/BsdfCompositeAmbientDxbcPatch/CanonicalJson.h
	offline/BsdfCompositeAmbientDxbcPatch/ContractPins.h
	offline/BsdfCompositeAmbientDxbcPatch/SemanticValidation.cpp
	offline/BsdfCompositeAmbientDxbcPatch/Sha256.cpp
	offline/BsdfCompositeAmbientDxbcPatch/Sha256.h
)
add_library(BsdfCompositeAmbientDxbcPatchOffline STATIC
	${FO4CS_BSDF_COMPOSITE_OFFLINE_SOURCES}
)
configure_host_test(BsdfCompositeAmbientDxbcPatchOffline)
set_property(TARGET BsdfCompositeAmbientDxbcPatchOffline PROPERTY INCLUDE_DIRECTORIES "")
target_include_directories(BsdfCompositeAmbientDxbcPatchOffline PUBLIC
	"${CMAKE_SOURCE_DIR}/offline/BsdfCompositeAmbientDxbcPatch"
)
target_link_libraries(BsdfCompositeAmbientDxbcPatchOffline PRIVATE bcrypt.lib)

add_executable(BsdfCompositeAmbientDxbcPatchTests
	tests/BsdfCompositeAmbientDxbcPatchTests.cpp
)
configure_host_test(BsdfCompositeAmbientDxbcPatchTests)
target_link_libraries(BsdfCompositeAmbientDxbcPatchTests PRIVATE
	BsdfCompositeAmbientDxbcPatchOffline
)

add_test(
	NAME BsdfCompositeAmbientDxbcPatch
	COMMAND BsdfCompositeAmbientDxbcPatchTests
		"${CMAKE_SOURCE_DIR}/scripts/shaders/offline/bsdf-composite-ambient-dxbc-patch-plans.json"
)
set_tests_properties(BsdfCompositeAmbientDxbcPatch PROPERTIES
	LABELS "shader;offline;unit"
	TIMEOUT 300
)

# Hard configure-time isolation: no offline source, link, or target may reach the plugin.
function(fo4cs_relative_sources OUTPUT TARGET_NAME)
	get_target_property(_source_dir ${TARGET_NAME} SOURCE_DIR)
	get_target_property(_sources ${TARGET_NAME} SOURCES)
	set(_relative "")
	foreach(_source IN LISTS _sources)
		if(NOT IS_ABSOLUTE "${_source}")
			set(_source "${_source_dir}/${_source}")
		endif()
		file(RELATIVE_PATH _source "${CMAKE_SOURCE_DIR}" "${_source}")
		string(REPLACE "\\" "/" _source "${_source}")
		list(APPEND _relative "${_source}")
	endforeach()
	list(SORT _relative)
	set(${OUTPUT} "${_relative}" PARENT_SCOPE)
endfunction()

get_target_property(_offline_type BsdfCompositeAmbientDxbcPatchOffline TYPE)
if(NOT _offline_type STREQUAL "STATIC_LIBRARY")
	message(FATAL_ERROR "BsdfCompositeAmbientDxbcPatchOffline must stay a host-only static library")
endif()

fo4cs_relative_sources(_offline_sources BsdfCompositeAmbientDxbcPatchOffline)
set(_offline_allowed ${FO4CS_BSDF_COMPOSITE_OFFLINE_SOURCES})
list(SORT _offline_allowed)
if(NOT _offline_sources STREQUAL _offline_allowed)
	message(FATAL_ERROR
		"BsdfCompositeAmbientDxbcPatchOffline sources left the allow-list: ${_offline_sources}")
endif()

get_target_property(_offline_includes BsdfCompositeAmbientDxbcPatchOffline INCLUDE_DIRECTORIES)
list(FILTER _offline_includes EXCLUDE REGEX "^$")
set(_offline_includes_allowed
	"${CMAKE_SOURCE_DIR}/offline/BsdfCompositeAmbientDxbcPatch"
)
if(NOT _offline_includes STREQUAL _offline_includes_allowed)
	message(FATAL_ERROR
		"BsdfCompositeAmbientDxbcPatchOffline includes left the allow-list: ${_offline_includes}")
endif()

get_target_property(_offline_links BsdfCompositeAmbientDxbcPatchOffline LINK_LIBRARIES)
if(NOT _offline_links STREQUAL "bcrypt.lib")
	message(FATAL_ERROR
		"BsdfCompositeAmbientDxbcPatchOffline links left the allow-list: ${_offline_links}")
endif()

get_target_property(_offline_test_links BsdfCompositeAmbientDxbcPatchTests LINK_LIBRARIES)
if(NOT _offline_test_links STREQUAL "BsdfCompositeAmbientDxbcPatchOffline")
	message(FATAL_ERROR
		"BsdfCompositeAmbientDxbcPatchTests links left the allow-list: ${_offline_test_links}")
endif()

if(TARGET FO4CommunityShaders)
	get_target_property(_plugin_sources FO4CommunityShaders SOURCES)
	foreach(_source IN LISTS _plugin_sources)
		string(REPLACE "\\" "/" _source "${_source}")
		if(_source MATCHES "offline/BsdfCompositeAmbientDxbcPatch")
			message(FATAL_ERROR "FO4CommunityShaders must not compile an offline consumer source")
		endif()
	endforeach()
	get_target_property(_plugin_links FO4CommunityShaders LINK_LIBRARIES)
	foreach(_link IN LISTS _plugin_links)
		if(_link MATCHES "BsdfCompositeAmbientDxbcPatch")
			message(FATAL_ERROR "FO4CommunityShaders must not link the offline consumer")
		endif()
	endforeach()
endif()

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
		"${CMAKE_SOURCE_DIR}/shaders/lighting"
)
set_tests_properties(ShaderCompile PROPERTIES LABELS "shader;unit")

add_executable(ShaderCatalogTests
	tests/ShaderCatalogTests.cpp
	features/ShaderCatalog/src/CatalogDB.cpp
	features/ShaderCatalog/src/CatalogDB.h
	features/ShaderCatalog/src/Provenance.cpp
	features/ShaderCatalog/src/Provenance.h
	features/ShaderCatalog/src/PixelShaderTracker.cpp
	features/ShaderCatalog/src/PixelShaderTracker.h
	features/ShaderCatalog/src/RouteCaptureCoordinator.cpp
	features/ShaderCatalog/src/RouteCaptureCoordinator.h
	features/ShaderCatalog/src/ShaderShape.cpp
	features/ShaderCatalog/src/ShaderShape.h
	src/Render/PixelShaderSwapModel.cpp
	src/Render/PixelShaderSwapBroker.h
	src/Utils/CSSha1.cpp
	src/Utils/CSSha1.h
)
configure_host_test(ShaderCatalogTests)
target_include_directories(ShaderCatalogTests PRIVATE
	"${CMAKE_SOURCE_DIR}/include"
	"${CMAKE_SOURCE_DIR}/features/ShaderCatalog/src"
)
target_compile_definitions(
	ShaderCatalogTests PRIVATE FO4CS_SHADER_CATALOG_TESTING=1)
target_link_libraries(ShaderCatalogTests PRIVATE
	unofficial::sqlite3::sqlite3
	spdlog::spdlog
	bcrypt.lib
	d3dcompiler.lib
)

add_test(NAME ShaderCatalog COMMAND ShaderCatalogTests)
set_tests_properties(ShaderCatalog PROPERTIES LABELS "shader;catalog;unit" TIMEOUT 120)

add_executable(PixelShaderSwapTests
	tests/PixelShaderSwapTests.cpp
	src/Render/EnginePixelShaderLookup.cpp
	src/Render/EnginePixelShaderLookup.h
	src/Render/PixelShaderSwapModel.cpp
	src/Render/PixelShaderSwapBroker.h
	src/Render/ShaderVariantResolver.cpp
	src/Render/ShaderVariantResolver.h
	src/Utils/CSSha1.cpp
	src/Utils/CSSha1.h
)
configure_host_test(PixelShaderSwapTests)
target_compile_definitions(
	PixelShaderSwapTests PRIVATE FO4CS_ENGINE_LOOKUP_TESTING=1)
target_link_libraries(PixelShaderSwapTests PRIVATE bcrypt.lib)

add_test(NAME PixelShaderSwap COMMAND PixelShaderSwapTests)
set_tests_properties(PixelShaderSwap PROPERTIES LABELS "render;shader;unit")

if(NOT FO4CS_HOST_TESTS_ONLY)
	add_executable(ShaderInjectionRegistrationTests
		tests/ShaderInjectionRegistrationTests.cpp
		features/ScreenSpaceShadows/src/SssDxbcPatchResolver.h
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
		FO4CS_SHADER_INJECTION_TESTING=1
	)
	target_include_directories(ShaderInjectionRegistrationTests BEFORE PRIVATE
		"${MICROSOFT_DETOURS_INCLUDE_DIR}"
		"${CMAKE_BINARY_DIR}/FO4CommunityShaders"
		"${CMAKE_SOURCE_DIR}/include"
		"${CMAKE_SOURCE_DIR}/src"
		"${CMAKE_SOURCE_DIR}/features/ScreenSpaceShadows/src"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/include"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/lib/commonlib-shared/include"
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
	set_tests_properties(ShaderInjectionRegistration PROPERTIES
		LABELS "render;shader;unit"
	)
endif()

if(FO4CS_HOST_TESTS_ONLY)
	find_package(xbyak CONFIG REQUIRED)
	set(PROJECT_VERSION_MAJOR "${FO4CS_VERSION_MAJOR}")
	set(PROJECT_VERSION_MINOR "${FO4CS_VERSION_MINOR}")
	set(PROJECT_VERSION_PATCH "${FO4CS_VERSION_PATCH}")
	set(PROJECT_NAME "FO4CommunityShaders")
	configure_file(
		"${CMAKE_SOURCE_DIR}/cmake/Plugin.h.in"
		"${CMAKE_BINARY_DIR}/FO4CommunityShaders/Plugin.h"
		@ONLY
	)
	add_library(ShaderCatalogCompileChecks OBJECT
		features/ShaderCatalog/src/Hooks.cpp
		features/ShaderCatalog/src/OrderlyExit.cpp
		features/ShaderCatalog/src/PixelShaderTracker.cpp
		features/ShaderCatalog/src/RouteCaptureCoordinator.cpp
		features/ShaderCatalog/src/ShaderCatalog.cpp
		features/ShaderCatalog/src/SubclassAttribution.cpp
		src/Render/EnginePixelShaderLookup.cpp
		src/Render/PixelShaderSwapBroker.cpp
		src/Render/PixelShaderSwapModel.cpp
		src/Render/ShaderSubclassContext.cpp
		src/Render/ShaderSubclassHooks.cpp
		src/Render/ShaderVariantResolver.cpp
		src/Render/ShaderVariantRuntimeResolver.cpp
	)
	configure_host_test(ShaderCatalogCompileChecks)
	target_precompile_headers(ShaderCatalogCompileChecks PRIVATE
		"${CMAKE_SOURCE_DIR}/include/PCH.h"
	)
	target_compile_definitions(ShaderCatalogCompileChecks PRIVATE
		_WINDOWS
		_AMD64_
		_UNICODE
		COMMONLIB_RUNTIMECOUNT=3
	)
	target_compile_options(ShaderCatalogCompileChecks PRIVATE
		/wd4200
		/wd4201
		/wd4324
	)
	target_include_directories(ShaderCatalogCompileChecks BEFORE PRIVATE
		"${MICROSOFT_DETOURS_INCLUDE_DIR}"
		"${CMAKE_BINARY_DIR}/FO4CommunityShaders"
		"${CMAKE_SOURCE_DIR}/include"
		"${CMAKE_SOURCE_DIR}/src"
		"${CMAKE_SOURCE_DIR}/features/ShaderCatalog/src"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/include"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/lib/commonlib-shared/include"
	)
	target_link_libraries(ShaderCatalogCompileChecks PRIVATE
		debug ${MICROSOFT_DETOURS_LIBRARY_DEBUG}
		optimized ${MICROSOFT_DETOURS_LIBRARY_RELEASE}
		Microsoft::DirectXTK
		Microsoft::DirectX-Headers
		imgui::imgui
		magic_enum::magic_enum
		spdlog::spdlog
		tomlplusplus::tomlplusplus
		xbyak::xbyak
	)

	add_executable(ShaderCatalogOrderlyExitTests
		tests/ShaderCatalogOrderlyExitTests.cpp
		features/ShaderCatalog/src/OrderlyExit.cpp
	)
	configure_host_test(ShaderCatalogOrderlyExitTests)
	target_compile_definitions(ShaderCatalogOrderlyExitTests PRIVATE
		_WINDOWS
		_AMD64_
		_UNICODE
		COMMONLIB_RUNTIMECOUNT=3
		FO4CS_SHADER_CATALOG_TESTING
	)
	target_include_directories(ShaderCatalogOrderlyExitTests BEFORE PRIVATE
		"${MICROSOFT_DETOURS_INCLUDE_DIR}"
		"${CMAKE_SOURCE_DIR}/include"
		"${CMAKE_SOURCE_DIR}/features/ShaderCatalog/src"
	)
	target_link_libraries(ShaderCatalogOrderlyExitTests PRIVATE
		debug ${CMAKE_SOURCE_DIR}/include/detours/Debug/detours.lib
		optimized ${CMAKE_SOURCE_DIR}/include/detours/Release/detours.lib
		debug ${MICROSOFT_DETOURS_LIBRARY_DEBUG}
		optimized ${MICROSOFT_DETOURS_LIBRARY_RELEASE}
	)
	add_test(
		NAME ShaderCatalogOrderlyExit
		COMMAND ShaderCatalogOrderlyExitTests
	)
	set_tests_properties(ShaderCatalogOrderlyExit PROPERTIES
		LABELS "catalog;shader;unit"
	)
endif()

set(_shader_roundtrip_script "${CMAKE_SOURCE_DIR}/scripts/shaders/verify-shader-roundtrip.ps1")
set(_shader_roundtrip_test_script "${CMAKE_SOURCE_DIR}/scripts/shaders/test-verify-shader-roundtrip.ps1")
add_test(
	NAME ShaderRoundtripManifestUnit
	COMMAND pwsh -NoProfile -ExecutionPolicy Bypass
		-File "${_shader_roundtrip_test_script}"
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
set_tests_properties(
	ShaderRoundtripManifestUnit
	PROPERTIES
	LABELS "shader;unit"
	TIMEOUT 120
)
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
