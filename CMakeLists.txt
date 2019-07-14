cmake_minimum_required(VERSION 3.1)
project(GLSLTranspile)

set (CMAKE_CXX_STANDARD 11)

set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/glslang)

set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/SPIRV-Cross)

include_directories(
  include
  deps/SPIRV-Cross
  deps/json
  deps/argh
)

add_executable(
  glsl-transpile
  src/main.cpp
)

target_link_libraries(
  glsl-transpile
	glslang
	glslang-default-resource-limits
	SPIRV
	spirv-cross-core
	spirv-cross-glsl
	spirv-cross-hlsl
	spirv-cross-msl
	spirv-cross-util
	spirv-cross-reflect
	spirv-cross-cpp
 )
