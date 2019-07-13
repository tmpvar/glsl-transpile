cmake_minimum_required(VERSION 3.0)
project(GLSLTranspile)

set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/glslang)

include_directories(
  include
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
 )
