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
 * *
 * Based on the public domain Reference Implementation in C, by
 * Brad Conte, original code here:
 *
 * https://github.com/B-Con/crypto-algorithms
 *
 * This file is released into the Public Domain.
 */
 /*************************** HEADER FILES ***************************/
#include <stdlib.h>
#include <memory.h>
#include "gpu/sha256.cuh"
/****************************** MACROS ******************************/
#define SHA256_BLOCK_SIZE 32            // SHA256 outputs a 32 byte digest

/**************************** DATA TYPES ****************************/

typedef struct {
	BYTE data[64];
	WORD datalen;
	unsigned long long bitlen;
	WORD state[8];
} CUDA_SHA256_CTX;

/****************************** MACROS ******************************/
#ifndef ROTLEFT
#define ROTLEFT(a,b) (((a) << (b)) | ((a) >> (32-(b))))
#endif

#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))

#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

/**************************** VARIABLES *****************************/
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

/*********************** FUNCTION DEFINITIONS ***********************/
__device__  __forceinline__ void cuda_sha256_transform(CUDA_SHA256_CTX* ctx, const BYTE data[])
{
	WORD a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];

	for (i = 0, j = 0; i < 16; ++i, j += 4)
		m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);
	for (; i < 64; ++i)
		m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];

	a = ctx->state[0];
	b = ctx->state[1];
	c = ctx->state[2];
	d = ctx->state[3];
	e = ctx->state[4];
	f = ctx->state[5];
	g = ctx->state[6];
	h = ctx->state[7];

	for (i = 0; i < 64; ++i) {
		t1 = h + EP1(e) + CH(e, f, g) + k[i] + m[i];
		t2 = EP0(a) + MAJ(a, b, c);
		h = g;
		g = f;
		f = e;
		e = d + t1;
		d = c;
		c = b;
		b = a;
		a = t1 + t2;
	}

	ctx->state[0] += a;
	ctx->state[1] += b;
	ctx->state[2] += c;
	ctx->state[3] += d;
	ctx->state[4] += e;
	ctx->state[5] += f;
	ctx->state[6] += g;
	ctx->state[7] += h;
}

