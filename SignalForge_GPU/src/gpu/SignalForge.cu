#include "gpu/SignalForge.cuh"
#include <cufft.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>


/*
 * sha256.cu Implementation of SHA256 Hashing
 *
 * Date: 12 June 2019
 * Revision: 1
 *
 * Based on the public domain Reference Implementation in C, by
 * Brad Conte, original code here:
 *
 * https://github.com/B-Con/crypto-algorithms
 *
 * This file is released into the Public Domain.
 */
#include <stdlib.h>
#include <memory.h>
#include "gpu/sha256.cuh"

#define SHA256_BLOCK_SIZE 32
#define NUM_STREAMS 8

typedef struct {
    BYTE data[64];
    WORD datalen;
    unsigned long long bitlen;
    WORD state[8];
} CUDA_SHA256_CTX;

#ifndef ROTLEFT
#define ROTLEFT(a,b) (((a) << (b)) | ((a) >> (32-(b))))
#endif
#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))
#define CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2)  ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6)  ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

__constant__ WORD k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

__device__ __forceinline__ void cuda_sha256_transform(CUDA_SHA256_CTX* ctx, const BYTE data[])
{
    WORD a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];
    for (i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = (data[j] << 24) | (data[j+1] << 16) | (data[j+2] << 8) | (data[j+3]);
    for (; i < 64; ++i)
        m[i] = SIG1(m[i-2]) + m[i-7] + SIG0(m[i-15]) + m[i-16];
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
    for (i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e,f,g) + k[i] + m[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

__device__ void cuda_sha256_init(CUDA_SHA256_CTX* ctx)
{
    ctx->datalen = 0; ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
}

__device__ void cuda_sha256_update(CUDA_SHA256_CTX* ctx, const BYTE data[], size_t len)
{
    WORD i;
    for (i = 0; i < len; ++i) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            cuda_sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

__device__ void cuda_sha256_final(CUDA_SHA256_CTX* ctx, BYTE hash[])
{
    WORD i = ctx->datalen;
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) ctx->data[i++] = 0x00;
        cuda_sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }
    ctx->bitlen += ctx->datalen * 8;
    ctx->data[63] = ctx->bitlen;       ctx->data[62] = ctx->bitlen >> 8;
    ctx->data[61] = ctx->bitlen >> 16; ctx->data[60] = ctx->bitlen >> 24;
    ctx->data[59] = ctx->bitlen >> 32; ctx->data[58] = ctx->bitlen >> 40;
    ctx->data[57] = ctx->bitlen >> 48; ctx->data[56] = ctx->bitlen >> 56;
    cuda_sha256_transform(ctx, ctx->data);
    for (i = 0; i < 4; ++i) {
        hash[i]    = (ctx->state[0] >> (24 - i*8)) & 0xff;
        hash[i+4]  = (ctx->state[1] >> (24 - i*8)) & 0xff;
        hash[i+8]  = (ctx->state[2] >> (24 - i*8)) & 0xff;
        hash[i+12] = (ctx->state[3] >> (24 - i*8)) & 0xff;
        hash[i+16] = (ctx->state[4] >> (24 - i*8)) & 0xff;
        hash[i+20] = (ctx->state[5] >> (24 - i*8)) & 0xff;
        hash[i+24] = (ctx->state[6] >> (24 - i*8)) & 0xff;
        hash[i+28] = (ctx->state[7] >> (24 - i*8)) & 0xff;
    }
}

__global__ void kernel_sha256_hash(BYTE* indata, WORD inlen, BYTE* outdata, WORD n_batch)
{
    WORD thread = blockIdx.x * blockDim.x + threadIdx.x;
    if (thread >= n_batch) return;
    BYTE* in  = indata  + thread * inlen;
    BYTE* out = outdata + thread * SHA256_BLOCK_SIZE;
    CUDA_SHA256_CTX ctx;
    cuda_sha256_init(&ctx);
    cuda_sha256_update(&ctx, in, inlen);
    cuda_sha256_final(&ctx, out);
}

// --------------------------------------------------------------------------------
// Streamed batch hash - overlaps H2D transfer with kernel execution
// using NUM_STREAMS concurrent CUDA streams
// --------------------------------------------------------------------------------
static void mcm_cuda_sha256_hash_batch_streamed(
    BYTE*  in,
    WORD   inlen,
    BYTE*  out,
    WORD   n_batch,
    WORD   threads_per_block)
{
    // Total sizes
    size_t total_in  = (size_t)inlen * n_batch;
    size_t total_out = (size_t)SHA256_BLOCK_SIZE * n_batch;

    // Allocate pinned host memory for async transfers
    BYTE* pinned_in  = nullptr;
    BYTE* pinned_out = nullptr;
    cudaMallocHost(&pinned_in,  total_in);
    cudaMallocHost(&pinned_out, total_out);
    memcpy(pinned_in, in, total_in);

    // Allocate device memory
    BYTE* d_in  = nullptr;
    BYTE* d_out = nullptr;
    cudaMalloc(&d_in,  total_in);
    cudaMalloc(&d_out, total_out);

    // Create streams
    cudaStream_t streams[NUM_STREAMS];
    for (int s = 0; s < NUM_STREAMS; s++)
        cudaStreamCreate(&streams[s]);

    // Split work across streams
    WORD chunk = (n_batch + NUM_STREAMS - 1) / NUM_STREAMS;

    for (int s = 0; s < NUM_STREAMS; s++)
    {
        WORD offset = s * chunk;
        if (offset >= n_batch) break;

        WORD count = std::min(chunk, n_batch - offset);

        size_t in_offset  = (size_t)offset * inlen;
        size_t out_offset = (size_t)offset * SHA256_BLOCK_SIZE;

        // Async H2D
        cudaMemcpyAsync(
            d_in + in_offset,
            pinned_in + in_offset,
            (size_t)count * inlen,
            cudaMemcpyHostToDevice,
            streams[s]);

        // Kernel launch on stream
        WORD blocks = (count + threads_per_block - 1) / threads_per_block;
        kernel_sha256_hash<<<blocks, threads_per_block, 0, streams[s]>>>(
            d_in  + in_offset,
            inlen,
            d_out + out_offset,
            count);

        // Async D2H
        cudaMemcpyAsync(
            pinned_out + out_offset,
            d_out + out_offset,
            (size_t)count * SHA256_BLOCK_SIZE,
            cudaMemcpyDeviceToHost,
            streams[s]);
    }

    // Sync all streams
    for (int s = 0; s < NUM_STREAMS; s++)
        cudaStreamSynchronize(streams[s]);

    // Copy result to output
    memcpy(out, pinned_out, total_out);

    // Cleanup
    for (int s = 0; s < NUM_STREAMS; s++)
        cudaStreamDestroy(streams[s]);

    cudaFree(d_in);
    cudaFree(d_out);
    cudaFreeHost(pinned_in);
    cudaFreeHost(pinned_out);

    cudaError_t error = cudaGetLastError();
    if (error != cudaSuccess)
        printf("Error cuda sha256 streamed: %s\n", cudaGetErrorString(error));
}

// --------------------------------------------------------------------------------
// Helper: convert 32-byte BYTE hash to 4 x uint64_t
// --------------------------------------------------------------------------------
static void ByteHashToUint64(const BYTE* hash32, uint64_t* out4)
{
    for (int i = 0; i < 4; i++)
    {
        out4[i] =
            ((uint64_t)hash32[i*8+0] << 56) | ((uint64_t)hash32[i*8+1] << 48) |
            ((uint64_t)hash32[i*8+2] << 40) | ((uint64_t)hash32[i*8+3] << 32) |
            ((uint64_t)hash32[i*8+4] << 24) | ((uint64_t)hash32[i*8+5] << 16) |
            ((uint64_t)hash32[i*8+6] <<  8) | ((uint64_t)hash32[i*8+7]);
    }
}

// --------------------------------------------------------------------------------
// SHA256HashWrapper_CPU - single file
// --------------------------------------------------------------------------------
void SHA256HashWrapper_CPU(uint8_t* h_input, uint64_t input_size, uint64_t* h_hash)
{
    BYTE out[32] = { 0 };
    mcm_cuda_sha256_hash_batch_streamed(
        h_input,
        static_cast<WORD>(input_size),
        out,
        1,
        256);
    ByteHashToUint64(out, h_hash);
}

// --------------------------------------------------------------------------------
// SHA256BatchWrapper_CPU - batch of files
// --------------------------------------------------------------------------------
void SHA256BatchWrapper_CPU(
    std::vector<std::vector<uint8_t>>& h_inputs,
    uint64_t* h_hashes,
    uint32_t                           num_files,
    uint32_t                           threads_per_block)
{
    if (num_files == 0) return;

    uint64_t fileSize = h_inputs[0].size();

    // Allocate pinned host buffer and device buffers
    BYTE* pinned_in = nullptr;
    BYTE* pinned_out = nullptr;
    BYTE* d_in = nullptr;
    BYTE* d_out = nullptr;

    size_t total_in = fileSize * num_files;
    size_t total_out = SHA256_BLOCK_SIZE * num_files;

    cudaMallocHost(&pinned_in, total_in);
    cudaMallocHost(&pinned_out, total_out);
    cudaMalloc(&d_in, total_in);
    cudaMalloc(&d_out, total_out);

    // Copy each file directly into pinned buffer — no intermediate flat vector
    for (uint32_t i = 0; i < num_files; i++)
        std::memcpy(pinned_in + i * fileSize, h_inputs[i].data(), fileSize);

    // Create streams
    cudaStream_t streams[NUM_STREAMS];
    for (int s = 0; s < NUM_STREAMS; s++)
        cudaStreamCreate(&streams[s]);

    // Split across streams
    uint32_t chunk = (num_files + NUM_STREAMS - 1) / NUM_STREAMS;

    for (int s = 0; s < NUM_STREAMS; s++)
    {
        uint32_t offset = s * chunk;
        if (offset >= num_files) break;

        uint32_t count = std::min(chunk, num_files - offset);

        size_t in_offset = (size_t)offset * fileSize;
        size_t out_offset = (size_t)offset * SHA256_BLOCK_SIZE;

        cudaMemcpyAsync(
            d_in + in_offset,
            pinned_in + in_offset,
            (size_t)count * fileSize,
            cudaMemcpyHostToDevice,
            streams[s]);

        uint32_t blocks = (count + threads_per_block - 1) / threads_per_block;
        kernel_sha256_hash << <blocks, threads_per_block, 0, streams[s] >> > (
            d_in + in_offset,
            static_cast<WORD>(fileSize),
            d_out + out_offset,
            count);

        cudaMemcpyAsync(
            pinned_out + out_offset,
            d_out + out_offset,
            (size_t)count * SHA256_BLOCK_SIZE,
            cudaMemcpyDeviceToHost,
            streams[s]);
    }

    for (int s = 0; s < NUM_STREAMS; s++)
        cudaStreamSynchronize(streams[s]);

    // Convert output
    for (uint32_t i = 0; i < num_files; i++)
        ByteHashToUint64(pinned_out + i * SHA256_BLOCK_SIZE, h_hashes + i * 4);

    // Cleanup
    for (int s = 0; s < NUM_STREAMS; s++)
        cudaStreamDestroy(streams[s]);

    cudaFreeHost(pinned_in);
    cudaFreeHost(pinned_out);
    cudaFree(d_in);
    cudaFree(d_out);
}

// --------------------------------------------------------------------------------
// ComputeMagnitudes kernel
// --------------------------------------------------------------------------------
__global__ void ComputeMagnitudes(
    cufftComplex* d_freq,
    float*        d_magnitudes,
    uint32_t      num_files,
    uint32_t      fft_size)
{
    uint32_t file_idx = blockIdx.x;
    uint32_t bin_idx  = blockIdx.y * blockDim.x + threadIdx.x;
    uint32_t half     = fft_size / 2 + 1;
    if (file_idx >= num_files || bin_idx >= half) return;
    cufftComplex c = d_freq[(uint64_t)file_idx * half + bin_idx];
    d_magnitudes[(uint64_t)file_idx * half + bin_idx] =
        sqrtf(c.x * c.x + c.y * c.y) / fft_size;
}

// --------------------------------------------------------------------------------
// FFTBatchWrapper_CPU
// --------------------------------------------------------------------------------
void FFTBatchWrapper_CPU(
    std::vector<std::vector<uint8_t>>& h_inputs,
    float*    h_magnitudes,
    uint32_t  num_files,
    uint32_t  fft_size,
    uint32_t  threads_per_block)
{
    uint64_t half = fft_size / 2 + 1;

    float*        d_float_flat;
    cufftComplex* d_freq;
    float*        d_magnitudes;

    cudaMalloc(&d_float_flat,  (uint64_t)num_files * fft_size * sizeof(float));
    cudaMalloc(&d_freq,        (uint64_t)num_files * half * sizeof(cufftComplex));
    cudaMalloc(&d_magnitudes,  (uint64_t)num_files * half * sizeof(float));
    cudaMemset(d_float_flat, 0,(uint64_t)num_files * fft_size * sizeof(float));

    for (uint32_t i = 0; i < num_files; i++)
    {
        const auto& pcm = h_inputs[i];
        uint32_t num_samples = std::min((uint32_t)(pcm.size() / 2), fft_size);
        std::vector<float> h_float(fft_size, 0.0f);
        for (uint32_t s = 0; s < num_samples; s++)
        {
            int16_t sample = (int16_t)(pcm[s*2] | (pcm[s*2+1] << 8));
            h_float[s] = sample / 32768.0f;
        }
        cudaMemcpy(
            d_float_flat + (uint64_t)i * fft_size,
            h_float.data(),
            fft_size * sizeof(float),
            cudaMemcpyHostToDevice);
    }

    cufftHandle plan;
    cufftPlan1d(&plan, fft_size, CUFFT_R2C, num_files);
    cufftExecR2C(plan, d_float_flat, d_freq);
    cudaDeviceSynchronize();
    cufftDestroy(plan);

    dim3 grid_mag(num_files, (half + threads_per_block - 1) / threads_per_block);
    ComputeMagnitudes<<<grid_mag, threads_per_block>>>(
        d_freq, d_magnitudes, num_files, fft_size);
    cudaDeviceSynchronize();

    cudaMemcpy(h_magnitudes, d_magnitudes,
        (uint64_t)num_files * half * sizeof(float),
        cudaMemcpyDeviceToHost);

    cudaFree(d_float_flat);
    cudaFree(d_freq);
    cudaFree(d_magnitudes);
}
