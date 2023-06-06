cmake_minimum_required(VERSION 3.12)
project(bc6h-integrator)

include(cmake/CPM.cmake)
include(cmake/shaders.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CXX_STANDARD_REQUIRED ON)

CPMAddPackage("gh:liblava/liblava#0.7.3")
CPMAddPackage("gh:soehrl/cpp-sync#feature/rename-namespace")
CPMAddPackage(NAME colormap GIT_REPOSITORY https://github.com/kbinani/colormap-shaders GIT_TAG master DOWNLOAD_ONLY YES)

add_executable(
  bc6h-integrator
  src/main.cpp
  src/application.hpp src/application.cpp
  src/dataset.hpp src/dataset.cpp
  src/integrator.hpp src/integrator.cpp
  src/data_source.hpp src/data_source.cpp
  src/dataset_view.hpp src/dataset_view.cpp
  src/integration.glsl src/integrate_raw.comp src/integrate_bc6h.comp src/integrate_analytic.comp
  src/dataset_view.vert src/dataset_view.frag
  src/lines.vert src/lines.frag
  src/seeding.comp
  src/analytic_vector_field.glsl
)

target_compile_definitions(
  bc6h-integrator
  PRIVATE NOMINMAX
)

target_link_libraries(
  bc6h-integrator
  PRIVATE lava::engine shaderc sync::sync
)

set(shader_extern_directories ${colormap_SOURCE_DIR}/shaders/glsl)

compile_shaders(bc6h-integrator ${shader_extern_directories})
