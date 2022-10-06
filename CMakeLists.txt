cmake_minimum_required(VERSION 3.22)
project(nc-broker)

set(CMAKE_BUILD_TYPE Release)

if (WIN32 OR MSVC)
  set(CMAKE_FIND_LIBRARY_PREFIXES "lib")
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".lib")
elseif (UNIX)
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
endif()
find_library(UV_LIBRARY uv REQUIRED)

add_executable(nc-broker main.c)
target_link_libraries(nc-broker ${UV_LIBRARY})
if(WIN32)
  target_link_libraries(nc-broker ws2_32)
endif()
