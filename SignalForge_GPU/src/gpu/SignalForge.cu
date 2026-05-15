#include "gpu/SignalForge.cuh"

// --------------------------------------------------------------------------------
// SHA256HashWrapper - hash any input size (must be multiple of 64 bytes for now)
// input: d_input, input_size (bytes), output: d_hash (4 x uint64_t, little-endian)
// --------------------------------------------------------------------------------
__global__ void SHA256HashWrapper(uint8_t* d_input, uint64_t input_size, uint64_t* d_hash)
{
    uint32_t s[8];
    uint32_t w[16];

    SHA256Initialize(s);

    // --- Process all complete 64-byte blocks ---
    uint64_t full_blocks = input_size / 64;
    for (uint64_t block = 0; block < full_blocks; block++) {
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)d_input[block * 64 + i * 4] << 24) |
                ((uint32_t)d_input[block * 64 + i * 4 + 1] << 16) |
                ((uint32_t)d_input[block * 64 + i * 4 + 2] << 8) |
                ((uint32_t)d_input[block * 64 + i * 4 + 3]);
        }
        SHA256Transform(s, w);
    }

    // --- Handle partial last block + padding ---
    uint64_t remaining = input_size % 64;
    uint64_t bit_length = input_size * 8;

    // Zero out w
    for (int i = 0; i < 16; i++)
        w[i] = 0;

    // Copy remaining bytes into w
    for (uint64_t i = 0; i < remaining; i++) {
        uint64_t byte_pos = full_blocks * 64 + i;
        int word_idx = i / 4;
        int byte_idx = 3 - (i % 4); // big-endian
        w[word_idx] |= ((uint32_t)d_input[byte_pos] << (byte_idx * 8));
    }

    // Append 0x80 bit
    int word_idx = remaining / 4;
    int byte_idx = 3 - (remaining % 4);
    w[word_idx] |= (0x80u << (byte_idx * 8));

    // If padding + length fits in this block (remaining < 56)
    if (remaining < 56) {
        w[14] = (uint32_t)(bit_length >> 32);
        w[15] = (uint32_t)(bit_length & 0xFFFFFFFF);
        SHA256Transform(s, w);
    }
    else {
        // Need an extra block for the length
        SHA256Transform(s, w);

        for (int i = 0; i < 16; i++)
            w[i] = 0;
        w[14] = (uint32_t)(bit_length >> 32);
        w[15] = (uint32_t)(bit_length & 0xFFFFFFFF);
        SHA256Transform(s, w);
    }

    // --- Output as 4 x uint64_t little-endian limbs ---
    d_hash[0] = ((uint64_t)s[0] << 32) | s[1];
    d_hash[1] = ((uint64_t)s[2] << 32) | s[3];
    d_hash[2] = ((uint64_t)s[4] << 32) | s[5];
    d_hash[3] = ((uint64_t)s[6] << 32) | s[7];
}

// --------------------------------------------------------------------------------
void SHA256HashWrapper_CPU(uint8_t* h_input, uint64_t input_size, uint64_t* h_hash)
{
    uint8_t* d_input;
    uint64_t* d_hash;

    cudaMalloc(&d_input, input_size * sizeof(uint8_t));
    cudaMalloc(&d_hash, 4 * sizeof(uint64_t));

    cudaMemcpy(d_input, h_input, input_size * sizeof(uint8_t), cudaMemcpyHostToDevice);

    SHA256HashWrapper << <1, 1 >> > (d_input, input_size, d_hash);
    cudaDeviceSynchronize();

    cudaMemcpy(h_hash, d_hash, 4 * sizeof(uint64_t), cudaMemcpyDeviceToHost);

    cudaFree(d_input);
    cudaFree(d_hash);
}