__device__ void cuda_sha256_init(CUDA_SHA256_CTX* ctx)
{
	ctx->datalen = 0;
	ctx->bitlen = 0;
	ctx->state[0] = 0x6a09e667;
	ctx->state[1] = 0xbb67ae85;
	ctx->state[2] = 0x3c6ef372;
	ctx->state[3] = 0xa54ff53a;
	ctx->state[4] = 0x510e527f;
	ctx->state[5] = 0x9b05688c;
	ctx->state[6] = 0x1f83d9ab;
	ctx->state[7] = 0x5be0cd19;
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
	WORD i;

	i = ctx->datalen;

	// Pad whatever data is left in the buffer.
	if (ctx->datalen < 56) {
		ctx->data[i++] = 0x80;
		while (i < 56)
			ctx->data[i++] = 0x00;
	}
	else {
		ctx->data[i++] = 0x80;
		while (i < 64)
			ctx->data[i++] = 0x00;
		cuda_sha256_transform(ctx, ctx->data);
		memset(ctx->data, 0, 56);
	}

	// Append to the padding the total message's length in bits and transform.
	ctx->bitlen += ctx->datalen * 8;
	ctx->data[63] = ctx->bitlen;
	ctx->data[62] = ctx->bitlen >> 8;
	ctx->data[61] = ctx->bitlen >> 16;
	ctx->data[60] = ctx->bitlen >> 24;
	ctx->data[59] = ctx->bitlen >> 32;
	ctx->data[58] = ctx->bitlen >> 40;
	ctx->data[57] = ctx->bitlen >> 48;
	ctx->data[56] = ctx->bitlen >> 56;
	cuda_sha256_transform(ctx, ctx->data);

	// Since this implementation uses little endian byte ordering and SHA uses big endian,
	// reverse all the bytes when copying the final state to the output hash.
	for (i = 0; i < 4; ++i) {
		hash[i] = (ctx->state[0] >> (24 - i * 8)) & 0x000000ff;
		hash[i + 4] = (ctx->state[1] >> (24 - i * 8)) & 0x000000ff;
		hash[i + 8] = (ctx->state[2] >> (24 - i * 8)) & 0x000000ff;
		hash[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0x000000ff;
		hash[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0x000000ff;
		hash[i + 20] = (ctx->state[5] >> (24 - i * 8)) & 0x000000ff;
		hash[i + 24] = (ctx->state[6] >> (24 - i * 8)) & 0x000000ff;
		hash[i + 28] = (ctx->state[7] >> (24 - i * 8)) & 0x000000ff;
	}
}

__global__ void kernel_sha256_hash(BYTE* indata, WORD inlen, BYTE* outdata, WORD n_batch)
{
	WORD thread = blockIdx.x * blockDim.x + threadIdx.x;
	if (thread >= n_batch)
	{
		return;
	}
	BYTE* in = indata + thread * inlen;
	BYTE* out = outdata + thread * SHA256_BLOCK_SIZE;
	CUDA_SHA256_CTX ctx;
	cuda_sha256_init(&ctx);
	cuda_sha256_update(&ctx, in, inlen);
	cuda_sha256_final(&ctx, out);
}

void mcm_cuda_sha256_hash_batch(BYTE* in, WORD inlen, BYTE* out, WORD n_batch)
{
	BYTE* cuda_indata;
	BYTE* cuda_outdata;
	cudaMalloc(&cuda_indata, inlen * n_batch);
	cudaMalloc(&cuda_outdata, SHA256_BLOCK_SIZE * n_batch);
	cudaMemcpy(cuda_indata, in, inlen * n_batch, cudaMemcpyHostToDevice);

	WORD thread = 256;
	WORD block = (n_batch + thread - 1) / thread;

	kernel_sha256_hash << < block, thread >> > (cuda_indata, inlen, cuda_outdata, n_batch);
	cudaMemcpy(out, cuda_outdata, SHA256_BLOCK_SIZE * n_batch, cudaMemcpyDeviceToHost);
	cudaDeviceSynchronize();
	cudaError_t error = cudaGetLastError();
	if (error != cudaSuccess) {
		printf("Error cuda sha256 hash: %s \n", cudaGetErrorString(error));
	}
	cudaFree(cuda_indata);
	cudaFree(cuda_outdata);
}
// --------------------------------------------------------------------------------
// --------------------------------------------------------------------------------
// --------------------------------------------------------------------------------


// --------------------------------------------------------------------------------
// Helper: convert 32-byte BYTE hash to 4 x uint64_t (big-endian, matches
// existing interface and unit test expected values)
// --------------------------------------------------------------------------------
static void ByteHashToUint64(const BYTE* hash32, uint64_t* out4)
{
    for (int i = 0; i < 4; i++)
    {
        out4[i] =
            ((uint64_t)hash32[i * 8 + 0] << 56) |
            ((uint64_t)hash32[i * 8 + 1] << 48) |
            ((uint64_t)hash32[i * 8 + 2] << 40) |
            ((uint64_t)hash32[i * 8 + 3] << 32) |
            ((uint64_t)hash32[i * 8 + 4] << 24) |
            ((uint64_t)hash32[i * 8 + 5] << 16) |
            ((uint64_t)hash32[i * 8 + 6] << 8) |
            ((uint64_t)hash32[i * 8 + 7]);
    }
}


// --------------------------------------------------------------------------------
// SHA256HashWrapper_CPU - single file
// --------------------------------------------------------------------------------
void SHA256HashWrapper_CPU(uint8_t* h_input, uint64_t input_size, uint64_t* h_hash)
{
    BYTE out[32] = { 0 };
    mcm_cuda_sha256_hash_batch(
        h_input,
        static_cast<WORD>(input_size),
        out,
        1
    );
    ByteHashToUint64(out, h_hash);
}

// --------------------------------------------------------------------------------
// SHA256BatchWrapper_CPU - batch of files
//
// mcm_cuda_sha256_hash_batch requires all files to be the same size.
// Strategy: group files by size, call once per size group, reassemble results.
// --------------------------------------------------------------------------------
void SHA256BatchWrapper_CPU(
    std::vector<std::vector<uint8_t>>& h_inputs,
    uint64_t* h_hashes,
    uint32_t                           num_files,
    uint32_t                           threads_per_block)
{
    // --- Group file indices by size ---
    std::map<uint64_t, std::vector<uint32_t>> sizeGroups;
    for (uint32_t i = 0; i < num_files; i++)
        sizeGroups[h_inputs[i].size()].push_back(i);

    // --- Process each size group ---
    for (auto& group : sizeGroups)
    {
        uint64_t fileSize = group.first;
        std::vector<uint32_t>& indices = group.second;
        uint32_t groupCount = static_cast<uint32_t>(indices.size());

        // Build flat input buffer for this group
        std::vector<BYTE> flatInput(fileSize * groupCount);
        for (uint32_t g = 0; g < groupCount; g++)
            std::memcpy(
                flatInput.data() + g * fileSize,
                h_inputs[indices[g]].data(),
                fileSize
            );

        // Output buffer: 32 bytes per file
        std::vector<BYTE> flatOutput(32 * groupCount, 0);

        mcm_cuda_sha256_hash_batch(
            flatInput.data(),
            static_cast<WORD>(fileSize),
            flatOutput.data(),
            groupCount
        );

        // Convert output and place into correct positions in h_hashes
        for (uint32_t g = 0; g < groupCount; g++)
        {
            uint32_t originalIdx = indices[g];
            ByteHashToUint64(
                flatOutput.data() + g * 32,
                h_hashes + originalIdx * 4
            );
        }
    }
}


// --------------------------------------------------------------------------------
// ComputeMagnitudes - compute magnitude spectrum from complex FFT output
// Each thread handles one frequency bin of one file
// Output: fft_size/2 magnitudes per file (only positive frequencies)
// --------------------------------------------------------------------------------
__global__ void ComputeMagnitudes(
    cufftComplex* d_freq,
    float* d_magnitudes,
    uint32_t      num_files,
    uint32_t      fft_size)
{
    uint32_t file_idx = blockIdx.x;
    uint32_t bin_idx = blockIdx.y * blockDim.x + threadIdx.x;
    uint32_t half = fft_size / 2 + 1;  // R2C output size is fft_size/2 + 1

    if (file_idx >= num_files || bin_idx >= half)
        return;

    cufftComplex c = d_freq[(uint64_t)file_idx * half + bin_idx];
    d_magnitudes[(uint64_t)file_idx * half + bin_idx] =
        sqrtf(c.x * c.x + c.y * c.y) / fft_size;
}

// --------------------------------------------------------------------------------
// FFTBatchWrapper_CPU
// Converts PCM bytes - float - cuFFT - magnitude spectrum
// Output: h_magnitudes[num_files * fft_size/2]
// --------------------------------------------------------------------------------
void FFTBatchWrapper_CPU(
    std::vector<std::vector<uint8_t>>& h_inputs,
    float* h_magnitudes,
    uint32_t  num_files,
    uint32_t  fft_size,
    uint32_t  threads_per_block)
{
    uint64_t half = fft_size / 2 + 1;  // R2C output size

    float* d_float_flat;
    cufftComplex* d_freq;
    float* d_magnitudes;

    cudaMalloc(&d_float_flat, (uint64_t)num_files * fft_size * sizeof(float));
    cudaMalloc(&d_freq, (uint64_t)num_files * half * sizeof(cufftComplex));
    cudaMalloc(&d_magnitudes, (uint64_t)num_files * half * sizeof(float));

    cudaMemset(d_float_flat, 0, (uint64_t)num_files * fft_size * sizeof(float));

    // Convert PCM to float on CPU and copy to GPU
    for (uint32_t i = 0; i < num_files; i++)
    {
        const auto& pcm = h_inputs[i];
        uint32_t num_samples = std::min((uint32_t)(pcm.size() / 2), fft_size);

        std::vector<float> h_float(fft_size, 0.0f);
        for (uint32_t s = 0; s < num_samples; s++)
        {
            int16_t sample = (int16_t)(pcm[s * 2] | (pcm[s * 2 + 1] << 8));
            h_float[s] = sample / 32768.0f;
        }

        cudaMemcpy(
            d_float_flat + (uint64_t)i * fft_size,
            h_float.data(),
            fft_size * sizeof(float),
            cudaMemcpyHostToDevice);
    }

    // Run cuFFT batch
    cufftHandle plan;
    cufftPlan1d(&plan, fft_size, CUFFT_R2C, num_files);
    cufftExecR2C(plan, d_float_flat, d_freq);
    cudaDeviceSynchronize();
    cufftDestroy(plan);

    // Compute magnitudes
    dim3 grid_mag(num_files, (half + threads_per_block - 1) / threads_per_block);
    ComputeMagnitudes << <grid_mag, threads_per_block >> > (
        d_freq, d_magnitudes, num_files, fft_size);
    cudaDeviceSynchronize();

    cudaMemcpy(h_magnitudes, d_magnitudes,
        (uint64_t)num_files * half * sizeof(float),
        cudaMemcpyDeviceToHost);

    cudaFree(d_float_flat);
    cudaFree(d_freq);
    cudaFree(d_magnitudes);
}