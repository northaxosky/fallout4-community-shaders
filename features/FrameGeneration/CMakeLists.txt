file(GLOB_RECURSE FEATURE_SOURCES
	LIST_DIRECTORIES false
	CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

target_sources(FO4CommunityShaders PRIVATE ${FEATURE_SOURCES})

target_include_directories(FO4CommunityShaders PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${CMAKE_SOURCE_DIR}/extern/Streamline/external/ngx-sdk/include
)

target_link_libraries(FO4CommunityShaders PRIVATE
	Streamline
	XeSS
	${CMAKE_SOURCE_DIR}/extern/Streamline/external/ngx-sdk/lib/Windows_x86_64/nvsdk_ngx_d.lib
)

# FidelityFX-SDK (FSR3) is shared with the Upscaling feature. Upscaling's CMakeLists adds the
# subdirectory first when both features are built; the guard prevents double-add when FrameGeneration
# is built standalone.
if(NOT TARGET ffx_fsr3_x64)
	set(FFX_API_VK OFF CACHE BOOL "" FORCE)
	set(FFX_API_DX12 OFF CACHE BOOL "" FORCE)
	set(FFX_ALL OFF CACHE BOOL "" FORCE)
	set(FFX_FSR3 ON CACHE BOOL "" FORCE)
	set(FFX_FSR ON CACHE BOOL "" FORCE)
	set(FFX_AUTO_COMPILE_SHADERS 1)
	add_subdirectory(${CMAKE_SOURCE_DIR}/extern/FidelityFX-SDK/sdk ${CMAKE_BINARY_DIR}/FidelityFX-SDK)

	foreach(FFX_TARGET
			ffx_backend_dx11_x64
			ffx_frameinterpolation_x64
			ffx_fsr3_x64
			ffx_fsr3upscaler_x64
			ffx_opticalflow_x64
			ffx_spd_x64)
		if(TARGET ${FFX_TARGET})
			target_compile_options(${FFX_TARGET} PRIVATE /WX-)
		endif()
	endforeach()
endif()

target_link_libraries(FO4CommunityShaders PRIVATE
	ffx_backend_dx11_x64
	ffx_fsr3_x64
)
