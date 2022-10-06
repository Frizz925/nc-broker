cmake_minimum_required(VERSION 3.22)
project(nc-broker)

if(WIN32 OR MSVC)
  set(CMAKE_FIND_LIBRARY_PREFIXES "lib")
endif()

set(CMAKE_BUILD_TYPE Release)

add_executable(nc-broker main.c)
if(WIN32)
  target_link_libraries(nc-broker ws2_32)
endif()
target_link_libraries(nc-broker uv)
