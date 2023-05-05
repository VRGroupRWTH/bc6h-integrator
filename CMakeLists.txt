cmake_minimum_required(VERSION 3.12)
project(bc6h-integrator)

include(cmake/CPM.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CXX_STANDARD_REQUIRED ON)

CPMAddPackage("gh:liblava/liblava#0.7.3")

add_executable(
  bc6h-integrator
  src/main.cpp
)

target_link_libraries(
  bc6h-integrator
  PRIVATE lava::engine shaderc
)
