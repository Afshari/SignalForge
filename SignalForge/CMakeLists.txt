cmake_minimum_required(VERSION 3.20)
project(SignalForge)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CUDAToolkit REQUIRED)
find_package(xxHash CONFIG REQUIRED)

set(Boost_NO_WARN_NEW_VERSIONS 1)
find_package(Boost 1.91 REQUIRED COMPONENTS json)
link_directories(${Boost_LIBRARY_DIRS})

find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf REQUIRED)

set(SOURCES
    src/AppRunner.cpp
    main.cpp
)

add_executable(SignalForge ${SOURCES})

target_include_directories(SignalForge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../SignalForge_CPU/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../SignalForge_GPU/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/../SignalForge_Proto
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(SignalForge PRIVATE
    ${OUTPUT_DIR}/libSignalForge_CPU.a
    ${OUTPUT_DIR}/libSignalForge_GPU.a
    CUDA::cudart_static
    CUDA::cufft
    Boost::json
    hiredis::hiredis
    gRPC::grpc++
    protobuf::libprotobuf
    xxHash::xxhash
)

add_custom_command(TARGET SignalForge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:SignalForge>
        ${OUTPUT_DIR}/SignalForge
)