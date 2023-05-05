cmake_minimum_required(VERSION 3.12)
project(bc6h-integrator)

include(cmake/CPM.cmake)
include(cmake/shaders.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CXX_STANDARD_REQUIRED ON)

CPMAddPackage("gh:liblava/liblava#0.7.3")

add_executable(
  bc6h-integrator
  src/main.cpp
  src/dataset.hpp src/dataset.cpp
  src/integrator.hpp src/integrator.cpp
  src/dataset_view.hpp src/dataset_view.cpp
  src/compute.comp
)

target_link_libraries(
  bc6h-integrator
  PRIVATE lava::engine shaderc
)
