cmake_minimum_required(VERSION 3.0)

project(MANGOWEBSERVER)

include_directories(${PROJECT_SOURCE_DIR}/include)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")

set(CMAKE_BUILD_TYPE Debug)

# set(SRCLIST "src/httpconn.cpp")

aux_source_directory(${PROJECT_SOURCE_DIR}/src SRCLIST)

list(REMOVE_ITEM SRCLIST ${PROJECT_SOURCE_DIR}/src/fsm.cpp)

add_executable(server main.cpp ${SRCLIST}) # 取出变量用大括号！！！

target_link_libraries(server pthread)