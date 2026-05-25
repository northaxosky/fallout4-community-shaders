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

add_library(cs_ngx INTERFACE)
target_include_directories(cs_ngx INTERFACE ${NGX_INCLUDE_DIR})
target_link_libraries(cs_ngx INTERFACE ${NGX_LIBS})
