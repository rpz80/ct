cmake_minimum_required(VERSION 3.8)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(withExample "Build example" OFF)

include_directories("src")
add_library(ct "src/ct.c" "src/ct.h")

if(withExample)
  add_executable(example "example/main.c")
  target_link_libraries(example PRIVATE ct)
endif(withExample)
