cmake_minimum_required(VERSION 3.0)

project(MANGOWEBSERVER)

include_directories(${PROJECT_SOURCE_DIR}/include)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")

set(CMAKE_BUILD_TYPE Debug)

enable_testing()

add_subdirectory(test)

add_subdirectory(src)