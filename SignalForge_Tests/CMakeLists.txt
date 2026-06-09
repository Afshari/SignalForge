cmake_minimum_required(VERSION 3.20)
project(SignalForge_Tests)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CUDAToolkit REQUIRED)

set(Boost_NO_WARN_NEW_VERSIONS 1)
find_package(Boost 1.91 REQUIRED COMPONENTS json)
link_directories(${Boost_LIBRARY_DIRS})

find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf REQUIRED)

# GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(TEST_SOURCES
    CLParserTests.cpp
    ConfigTests.cpp
    SHA256Tests.cpp
    WavReaderTests.cpp
    FFTTests.cpp
    RedisClientTests.cpp
    PipelineTests.cpp
    GrpcTests.cpp
    UtilsTests.cpp
)

add_executable(SignalForge_Tests ${TEST_SOURCES})

target_include_directories(SignalForge_Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../SignalForge_CPU/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../SignalForge_GPU/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../SignalForge_Proto
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(SignalForge_Tests PRIVATE
    ${OUTPUT_DIR}/libSignalForge_CPU.a
    ${OUTPUT_DIR}/libSignalForge_GPU.a
    CUDA::cudart_static
    CUDA::cufft
    Boost::json
    hiredis::hiredis
    gtest
    gtest_main
    gRPC::grpc++
    protobuf::libprotobuf
)

# Copy config.json and test_data next to test executable
add_custom_command(TARGET SignalForge_Tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:SignalForge_Tests>
        ${OUTPUT_DIR}/SignalForge_Tests
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/config.json
        ${OUTPUT_DIR}/config.json
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/test_data
        ${OUTPUT_DIR}/test_data
)