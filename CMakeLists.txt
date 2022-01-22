cmake_minimum_required(VERSION 3.4)

project(APCpp)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

add_library(APCpp SHARED Archipelago.cpp Archipelago.h)

set(BUILD_SHARED_LIBS OFF)

add_subdirectory(IXWebSocket)
include_directories(IXWebSocket)
target_link_libraries(APCpp ixwebsocket)

if (MINGW)
    target_link_libraries(APCpp -static -static-libstdc++ -static-libgcc)
endif(MINGW)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP jsoncpp)
if ((NOT JSONCPP_FOUND) OR MINGW)
    add_subdirectory(jsoncpp)
    set_property(TARGET jsoncpp_static PROPERTY POSITION_INDEPENDENT_CODE ON)
    include_directories(jsoncpp/include)
    target_link_libraries(APCpp jsoncpp_static)
else()
    include_directories(${JSONCPP_INCLUDE_DIRS})
    target_link_libraries(APCpp ${JSONCPP_LDFLAGS})
endif((NOT JSONCPP_FOUND) OR MINGW)

if (WIN32)
    target_link_libraries(APCpp ws2_32)
endif(WIN32)