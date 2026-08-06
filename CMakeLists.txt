cmake_minimum_required(VERSION 3.28)

set(CMAKE_CXX_STANDARD 20)

project(SignalingServer)

option(ADDRESS_SANITIZER "Enable address sanitazer" ON)

if(ADDRESS_SANITIZER)
    add_compile_options("$<$<CONFIG:RelWithDebInfo>:-fno-omit-frame-pointer;-fsanitize=address>")
    add_compile_options("$<$<CONFIG:Debug>:-fno-omit-frame-pointer;-fsanitize=address>")
    add_link_options("$<$<CONFIG:RelWithDebInfo>:-fsanitize=address>")
    add_link_options("$<$<CONFIG:Debug>:-fsanitize=address>")
endif()

if(DEFINED ENV{SNAPCRAFT_PROJECT_NAME})
    add_definitions(-DSNAPCRAFT_BUILD=1)
    set(SNAPCRAFT_BUILD YES)
endif()

set(STREAMING_SUPPORT OFF CACHE BOOL "Only WebRTSP signalling is used" FORCE)
add_subdirectory(WebRTSP)

add_subdirectory(WebMonitor)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/WebRTSP
)

find_package(SQLite3 REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_search_module(SPDLOG REQUIRED spdlog)
pkg_search_module(LIBCONFIG REQUIRED libconfig)
pkg_search_module(LIBNICE REQUIRED nice)


file(GLOB SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    *.cpp
    *.h
    *.conf.sample
    *.cmake
)

file(GLOB_RECURSE SNAP RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    snap/*
)

add_executable(${PROJECT_NAME} ${SOURCES} ${SNAP})
target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/WebRTSP
        ${LIBCONFIG_INCLUDE_DIRS}
        ${SPDLOG_INCLUDE_DIRS}
        ${LIBNICE_INCLUDE_DIRS}
)
target_link_libraries(${PROJECT_NAME}
    ${LIBCONFIG_LIBRARIES}
    ${SPDLOG_LDFLAGS}
    ${LIBNICE_LDFLAGS}
    SQLite::SQLite3
    Signalling
    RtspParser
    Http
)

if(SNAPCRAFT_BUILD)
    install(TARGETS ${PROJECT_NAME} DESTINATION bin)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/signaling-server.conf.sample DESTINATION etc)
endif()
