if(TARGET cs_ngx)
	return()
endif()

set(NGX_ROOT "${CMAKE_SOURCE_DIR}/extern/Streamline/external/ngx-sdk" CACHE PATH "NVIDIA NGX SDK root")
set(NGX_INCLUDE_DIR "${NGX_ROOT}/include" CACHE PATH "NVIDIA NGX SDK include directory")
set(NGX_LIB_DIR "${NGX_ROOT}/lib/Windows_x86_64" CACHE PATH "NVIDIA NGX SDK library directory")
set(NGX_LIBS
	"$<$<CONFIG:Debug>:${NGX_LIB_DIR}/nvsdk_ngx_d_dbg.lib>"
	"$<$<NOT:$<CONFIG:Debug>>:${NGX_LIB_DIR}/nvsdk_ngx_d.lib>"
)

foreach(_ngx_library IN ITEMS
	"${NGX_LIB_DIR}/nvsdk_ngx_d.lib"
	"${NGX_LIB_DIR}/nvsdk_ngx_d_dbg.lib"
)
	set(_ngx_problem)
	if(NOT EXISTS "${_ngx_library}")
		set(_ngx_problem "is missing")
	else()
		file(STRINGS "${_ngx_library}" _ngx_lfs_pointer
			LIMIT_INPUT 256
			LIMIT_COUNT 1
			REGEX "^version https://git-lfs.github.com/spec/v1$"
		)
		if(_ngx_lfs_pointer)
			set(_ngx_problem "is still a Git LFS pointer")
		endif()
	endif()

	if(_ngx_problem)
		message(FATAL_ERROR
			"NVIDIA NGX library ${_ngx_problem}:\n"
			"  ${_ngx_library}\n"
			"The Streamline submodule requires Git LFS. Install it, then run from the repository root:\n"
			"  git lfs install\n"
			"  git submodule update --init --recursive --checkout\n"
			"  git -C extern/Streamline lfs pull"
		)
	endif()
endforeach()

add_library(cs_ngx INTERFACE)
target_include_directories(cs_ngx INTERFACE ${NGX_INCLUDE_DIR})
target_link_libraries(cs_ngx INTERFACE ${NGX_LIBS})