// --------------------------------------------------------------------------------
// SHA256BatchWrapper - hash multiple inputs in parallel
// Each thread handles one file
// d_input_flat: all files concatenated into one buffer
// d_offsets: start offset of each file in d_input_flat
// d_sizes: size of each file in bytes
// d_hashes: output array of hashes (4 x uint64_t per file)
// num_files: total number of files
// --------------------------------------------------------------------------------
__global__ void SHA256BatchWrapper(
    uint8_t* d_input_flat,
    uint64_t* d_offsets,
    uint64_t* d_sizes,
    uint64_t* d_hashes,
    uint32_t  num_files)
{
    uint32_t idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= num_files)
        return;

    uint8_t* input = d_input_flat + d_offsets[idx];
    uint64_t  input_size = d_sizes[idx];
    uint64_t* output = d_hashes + idx * 4;

    uint32_t s[8];
    uint32_t w[16];

    SHA256Initialize(s);

    // --- Process all complete 64-byte blocks ---
    uint64_t full_blocks = input_size / 64;
    for (uint64_t block = 0; block < full_blocks; block++) {
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)input[block * 64 + i * 4] << 24) |
                ((uint32_t)input[block * 64 + i * 4 + 1] << 16) |
                ((uint32_t)input[block * 64 + i * 4 + 2] << 8) |
                ((uint32_t)input[block * 64 + i * 4 + 3]);
        }
        SHA256Transform(s, w);
    }

    // --- Handle partial last block + padding ---
    uint64_t remaining = input_size % 64;
    uint64_t bit_length = input_size * 8;

    for (int i = 0; i < 16; i++)
        w[i] = 0;

    for (uint64_t i = 0; i < remaining; i++) {
        uint64_t byte_pos = full_blocks * 64 + i;
        int word_idx = i / 4;
        int byte_idx = 3 - (i % 4);
        w[word_idx] |= ((uint32_t)input[byte_pos] << (byte_idx * 8));
    }

    int word_idx = remaining / 4;
    int byte_idx = 3 - (remaining % 4);
    w[word_idx] |= (0x80u << (byte_idx * 8));

    if (remaining < 56) {
        w[14] = (uint32_t)(bit_length >> 32);
        w[15] = (uint32_t)(bit_length & 0xFFFFFFFF);
        SHA256Transform(s, w);
    }
    else {
        SHA256Transform(s, w);
        for (int i = 0; i < 16; i++)
            w[i] = 0;
        w[14] = (uint32_t)(bit_length >> 32);
        w[15] = (uint32_t)(bit_length & 0xFFFFFFFF);
        SHA256Transform(s, w);
    }

    // --- Output ---
    output[0] = ((uint64_t)s[0] << 32) | s[1];
    output[1] = ((uint64_t)s[2] << 32) | s[3];
    output[2] = ((uint64_t)s[4] << 32) | s[5];
    output[3] = ((uint64_t)s[6] << 32) | s[7];
}

// --------------------------------------------------------------------------------
void SHA256BatchWrapper_CPU(
    std::vector<std::vector<uint8_t>>& h_inputs,
    uint64_t* h_hashes,
    uint32_t num_files,
    uint32_t  threads_per_block)
{
    // --- Build flat buffer, offsets, sizes ---
    std::vector<uint64_t> h_offsets(num_files);
    std::vector<uint64_t> h_sizes(num_files);
    std::vector<uint8_t>  h_flat;

    uint64_t offset = 0;
    for (uint32_t i = 0; i < num_files; i++)
    {
        h_offsets[i] = offset;
        h_sizes[i] = h_inputs[i].size();
        h_flat.insert(h_flat.end(), h_inputs[i].begin(), h_inputs[i].end());
        offset += h_inputs[i].size();
    }

    // --- Allocate GPU memory ---
    uint8_t* d_input_flat;
    uint64_t* d_offsets;
    uint64_t* d_sizes;
    uint64_t* d_hashes;

    cudaMalloc(&d_input_flat, h_flat.size() * sizeof(uint8_t));
    cudaMalloc(&d_offsets, num_files * sizeof(uint64_t));
    cudaMalloc(&d_sizes, num_files * sizeof(uint64_t));
    cudaMalloc(&d_hashes, num_files * 4 * sizeof(uint64_t));

    // --- Copy to GPU ---
    cudaMemcpy(d_input_flat, h_flat.data(), h_flat.size() * sizeof(uint8_t), cudaMemcpyHostToDevice);
    cudaMemcpy(d_offsets, h_offsets.data(), num_files * sizeof(uint64_t), cudaMemcpyHostToDevice);
    cudaMemcpy(d_sizes, h_sizes.data(), num_files * sizeof(uint64_t), cudaMemcpyHostToDevice);

    // --- Launch kernel ---
    int blocks = (num_files + threads_per_block - 1) / threads_per_block;
    SHA256BatchWrapper << <blocks, threads_per_block >> > (
        d_input_flat, d_offsets, d_sizes, d_hashes, num_files);
    cudaDeviceSynchronize();

    // --- Copy results back ---
    cudaMemcpy(h_hashes, d_hashes, num_files * 4 * sizeof(uint64_t), cudaMemcpyDeviceToHost);

    // --- Free GPU memory ---
    cudaFree(d_input_flat);
    cudaFree(d_offsets);
    cudaFree(d_sizes);
    cudaFree(d_hashes);
}

