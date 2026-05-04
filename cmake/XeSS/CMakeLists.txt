cmake_minimum_required(VERSION 3.21)

if(TARGET XeSS)
  return()
endif()

add_library(XeSS INTERFACE)

target_include_directories(
  XeSS INTERFACE
  ${CMAKE_SOURCE_DIR}/extern/XeSS/inc
  ${CMAKE_SOURCE_DIR}/extern/XeSS/inc/xess_fg
  ${CMAKE_SOURCE_DIR}/extern/XeSS/inc/xell
)
