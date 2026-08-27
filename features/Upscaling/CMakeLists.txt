file(GLOB_RECURSE FEATURE_SOURCES
	LIST_DIRECTORIES false
	CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

target_sources(FO4CommunityShaders PRIVATE ${FEATURE_SOURCES})

target_include_directories(FO4CommunityShaders PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${CMAKE_CURRENT_SOURCE_DIR}/src/RCAS
)

# FSR3 dispatch uses structured exception handling, which is incompatible with /EHsc objects.
set_source_files_properties(
	"${CMAKE_CURRENT_SOURCE_DIR}/src/FidelityFX.cpp"
	PROPERTIES COMPILE_OPTIONS "/EHa"
)

target_link_libraries(FO4CommunityShaders PRIVATE Streamline)
target_link_libraries(FO4CommunityShaders PRIVATE d3d12.lib dxgi.lib)
