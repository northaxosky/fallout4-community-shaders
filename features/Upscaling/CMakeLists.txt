file(GLOB_RECURSE FEATURE_SOURCES
	LIST_DIRECTORIES false
	CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

set(XESS_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/.sdk-cache/XeSS-v3.0.2-include")
if(NOT EXISTS "${XESS_INCLUDE_DIR}/xess.h" OR
	NOT EXISTS "${XESS_INCLUDE_DIR}/xefg_swapchain_d3d12.h" OR
	NOT EXISTS "${XESS_INCLUDE_DIR}/xell_d3d12.h")
	message(FATAL_ERROR
		"XeSS 3.0.2 headers are missing. Run scripts/fetch-sdks.ps1 before configuring."
	)
endif()

target_sources(FO4CommunityShaders PRIVATE ${FEATURE_SOURCES})

target_include_directories(FO4CommunityShaders PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${CMAKE_CURRENT_SOURCE_DIR}/src/RCAS
	${XESS_INCLUDE_DIR}
)

# FSR3 dispatch uses structured exception handling, which is incompatible with /EHsc objects.
set_source_files_properties(
	"${CMAKE_CURRENT_SOURCE_DIR}/src/FidelityFX.cpp"
	PROPERTIES COMPILE_OPTIONS "/EHa"
)

target_link_libraries(FO4CommunityShaders PRIVATE Streamline)
target_link_libraries(FO4CommunityShaders PRIVATE d3d12.lib dxgi.lib)
