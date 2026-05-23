function(configure_xse_plugin TARGET_NAME MAJOR MINOR PATCH)

	target_compile_features(${TARGET_NAME} PRIVATE cxx_std_23)

	set(PROJECT_NAME ${TARGET_NAME})
	set(PROJECT_VERSION_MAJOR ${MAJOR})
	set(PROJECT_VERSION_MINOR ${MINOR})
	set(PROJECT_VERSION_PATCH ${PATCH})
	set(PROJECT_VERSION "${MAJOR}.${MINOR}.${PATCH}")

	configure_file(
		${CMAKE_SOURCE_DIR}/cmake/Plugin.h.in
		${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}/Plugin.h
		@ONLY
	)

	configure_file(
		${CMAKE_SOURCE_DIR}/cmake/Version.rc.in
		${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}/version.rc
		@ONLY
	)

	target_sources(${TARGET_NAME} PRIVATE
		${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}/Plugin.h
		${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}/version.rc
	)

	target_precompile_headers(${TARGET_NAME} PRIVATE
		${CMAKE_SOURCE_DIR}/include/PCH.h
	)

	target_compile_definitions(${TARGET_NAME} PRIVATE
		_WINDOWS
		_AMD64_
	)

	if(CMAKE_GENERATOR MATCHES "Visual Studio")
		target_compile_definitions(${TARGET_NAME} PRIVATE
			_UNICODE
			"$<$<CONFIG:DEBUG>:DEBUG>"
		)

		set(SC_RELEASE_OPTS "/Zi;/fp:fast;/GL;/Gy-;/Gm-;/Gw;/sdl-;/GS-;/guard:cf-;/O2;/Ob2;/Oi;/Ot;/Oy;/fp:except-")

		target_compile_options(${TARGET_NAME} PRIVATE
			/MP
			/W4
			/WX
			/permissive-
			/utf-8
			/Zc:alignedNew
			/Zc:auto
			/Zc:__cplusplus
			/Zc:externC
			/Zc:externConstexpr
			/Zc:forScope
			/Zc:hiddenFriend
			/Zc:implicitNoexcept
			/Zc:lambda
			/Zc:noexceptTypes
			/Zc:preprocessor
			/Zc:referenceBinding
			/Zc:rvalueCast
			/Zc:sizedDealloc
			/Zc:strictStrings
			/Zc:ternary
			/Zc:threadSafeInit
			/Zc:trigraphs
			/Zc:wchar_t
			/wd4200 # nonstandard extension used : zero-sized array in struct/union
			/arch:AVX
		)

		target_compile_options(${TARGET_NAME} PRIVATE "$<$<CONFIG:DEBUG>:/fp:strict>")
		target_compile_options(${TARGET_NAME} PRIVATE "$<$<CONFIG:DEBUG>:/ZI>")
		target_compile_options(${TARGET_NAME} PRIVATE "$<$<CONFIG:DEBUG>:/Od>")
		target_compile_options(${TARGET_NAME} PRIVATE "$<$<CONFIG:DEBUG>:/Gy>")
		target_compile_options(${TARGET_NAME} PRIVATE "$<$<CONFIG:RELEASE>:${SC_RELEASE_OPTS}>")

		target_link_options(${TARGET_NAME} PRIVATE
			/WX
			"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
			"$<$<CONFIG:RELEASE>:/LTCG;/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
		)
	endif()

	target_include_directories(${TARGET_NAME} PRIVATE
		${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}
		${CMAKE_SOURCE_DIR}/include
		${CMAKE_SOURCE_DIR}/src
	)

	target_link_libraries(${TARGET_NAME} PUBLIC
		CommonLibF4::CommonLibF4
	)

endfunction()

function(register_feature FEATURE_NAME)
	add_subdirectory(features/${FEATURE_NAME})
endfunction()
