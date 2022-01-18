cmake_minimum_required(VERSION 3.1)
set(CMAKE_CXX_STANDARD 11)

project(APCpp)

set(BUILD_SHARED_LIBS OFF)
add_subdirectory(IXWebSocket)
include_directories(IXWebSocket)
add_subdirectory(jsoncpp)
include_directories(jsoncpp/include)

add_library(APCpp SHARED Archipelago.cpp Archipelago.h)
target_link_libraries(APCpp jsoncpp ixwebsocket)
