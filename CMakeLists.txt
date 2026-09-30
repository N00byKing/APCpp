cmake_minimum_required(VERSION 3.24)

project(APCpp)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(APCPP_SOURCES
    Archipelago.cpp
    Archipelago_DataStorageSim.cpp
    Archipelago_Gifting.cpp)

# Dependencies
add_library(APCpp_deps INTERFACE)

# Public header def
add_library(APCpp_headers INTERFACE)
target_include_directories(APCpp_headers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)

# Compile once, create static and dynamic lib
add_library(APCpp_objects OBJECT ${APCPP_SOURCES})
target_link_libraries(APCpp_objects
    PUBLIC APCpp_headers
    PRIVATE APCpp_deps)
add_library(APCpp SHARED)
target_link_libraries(APCpp
    PUBLIC APCpp_headers
    PRIVATE APCpp_objects APCpp_deps)
add_library(APCpp_static STATIC)
target_link_libraries(APCpp_static
    PUBLIC APCpp_headers APCpp_deps
    PRIVATE APCpp_objects)

add_library(APCpp::APCpp ALIAS APCpp)
add_library(APCpp::APCpp_static ALIAS APCpp_static)

set_target_properties(APCpp PROPERTIES PUBLIC_HEADER Archipelago.h)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)

# Enable TLS. On Windows, build mbedTLS
set(USE_TLS ON CACHE BOOL "Enable TLS support" FORCE)

# Fallback to mbedtls if openssl not found
if (NOT MINGW)
    find_package(OpenSSL)
    if (NOT OPENSSL_FOUND)
        set(USE_MBED_TLS ON)
    endif(NOT OPENSSL_FOUND)
endif (NOT MINGW)

if ((WIN32 AND USE_TLS) OR USE_MBED_TLS)
    set(APCPP_MBEDTLS_VER 3.6.4)
    execute_process(COMMAND ${CMAKE_COMMAND} -E remove -f mbedtls-${APCPP_MBEDTLS_VER})
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E tar xzf ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls-${APCPP_MBEDTLS_VER}.tar.bz2
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
    set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
    set(MBEDTLS_INCLUDE_DIRS ${CMAKE_BINARY_DIR}/mbedtls-${APCPP_MBEDTLS_VER}/include)
    set(MBEDTLS_VERSION_GREATER_THAN_3 ${MBEDTLS_INCLUDE_DIRS})
    set(MBEDTLS_LIBRARY mbedtls)
    set(MBEDX509_LIBRARY mbedx509)
    set(MBEDCRYPTO_LIBRARY mbedcrypto)
    add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/mbedtls-${APCPP_MBEDTLS_VER} mbedtls_bin)
endif ((WIN32 AND USE_TLS) OR USE_MBED_TLS)

# Attempt finding system version of zlib
if (NOT MINGW)
    find_package(ZLIB QUIET)
    if (ZLIB_FOUND)
        message("Using system zlib installation.")
    else()
        message("Using bundled zlib.")
    endif(ZLIB_FOUND)
endif (NOT MINGW)
if (MINGW OR NOT ZLIB_FOUND)
    # Force static build for zlib if not in system, and alias static to shared for IXWebsocket
    set(ZLIB_BUILD_SHARED OFF)
    include(FetchContent)
    FetchContent_Declare(
        zlib
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/zlib"
        OVERRIDE_FIND_PACKAGE
    )
    FetchContent_MakeAvailable(zlib)
    add_library(ZLIB::ZLIB ALIAS zlibstatic)
endif (MINGW OR NOT ZLIB_FOUND)

set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(IXWebSocket)
target_link_libraries(APCpp_deps INTERFACE ixwebsocket)

# MinGW shared lib uses static deps
if (MINGW)
    target_link_options(APCpp PRIVATE -static -static-libstdc++ -static-libgcc)
endif(MINGW)

# Attempt finding system version of JsonCpp
find_package(PkgConfig)
if (PKGCONFIG_FOUND AND (NOT MINGW) AND (NOT BUNDLED_JSONCPP))
    pkg_check_modules(JSONCPP IMPORTED_TARGET jsoncpp)
    if (JSONCPP_FOUND)
        target_link_libraries(APCpp_deps INTERFACE PkgConfig::JSONCPP)
    endif(JSONCPP_FOUND)
endif(PKGCONFIG_FOUND AND (NOT MINGW) AND (NOT BUNDLED_JSONCPP))

# No system version: Enable bundled
if((NOT JSONCPP_FOUND) OR BUNDLED_JSONCPP)
    set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
    set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(jsoncpp)
    set_property(TARGET jsoncpp_static PROPERTY POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(APCpp_deps INTERFACE jsoncpp_static)
endif((NOT JSONCPP_FOUND) OR BUNDLED_JSONCPP)

if (WIN32)
    target_link_libraries(APCpp_deps INTERFACE ws2_32 crypt32 bcrypt)
endif(WIN32)

install(TARGETS APCpp APCpp_static PUBLIC_HEADER)
