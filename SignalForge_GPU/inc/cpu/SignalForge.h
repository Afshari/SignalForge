#pragma once
#include <stdint.h>
#include <vector>

void SHA256HashWrapper_CPU(uint8_t* h_input, uint64_t input_size, uint64_t* h_hash);
void SHA256BatchWrapper_CPU(std::vector<std::vector<uint8_t>>& h_inputs, uint64_t* h_hashes,
    uint32_t num_files, uint32_t  threads_per_block);
void FFTBatchWrapper_CPU(std::vector<std::vector<uint8_t>>& h_inputs, float* h_magnitudes,
    uint32_t num_files, uint32_t fft_size, uint32_t threads_per_block);