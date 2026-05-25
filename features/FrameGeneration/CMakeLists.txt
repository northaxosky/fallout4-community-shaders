file(GLOB_RECURSE FEATURE_SOURCES
	LIST_DIRECTORIES false
	CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

target_sources(FO4CommunityShaders PRIVATE ${FEATURE_SOURCES})

target_include_directories(FO4CommunityShaders PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(FO4CommunityShaders PRIVATE
	Streamline
	XeSS
	cs_ngx
)

ensure_fidelityfx_sdk()

target_link_libraries(FO4CommunityShaders PRIVATE
	ffx_backend_dx11_x64
	ffx_fsr3_x64
)
