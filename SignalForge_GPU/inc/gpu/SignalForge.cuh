#pragma once
#include <cuda.h>
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>
#include "gpu/GPUHash.h"
#include <cufft.h>


__global__ void ComputeMagnitudes(
    cufftComplex* d_freq,
    float* d_magnitudes,
    uint32_t      num_files,
    uint32_t      fft_size);