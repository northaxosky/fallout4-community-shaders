function(ensure_fidelityfx_sdk)
	if(TARGET ffx_fsr3_x64)
		return()
	endif()

	set(FFX_API_VK OFF CACHE BOOL "" FORCE)
	set(FFX_API_DX12 OFF CACHE BOOL "" FORCE)
	set(FFX_ALL OFF CACHE BOOL "" FORCE)
	set(FFX_FSR3 ON CACHE BOOL "" FORCE)
	set(FFX_FSR ON CACHE BOOL "" FORCE)
	set(FFX_AUTO_COMPILE_SHADERS 1 CACHE BOOL "" FORCE)

	file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/ffx_sdk")
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
endfunction()
