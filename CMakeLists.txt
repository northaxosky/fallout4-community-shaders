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

# Dear-Modding-FO4/commonlibf4 ships only xmake.lua, so inject our CMakeLists template before add_subdirectory.
configure_file(
	${CMAKE_SOURCE_DIR}/cmake/CommonLibF4.cmake.txt
	${CMAKE_SOURCE_DIR}/extern/CommonLibF4/CMakeLists.txt
	COPYONLY
)
add_subdirectory(extern/CommonLibF4 CommonLibF4 EXCLUDE_FROM_ALL)

# vcpkg packages
find_package(directxtk CONFIG REQUIRED)
find_package(directxtex CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

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
configure_xse_plugin(FO4CommunityShaders 0 1 0)

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
)

# Features attach their own sources to FO4CommunityShaders via target_sources.
register_feature(MotionVectorFixes)
register_feature(Upscaling)
register_feature(FrameGeneration)
register_feature(RenderDoc)
register_feature(PerformanceOverlay)
