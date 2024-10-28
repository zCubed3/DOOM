cmake_minimum_required(VERSION 3.29)
project(doom-connector C)

set(CMAKE_C_STANDARD 99)

#
# Build folders
#

# Fallback folder
if (NOT DEFINED DOOMCONN_OUTPUT)
    set(DOOMCONN_OUTPUT "${CMAKE_C_COMPILER_ID}-${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_BUILD_TYPE}")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/${DOOMCONN_OUTPUT}")
set(CMAKE_PDB_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})

#
# Project
#

add_subdirectory(dependencies/SDL EXCLUDE_FROM_ALL)

add_executable(doom-connector main.c)

target_link_libraries(doom-connector PUBLIC ws2_32 SDL3::SDL3)

target_include_directories(doom-connector PUBLIC SDL3)