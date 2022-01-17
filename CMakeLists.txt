cmake_minimum_required(VERSION 3.1)
set(CMAKE_CXX_STANDARD 11)

project(APCpp)

add_subdirectory(IXWebSocket)
add_subdirectory(jsoncpp)

add_library(APCpp STATIC Archipelago.cpp Archipelago.h)
target_link_libraries(APCpp jsoncpp-static ixwebsocket)
