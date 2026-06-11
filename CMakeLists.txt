cmake_minimum_required(VERSION 3.20)
project(SignalForge_Solution)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 86)
endif()

# Output directory — works on both Windows (multi-config) and Linux (single-config)
if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set(OUTPUT_DIR ${CMAKE_SOURCE_DIR}/x64/$<CONFIG>)
else()
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release)
    endif()
    set(OUTPUT_DIR ${CMAKE_SOURCE_DIR}/x64/${CMAKE_BUILD_TYPE})
endif()

message(STATUS "Output directory: ${OUTPUT_DIR}")
message(STATUS "CUDA architectures: ${CMAKE_CUDA_ARCHITECTURES}")

find_package(hiredis CONFIG REQUIRED)

find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)

# Boost — installed from source at /usr/local on Linux
# On Windows pass -DBOOST_ROOT=... or set BOOST_ROOT env var
set(Boost_NO_WARN_NEW_VERSIONS 1)
find_package(Boost 1.91 REQUIRED COMPONENTS json)
link_directories(${Boost_LIBRARY_DIRS})
message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")

# Sub-projects
add_subdirectory(SignalForge_GPU)
add_subdirectory(SignalForge_CPU)
add_subdirectory(SignalForge)
add_subdirectory(SignalForge_Tests)

add_dependencies(SignalForge      SignalForge_CPU SignalForge_GPU)
add_dependencies(SignalForge_Tests SignalForge_CPU SignalForge_GPU)

# Copy config.json to output directories
if(CMAKE_GENERATOR MATCHES "Visual Studio")
    foreach(CONFIG Debug Release)
        file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/x64/${CONFIG})
        configure_file(
            ${CMAKE_SOURCE_DIR}/config.json
            ${CMAKE_SOURCE_DIR}/x64/${CONFIG}/config.json
            COPYONLY
        )
    endforeach()
else()
    file(MAKE_DIRECTORY ${OUTPUT_DIR})
    configure_file(
        ${CMAKE_SOURCE_DIR}/config.json
        ${OUTPUT_DIR}/config.json
        COPYONLY
    )
endif()
