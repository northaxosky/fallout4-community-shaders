cmake_minimum_required(VERSION 3.21)

project(
	FO4CommunityShaders
	VERSION 0.1.0
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

add_subdirectory(cmake/Streamline)

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
find_package(spdlog CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)
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
	target_sources(FO4CommunityShaders PRIVATE
		${CORE_SOURCES}
	)

	include(Common)
	configure_xse_plugin(FO4CommunityShaders)

	target_link_libraries(FO4CommunityShaders PRIVATE
		debug ${CMAKE_SOURCE_DIR}/extern/detours/Debug/detours.lib
		optimized ${CMAKE_SOURCE_DIR}/extern/detours/Release/detours.lib
		Microsoft::DirectXTK
		Microsoft::DirectXTex
		Microsoft::DirectX-Headers
		bcrypt.lib
		magic_enum::magic_enum
		d3dcompiler.lib
		version.lib
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
	register_feature(FrameGeneration)
	register_feature(ScreenSpaceShadows)
	register_feature(TerrainShadows)
	register_feature(ScreenSpaceGI)
	register_feature(InverseSquareLighting)
	register_feature(ExponentialHeightFog)
	register_feature(DynamicCubemaps)
	register_feature(WetnessEffects)
	register_feature(WaterEffects)

	# The Streamline verifier is compiled into the host; FidelityFX builds from source.
	include(FidelityFX-SDK)
endif()

enable_testing()

function(configure_host_test TARGET_NAME)
	target_compile_features(${TARGET_NAME} PRIVATE cxx_std_23)
	target_include_directories(${TARGET_NAME} PRIVATE
		"${CMAKE_SOURCE_DIR}/src"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/lib/dearmoddingui-api/include"
	)
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

add_executable(TemporalPipelineStateTests
	tests/TemporalPipelineStateTests.cpp
	src/Render/TemporalPipelineState.h
	src/Render/TemporalRenderSizing.h
)
configure_host_test(TemporalPipelineStateTests)

add_test(NAME TemporalPipelineState COMMAND TemporalPipelineStateTests)
set_tests_properties(TemporalPipelineState PROPERTIES LABELS "render;state;unit")

add_executable(StreamlinePresentationTests
	tests/StreamlinePresentationTests.cpp
	features/Upscaling/src/StreamlineInterfaceUpgrade.cpp
	features/Upscaling/src/StreamlineInterfaceUpgrade.h
)
configure_host_test(StreamlinePresentationTests)
target_include_directories(StreamlinePresentationTests PRIVATE
	"${CMAKE_SOURCE_DIR}/features/Upscaling/src"
	"${CMAKE_SOURCE_DIR}/extern/Streamline/include"
)

add_test(NAME StreamlinePresentation COMMAND StreamlinePresentationTests)
set_tests_properties(StreamlinePresentation PROPERTIES LABELS "render;streamline;unit")

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

add_executable(TerrainShadowsMathTests
	tests/TerrainShadowsMathTests.cpp
	features/TerrainShadows/src/TerrainShadowsMath.h
)
configure_host_test(TerrainShadowsMathTests)
target_include_directories(TerrainShadowsMathTests PRIVATE
	"${CMAKE_SOURCE_DIR}/features/TerrainShadows/src"
)

add_test(NAME TerrainShadowsMath COMMAND TerrainShadowsMathTests)
set_tests_properties(TerrainShadowsMath PROPERTIES LABELS "render;shader;unit")

add_executable(TerrainShadowsResizeTests
	tests/TerrainShadowsResizeTests.cpp
	features/TerrainShadows/src/HeightMapResize.cpp
	features/TerrainShadows/src/HeightMapResize.h
)
configure_host_test(TerrainShadowsResizeTests)
target_include_directories(TerrainShadowsResizeTests PRIVATE
	"${CMAKE_SOURCE_DIR}/features/TerrainShadows/src"
)
target_link_libraries(TerrainShadowsResizeTests PRIVATE Microsoft::DirectXTex)

add_test(NAME TerrainShadowsResize COMMAND TerrainShadowsResizeTests)
set_tests_properties(TerrainShadowsResize PROPERTIES LABELS "render;unit")

add_executable(WetnessEffectsMathTests
	tests/WetnessEffectsMathTests.cpp
	features/WetnessEffects/src/WetnessMath.h
)
configure_host_test(WetnessEffectsMathTests)
target_include_directories(WetnessEffectsMathTests PRIVATE
	"${CMAKE_SOURCE_DIR}/features/WetnessEffects/src"
)

add_test(NAME WetnessEffectsMath COMMAND WetnessEffectsMathTests)
set_tests_properties(WetnessEffectsMath PROPERTIES LABELS "render;shader;unit")

add_executable(InverseSquareLightingMathTests
	tests/InverseSquareLightingMathTests.cpp
	features/InverseSquareLighting/src/InverseSquareLightingMath.h
)
configure_host_test(InverseSquareLightingMathTests)
target_include_directories(InverseSquareLightingMathTests PRIVATE
	"${CMAKE_SOURCE_DIR}/features/InverseSquareLighting/src"
)

add_test(NAME InverseSquareLightingMath COMMAND InverseSquareLightingMathTests)
set_tests_properties(
	InverseSquareLightingMath
	PROPERTIES LABELS "render;shader;unit"
)

add_executable(ExponentialHeightFogMathTests
	tests/ExponentialHeightFogMathTests.cpp
	features/ExponentialHeightFog/src/ExponentialHeightFogMath.h
)
configure_host_test(ExponentialHeightFogMathTests)
target_include_directories(ExponentialHeightFogMathTests PRIVATE
	"${CMAKE_SOURCE_DIR}/features/ExponentialHeightFog/src"
)

add_test(NAME ExponentialHeightFogMath COMMAND ExponentialHeightFogMathTests)
set_tests_properties(
	ExponentialHeightFogMath
	PROPERTIES LABELS "render;shader;unit"
)

add_executable(WaterEffectsMathTests
	tests/WaterEffectsMathTests.cpp
	features/WaterEffects/src/WaterEffectsMath.h
)
configure_host_test(WaterEffectsMathTests)
target_include_directories(WaterEffectsMathTests PRIVATE
	"${CMAKE_SOURCE_DIR}/features/WaterEffects/src"
)

add_test(NAME WaterEffectsMath COMMAND WaterEffectsMathTests)
set_tests_properties(
	WaterEffectsMath
	PROPERTIES LABELS "render;shader;unit"
)

add_executable(HostIntegrationTests
	tests/HostIntegrationTests.cpp
	src/Host/HostClientOptions.h
	src/Host/HostPageCatalog.cpp
	src/Host/HostPageCatalog.h
)
configure_host_test(HostIntegrationTests)
target_link_libraries(HostIntegrationTests PRIVATE tomlplusplus::tomlplusplus)

add_test(NAME HostIntegration COMMAND HostIntegrationTests)
set_tests_properties(HostIntegration PROPERTIES LABELS "menu;host;unit")

add_library(StreamlineModuleFixture SHARED tests/StreamlineModuleFixture.cpp)
configure_host_test(StreamlineModuleFixture)

add_executable(StreamlineModuleTests
	tests/StreamlineModuleTests.cpp
	src/Utils/StreamlineModule.cpp
)
configure_host_test(StreamlineModuleTests)
target_link_libraries(StreamlineModuleTests PRIVATE Streamline psapi.lib)
add_dependencies(StreamlineModuleTests StreamlineModuleFixture)
add_test(
	NAME StreamlineModule
	COMMAND StreamlineModuleTests "$<TARGET_FILE:StreamlineModuleFixture>"
)
set_tests_properties(StreamlineModule PROPERTIES LABELS "streamline;security;unit")

if(NOT FO4CS_HOST_TESTS_ONLY)
	add_executable(UpscalingPublicationTests
		tests/UpscalingPublicationTests.cpp
		features/Upscaling/src/ProviderOutputPreview.h
		features/Upscaling/src/UpscalingPublication.h
		src/Render/Annotation.cpp
		src/Render/Annotation.h
		src/Render/RenderUIPathGate.h
		src/Render/RendererContext.cpp
		src/Render/RendererContext.h
	)
	configure_host_test(UpscalingPublicationTests)
	target_precompile_headers(UpscalingPublicationTests PRIVATE
		"${CMAKE_SOURCE_DIR}/src/PCH.h"
	)
	target_compile_definitions(UpscalingPublicationTests PRIVATE
		_WINDOWS
		_AMD64_
		_UNICODE
		COMMONLIB_RUNTIMECOUNT=3
	)
	target_include_directories(UpscalingPublicationTests BEFORE PRIVATE
		"${CMAKE_BINARY_DIR}/FO4CommunityShaders"
		"${CMAKE_SOURCE_DIR}/features/Upscaling/src"
		"${CMAKE_SOURCE_DIR}/extern"
		"${CMAKE_SOURCE_DIR}/src"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/include"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/lib/commonlib-shared/include"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/lib/dearmoddingui-api/include"
	)
	target_link_libraries(UpscalingPublicationTests PRIVATE
		CommonLibF4::CommonLibF4
		Microsoft::DirectXTK
		Microsoft::DirectX-Headers
		bcrypt.lib
		d3d11.lib
		d3dcompiler.lib
		dxguid.lib
		magic_enum::magic_enum
		spdlog::spdlog
	)

	add_test(
		NAME UpscalingPublication
		COMMAND UpscalingPublicationTests
			"${CMAKE_SOURCE_DIR}/features/Upscaling/Shaders/Upscaling/SpatialFallbackPS.hlsl"
			"${CMAKE_SOURCE_DIR}/features/Upscaling/Shaders/Upscaling/UpscaleVS.hlsl"
	)
	set_tests_properties(UpscalingPublication PROPERTIES LABELS "render;unit")

	add_executable(ProjectionMathTests
		tests/ProjectionMathTests.cpp
	)
	configure_host_test(ProjectionMathTests)
	target_link_libraries(ProjectionMathTests PRIVATE CommonLibF4::CommonLibF4 Microsoft::DirectXMath)

	add_test(NAME ProjectionMath COMMAND ProjectionMathTests)
	set_tests_properties(ProjectionMath PROPERTIES LABELS "render;unit")
endif()

add_executable(FrameBufferTests
	tests/FrameBufferTests.cpp
	src/Render/FrameBufferMath.h
)
configure_host_test(FrameBufferTests)
target_link_libraries(FrameBufferTests PRIVATE Microsoft::DirectXMath)

add_test(NAME FrameBuffer COMMAND FrameBufferTests)
set_tests_properties(FrameBuffer PROPERTIES LABELS "render;shader;unit")

add_executable(FrameGenerationCameraTests
	tests/FrameGenerationCameraTests.cpp
	features/Upscaling/src/SuperResolutionFov.h
	src/Render/FrameBufferMath.h
)
configure_host_test(FrameGenerationCameraTests)
target_include_directories(FrameGenerationCameraTests PRIVATE
	"${CMAKE_SOURCE_DIR}/features/Upscaling/src"
)
target_link_libraries(FrameGenerationCameraTests PRIVATE Microsoft::DirectXMath)

add_test(
	NAME FrameGenerationCamera
	COMMAND FrameGenerationCameraTests
)
set_tests_properties(FrameGenerationCamera PROPERTIES LABELS "render;unit")

add_executable(FrameGenerationContractTests
	tests/FrameGenerationContractTests.cpp
	src/Render/FrameGenerationCpuTiming.h
	src/Render/FrameGenerationOrchestration.h
	features/FrameGeneration/src/FidelityFXFrameGenerationContract.h
	features/FrameGeneration/src/StreamlineFrameGenerationContract.h
)
configure_host_test(FrameGenerationContractTests)
target_include_directories(FrameGenerationContractTests PRIVATE
	"${CMAKE_SOURCE_DIR}/src"
	"${CMAKE_SOURCE_DIR}/features/FrameGeneration/src"
	"${CMAKE_SOURCE_DIR}/extern"
	"${CMAKE_SOURCE_DIR}/extern/Streamline/include"
)

add_test(
	NAME FrameGenerationContract
	COMMAND FrameGenerationContractTests
)
set_tests_properties(FrameGenerationContract PROPERTIES LABELS "render;contract;unit")

add_executable(DXGISwapChainProxyTests
	tests/DXGISwapChainProxyTests.cpp
	features/Upscaling/src/DXGISwapChainProxy.cpp
)
configure_host_test(DXGISwapChainProxyTests)
target_include_directories(DXGISwapChainProxyTests PRIVATE
	"${CMAKE_SOURCE_DIR}/features/Upscaling/src"
)
target_link_libraries(DXGISwapChainProxyTests PRIVATE
	d3d11.lib
	dxgi.lib
)
add_test(
	NAME DXGISwapChainProxy
	COMMAND DXGISwapChainProxyTests
)
set_tests_properties(
	DXGISwapChainProxy
	PROPERTIES LABELS "render;contract;unit")

if(NOT FO4CS_HOST_TESTS_ONLY)
	add_executable(FrameGenerationRetirementGpuTests
		tests/FrameGenerationRetirementGpuTests.cpp
	)
	configure_host_test(FrameGenerationRetirementGpuTests)
	target_include_directories(FrameGenerationRetirementGpuTests BEFORE PRIVATE
		"${CMAKE_BINARY_DIR}/FO4CommunityShaders"
		"${CMAKE_SOURCE_DIR}/extern"
	)
	target_link_libraries(FrameGenerationRetirementGpuTests PRIVATE
		Microsoft::DirectX-Headers
		d3d11.lib
		d3d12.lib
		dxgi.lib
	)
	add_test(
		NAME FrameGenerationRetirementGpu
		COMMAND FrameGenerationRetirementGpuTests
	)
	set_tests_properties(
		FrameGenerationRetirementGpu
		PROPERTIES LABELS "render;gpu;contract")
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

if(NOT FO4CS_HOST_TESTS_ONLY)
	include(ShaderStage)
	set(FO4CS_SHADER_STAGE_DIR "${CMAKE_BINARY_DIR}/ShaderStage/Shaders")
	add_shader_stage(ShaderStage
		DESTINATION "${FO4CS_SHADER_STAGE_DIR}"
		EXCLUDE_REGEXES
			"^Upscaling/XeSS(/|$)"
		DIRECTORIES
			"${CMAKE_SOURCE_DIR}/package/Shaders"
			"${CMAKE_SOURCE_DIR}/features/ScreenSpaceGI/Shaders"
			"${CMAKE_SOURCE_DIR}/features/ScreenSpaceShadows/Shaders"
			"${CMAKE_SOURCE_DIR}/features/TerrainShadows/Shaders"
			"${CMAKE_SOURCE_DIR}/features/Upscaling/Shaders"
			"${CMAKE_SOURCE_DIR}/features/FrameGeneration/Shaders"
			"${CMAKE_SOURCE_DIR}/features/InverseSquareLighting/Shaders"
			"${CMAKE_SOURCE_DIR}/features/ExponentialHeightFog/Shaders"
			"${CMAKE_SOURCE_DIR}/features/DynamicCubemaps/Shaders"
			"${CMAKE_SOURCE_DIR}/features/WetnessEffects/Shaders"
			"${CMAKE_SOURCE_DIR}/features/WaterEffects/Shaders"
		FILES
			"${CMAKE_SOURCE_DIR}/tests/shaders/SharedDataProbe.hlsl"
	)

	add_executable(ShaderCompileTests
		tests/ShaderCompileTests.cpp
		tests/ShaderCompileIdentityWitnesses.inl
		src/Render/ShaderInjection.cpp
		src/Render/ShaderInjectionCompileRequest.cpp
		src/Render/ShaderFamilyDescriptor.cpp
		src/Render/ShaderFamilyDescriptor.h
		src/Render/ShaderSubclassContext.cpp
		src/Render/ShaderSubclassContext.h
		src/Render/SharedDataDispatchScope.cpp
		src/Render/PixelShaderSwapModel.cpp
		src/Utils/CSSha1.cpp
		src/Utils/CSSha256.cpp
		src/Utils/CSSha256.h
		src/Utils/ShaderCompile.cpp
		src/Utils/ShaderCompile.h
		${FO4CS_EMBEDDED_SHADER_VARIANT_SOURCES}
	)
	configure_host_test(ShaderCompileTests)
	target_precompile_headers(ShaderCompileTests PRIVATE
		"${CMAKE_SOURCE_DIR}/src/PCH.h"
	)
	target_compile_definitions(ShaderCompileTests PRIVATE
		_WINDOWS
		_AMD64_
		_UNICODE
		COMMONLIB_RUNTIMECOUNT=3
	)
	target_include_directories(ShaderCompileTests BEFORE PRIVATE
		"${CMAKE_BINARY_DIR}/FO4CommunityShaders"
		"${CMAKE_SOURCE_DIR}/extern"
		"${CMAKE_SOURCE_DIR}/src"
		"${CMAKE_SOURCE_DIR}/tests"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/include"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/lib/commonlib-shared/include"
	)
	target_link_libraries(ShaderCompileTests PRIVATE
		debug ${CMAKE_SOURCE_DIR}/extern/detours/Debug/detours.lib
		optimized ${CMAKE_SOURCE_DIR}/extern/detours/Release/detours.lib
		CommonLibF4::CommonLibF4
		Microsoft::DirectXTK
		Microsoft::DirectX-Headers
		bcrypt.lib
		d3dcompiler.lib
		magic_enum::magic_enum
		spdlog::spdlog
	)
	add_dependencies(ShaderCompileTests ShaderStage)

	add_test(
		NAME ShaderCompile
		COMMAND ShaderCompileTests
			"${FO4CS_SHADER_STAGE_DIR}"
	)
	set_tests_properties(ShaderCompile PROPERTIES LABELS "shader;unit")

endif()

add_executable(PixelShaderSwapTests
	tests/PixelShaderSwapTests.cpp
	src/Render/PixelShaderSwapModel.cpp
	src/Render/PixelShaderSwapBroker.h
	src/Render/ShaderVariantResolver.cpp
	src/Render/ShaderVariantResolver.h
)
configure_host_test(PixelShaderSwapTests)

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
target_link_libraries(
	ShaderCacheTests PRIVATE bcrypt.lib d3dcompiler.lib version.lib
)

add_test(NAME ShaderCache COMMAND ShaderCacheTests)
set_tests_properties(ShaderCache PROPERTIES LABELS "shader;cache;unit")

add_executable(ShaderVariantCompilationTests
	tests/ShaderVariantCompilationTests.cpp
	src/Render/ShaderVariantCompilation.cpp
)
configure_host_test(ShaderVariantCompilationTests)
target_link_libraries(ShaderVariantCompilationTests PRIVATE d3d11.lib)

add_test(
	NAME ShaderVariantCompilation
	COMMAND ShaderVariantCompilationTests
)
set_tests_properties(ShaderVariantCompilation PROPERTIES
	LABELS "render;shader;cache;unit"
)

if(NOT FO4CS_HOST_TESTS_ONLY)
	add_executable(ShaderInjectionRegistrationTests
		tests/ShaderInjectionRegistrationTests.cpp
		src/Render/ShaderInjection.cpp
		src/Render/ShaderInjectionCompileRequest.cpp
		src/Render/ShaderFamilyDescriptor.cpp
		src/Render/ShaderFamilyDescriptor.h
		src/Render/SharedDataDispatchScope.cpp
		src/Render/PixelShaderSwapBroker.cpp
		src/Render/PixelShaderSwapModel.cpp
		src/Render/ShaderSubclassContext.cpp
		src/Render/ShaderVariantResolver.cpp
		src/Render/ShaderVariantRuntimeResolver.cpp
	)
	configure_host_test(ShaderInjectionRegistrationTests)
	target_precompile_headers(ShaderInjectionRegistrationTests PRIVATE
		"${CMAKE_SOURCE_DIR}/src/PCH.h"
	)
	target_compile_definitions(ShaderInjectionRegistrationTests PRIVATE
		_WINDOWS
		_AMD64_
		_UNICODE
		COMMONLIB_RUNTIMECOUNT=3
		FO4CS_SHADER_INJECTION_TESTING
	)
	target_include_directories(ShaderInjectionRegistrationTests BEFORE PRIVATE
		"${CMAKE_BINARY_DIR}/FO4CommunityShaders"
		"${CMAKE_SOURCE_DIR}/extern"
		"${CMAKE_SOURCE_DIR}/src"
		"${CMAKE_SOURCE_DIR}/tests"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/include"
		"${CMAKE_SOURCE_DIR}/extern/CommonLibF4/lib/commonlib-shared/include"
	)
	target_link_libraries(ShaderInjectionRegistrationTests PRIVATE
		debug ${CMAKE_SOURCE_DIR}/extern/detours/Debug/detours.lib
		optimized ${CMAKE_SOURCE_DIR}/extern/detours/Release/detours.lib
		CommonLibF4::CommonLibF4
		Microsoft::DirectXTK
		Microsoft::DirectX-Headers
		bcrypt.lib
		d3d11.lib
		d3dcompiler.lib
		magic_enum::magic_enum
		spdlog::spdlog
	)

	add_test(
		NAME ShaderInjectionDescriptorSelection
		COMMAND ShaderInjectionRegistrationTests --baseline-ownership
	)
	add_test(
		NAME ShaderInjectionClaimLedger
		COMMAND ShaderInjectionRegistrationTests --claim-ledger
	)
	add_test(
		NAME ShaderInjectionNativeObserver
		COMMAND ShaderInjectionRegistrationTests --native-observer
	)
	add_test(
		NAME ShaderInjectionLazyPreparation
		COMMAND ShaderInjectionRegistrationTests --lazy-preparation
	)
	add_test(
		NAME ShaderInjectionNativeOutcomeCache
		COMMAND ShaderInjectionRegistrationTests --native-outcome-cache
	)
	add_test(
		NAME ShaderInjectionComputeDispatchBridge
		COMMAND ShaderInjectionRegistrationTests --dispatch-bridge
	)
	add_test(
		NAME ShaderInjectionComputeHooksMissing
		COMMAND ShaderInjectionRegistrationTests --compute-hooks-missing
	)
	add_test(
		NAME ShaderInjectionComputeBaselineOnly
		COMMAND ShaderInjectionRegistrationTests --compute-baseline-only
	)
	add_test(
		NAME ShaderInjectionComputePhase
		COMMAND ShaderInjectionRegistrationTests --compute-phase
	)
	add_test(
		NAME ShaderInjectionComputeDescriptor
		COMMAND ShaderInjectionRegistrationTests --compute-descriptor
	)
	add_test(
		NAME ShaderInjectionContributorConflict
		COMMAND ShaderInjectionRegistrationTests --contributor-conflict
	)
	set_tests_properties(ShaderInjectionDescriptorSelection PROPERTIES
		LABELS "render;shader;unit"
	)
	set_tests_properties(ShaderInjectionClaimLedger PROPERTIES
		LABELS "render;shader;unit"
	)
	set_tests_properties(ShaderInjectionNativeObserver PROPERTIES
		LABELS "render;shader;unit"
	)
	set_tests_properties(ShaderInjectionLazyPreparation PROPERTIES
		LABELS "render;shader;unit"
		TIMEOUT 30
	)
	set_tests_properties(ShaderInjectionNativeOutcomeCache PROPERTIES
		LABELS "render;shader;cache;unit"
		TIMEOUT 30
	)
	set_tests_properties(ShaderInjectionComputeDispatchBridge PROPERTIES
		LABELS "render;shader;unit"
	)
	set_tests_properties(ShaderInjectionComputeHooksMissing PROPERTIES
		LABELS "render;shader;unit"
	)
	set_tests_properties(ShaderInjectionComputeBaselineOnly PROPERTIES
		LABELS "render;shader;unit"
	)
	set_tests_properties(ShaderInjectionComputePhase PROPERTIES
		LABELS "render;shader;unit"
	)
	set_tests_properties(ShaderInjectionComputeDescriptor PROPERTIES
		LABELS "render;shader;unit"
	)
	set_tests_properties(ShaderInjectionContributorConflict PROPERTIES
		LABELS "render;shader;unit"
	)
endif()
