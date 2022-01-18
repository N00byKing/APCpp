cmake_minimum_required(VERSION 3.4)
set(CMAKE_CXX_STANDARD 11)

project(APCpp)

set(WINDOWS_EXPORT_ALL_SYMBOLS ON)

set(BUILD_SHARED_LIBS OFF)
add_subdirectory(IXWebSocket)
include_directories(IXWebSocket)
add_subdirectory(jsoncpp)
set_property(TARGET jsoncpp_static PROPERTY POSITION_INDEPENDENT_CODE ON)
include_directories(jsoncpp/include)

add_library(APCpp SHARED Archipelago.cpp Archipelago.h)
target_link_libraries(APCpp jsoncpp_static ixwebsocket)
