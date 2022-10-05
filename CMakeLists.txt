set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_BUILD_TYPE Release)

if(UNIX)
  include(GNUInstallDirs)
endif()

cmake_minimum_required(VERSION 3.22)

project(nc-broker)

add_executable(nc-broker main.c)
target_link_libraries(nc-broker uv)
if(WIN32)
    target_link_libraries(nc-broker ws2_32)
endif()
