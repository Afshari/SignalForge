cmake_minimum_required(VERSION 3.20)
project(SignalForge_GPU CUDA CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 86)
endif()

find_package(CUDAToolkit REQUIRED)

set(GPU_SOURCES
    src/gpu/SignalForge.cu
)

add_library(SignalForge_GPU STATIC ${GPU_SOURCES})

set_target_properties(SignalForge_GPU PROPERTIES
    CUDA_SEPARABLE_COMPILATION OFF
)

target_include_directories(SignalForge_GPU PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_link_libraries(SignalForge_GPU PRIVATE
    CUDA::cudart_static
)

add_custom_command(TARGET SignalForge_GPU POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_FILE:SignalForge_GPU>
        ${OUTPUT_DIR}/libSignalForge_GPU.a
)