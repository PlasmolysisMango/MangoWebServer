cmake_minimum_required(VERSION 3.0)

project(MANGOWEBSERVER)

include_directories(../include)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")

set(CMAKE_BUILD_TYPE Debug)

set(MAIN test.cpp)

set(SRCLIST "cpp14_threadpool.cpp")

# aux_source_directory(${PROJECT_SOURCE_DIR}/src SRCLIST)

# list(REMOVE_ITEM SRCLIST ${PROJECT_SOURCE_DIR}/src/fsm.cpp)

add_executable(server ${MAIN} ${SRCLIST}) # 取出变量用大括号！！！

target_link_libraries(server pthread)