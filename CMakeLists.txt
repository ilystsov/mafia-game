cmake_minimum_required(VERSION 3.15)
project(MafiaGame LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_subdirectory(${CMAKE_SOURCE_DIR}/thirdparty/cppcoro)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(MafiaGame src/main.cpp)

target_link_libraries(MafiaGame PRIVATE pthread cppcoro)
