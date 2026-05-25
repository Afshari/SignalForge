cmake_minimum_required(VERSION 3.20)
project(SignalForge_CPU)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(Boost_NO_WARN_NEW_VERSIONS 1)
find_package(Boost 1.91 REQUIRED COMPONENTS json)
find_package(hiredis CONFIG REQUIRED)
link_directories(${Boost_LIBRARY_DIRS})

set(CPU_SOURCES
    src/CLParser.cpp
    src/Config.cpp
    src/WavReader.cpp
    src/RedisClient.cpp
)

add_library(SignalForge_CPU STATIC ${CPU_SOURCES})

target_include_directories(SignalForge_CPU PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../SignalForge_GPU/inc
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(SignalForge_CPU PRIVATE
    Boost::json
    hiredis::hiredis
)

add_custom_command(TARGET SignalForge_CPU POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:SignalForge_CPU>
        ${OUTPUT_DIR}/libSignalForge_CPU.a
)