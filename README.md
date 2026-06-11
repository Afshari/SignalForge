# SignalForge

A GPU-accelerated distributed pipeline for ingesting, deduplicating, and analyzing real-world engine sound recordings. SignalForge uses cuFFT on the GPU for frequency-domain feature extraction and deduplication — two signals that sound the same produce near-identical FFT magnitude arrays, which are used as Redis deduplication keys via xxHash64. A PyTorch LSTM autoencoder consumes the stored FFT features for anomaly detection.

SHA-256-based exact deduplication is available as a second pipeline mode (`--pipeline-sha256`) for comparison or byte-level identity checks.

---

## Table of Contents
- [SignalForge](#signalforge)
  - [Table of Contents](#table-of-contents)
- [SignalForge](#signalforge-1)
  - [Pipeline Architecture](#pipeline-architecture)
  - [I/O Optimization](#io-optimization)
  - [Benchmark](#benchmark)
  - [LSTM Results](#lstm-results)
  - [Project Structure](#project-structure)
  - [Requirements](#requirements)
  - [Build \& Run](#build--run)
    - [Windows (Visual Studio)](#windows-visual-studio)
    - [Linux / Docker](#linux--docker)
    - [Run](#run)
  - [Configuration (`config.json`)](#configuration-configjson)
  - [Redis Key Schema](#redis-key-schema)
  - [Tests](#tests)
  - [Credits](#credits)

# SignalForge

A GPU-accelerated distributed pipeline for ingesting, deduplicating, and analyzing real-world engine sound recordings. SignalForge uses cuFFT on the GPU for frequency-domain feature extraction and deduplication — two signals that sound the same produce near-identical FFT magnitude arrays, which are used as Redis deduplication keys via xxHash64. A PyTorch LSTM autoencoder consumes the stored FFT features for anomaly detection.

SHA-256-based exact deduplication is available as a second pipeline mode (`--pipeline-sha256`) for comparison or byte-level identity checks.

---

## Pipeline Architecture

![Pipeline Architecture](docs/assets/pipeline.svg)

**Default pipeline (`--pipeline`, FFT-only):**

1. **Scanner thread** — reads WAV files from `input_dir` (root and one level of subdirectories)
2. **Reader threads** — read raw PCM data, batch up to `fft.batch_size` files
3. **GPU worker** — runs cuFFT on each batch, pushes magnitude arrays to result queue
4. **Redis writer** — computes xxHash64 of each magnitude array, checks for duplicates, stores new results as `fft_mag:0:<xxhash>` in Redis

**SHA-256 pipeline (`--pipeline-sha256`):**

Same as above but the GPU worker runs SHA-256 first, filters exact duplicates via `sha256:<hex>` Redis keys, then runs cuFFT on survivors. Results stored as `fft_mag_sha256:0:<xxhash>`.

**gRPC mode (`--grpc`):**

Scanner is replaced by a gRPC receiver that accepts files from remote clients over the network. Incoming files are written to `input_dir` and processed by the same reader/GPU/writer threads.

---

## I/O Optimization

File reading uses `mmap` + `posix_fadvise(POSIX_FADV_SEQUENTIAL)` on Linux instead of `std::ifstream`. This eliminates the double-copy that `ifstream` introduces (kernel buffer → ifstream buffer → user buffer) and maps the file directly into the process address space. `posix_fadvise` tells the OS to prefetch pages aggressively starting at the PCM data offset, not from byte 0.

`std::ifstream` is used as a fallback on Windows via `#ifdef _WIN32`.

This is implemented in `WavReader::ReadPCM()` in `SignalForge_CPU/src/WavReader.cpp`.

---

## Benchmark

Tested on AWS EC2 g4dn.xlarge (Tesla T4, 16GB GPU, Ubuntu 22.04), Docker container, CUDA 12.0, 21,528 mixed-size WAV files.

**C++ pipeline comparison:**

| Pipeline | Total | SHA-256 | FFT | Redis | Throughput |
|----------|-------|---------|-----|-------|------------|
| FFT-only (default) | 5.4s | — | 2.5s | 2.2s | ~3,967 files/s |
| SHA-256 + FFT | 14.1s | 9.5s | 0.6s | 2.4s | ~1,529 files/s |

Removing SHA-256 and using FFT-based deduplication gives **2.5x throughput improvement** on the same hardware. SHA-256 alone accounts for 68% of total pipeline time.

**C++ FFT-only vs Python FFT-only (same hardware):**

| Implementation | Workers | Total | Throughput | vs C++ |
|----------------|---------|-------|------------|--------|
| Python (multiprocessing) | 1 | 62.2s | 346 files/s | 11.5x slower |
| Python (multiprocessing) | 2 | 48.0s | 449 files/s | 8.8x slower |
| Python (multiprocessing) | 4 | 46.7s | 461 files/s | 8.6x slower |
| Python (multiprocessing) | 8 | 47.8s | 451 files/s | 8.8x slower |
| **C++ CUDA (default)** | — | **5.4s** | **3,967 files/s** | **baseline** |

Config: `fft.batch_size=8192`, `fft.threads_per_block=256`, `pipeline.reader_threads=4`

---

## LSTM Results

LSTM autoencoder trained on 200 clean engine signals, tested on 30 anomaly signals:

| Signal type | Reconstruction error | Result     |
|-------------|----------------------|------------|
| Clean       | ~0.000               | ✓ normal   |
| Anomaly     | ~0.62–0.68           | ✓ detected |

30/30 anomaly files correctly detected. 0 false positives.

Full ML details → [SignalForge_ML/README.md](SignalForge_ML/README.md)

---

## Project Structure

```
SignalForge/
├── SignalForge/               # Entry point (main.cpp)
├── SignalForge_CPU/           # Pipeline, gRPC receiver, Redis client
├── SignalForge_GPU/           # CUDA kernels: SHA-256, cuFFT
├── SignalForge_Tests/         # GoogleTest unit and integration tests
├── SignalForge_ML/            # PyTorch LSTM autoencoder (anomaly detection)
├── SignalForge_Tools/         # Python signal generator and gRPC client
├── SignalForge_Bench/         # Python baseline benchmarks and NCU/nsys profiling tools
├── scripts/                   # Shell utilities (show-config.sh)
├── SignalForge_Proto/         # Protobuf definitions and generated stubs
├── docs/                      # Diagrams and documentation assets
├── config.json                # Runtime configuration
├── CMakeLists.txt             # Linux/Docker build
├── Dockerfile
├── docker-compose.yml
```

---

## Requirements


| Component       | Details                                           |
|-----------------|---------------------------------------------------|
| GPU             | NVIDIA CUDA-capable (tested on Tesla T4, RTX 3060) |
| CUDA Toolkit    | 12.0+                                             |
| Compiler        | C++20 (MSVC on Windows, GCC on Linux)             |
| Build           | CMake 3.20+                                       |
| Boost           | 1.91+ (Boost.JSON)                                |
| Redis           | 7+ (hiredis client)                               |
| xxHash          | 0.8+ (built from source on Linux, vcpkg on Windows)   |
| gRPC            | 1.54+ with Protobuf                               |
| Python          | 3.10+ with PyTorch (ML module)                    |
| OS              | Windows 10/11, Ubuntu 22.04, Docker               |

---

## Build & Run

![Build Flow](docs/assets//build_flow.svg)

### Windows (Visual Studio)

Open `SignalForge.sln` in Visual Studio 2022, select `x64 / Release`, build with `Ctrl+Shift+B`.

### Linux / Docker

```bash
docker compose build
docker compose run signalforge
```

### Run

```bash
# FFT-only pipeline - default, no flag needed
./SignalForge

# SHA-256 + FFT pipeline
./SignalForge --pipeline-sha256

# Hash mode - SHA-256 only, sequential
./SignalForge --hash

# FFT mode - cuFFT only, sequential
./SignalForge --fft

# Profiling mode - SHA-256 with timing
./SignalForge --profile

# gRPC distributed mode
./SignalForge --grpc

# Custom config directory
./SignalForge --config path/to/config

# Windows - same flags, replace ./SignalForge with SignalForge.exe
```

For detailed commands, Docker profiles, Redis setup, and troubleshooting → [DEVGUIDE.md](docs/DEVGUIDE.md)

## Configuration (`config.json`)

```json
{
    "file": {
        "max_file_size_kb": 2048,
        "sample_rate": 44100
    },
    "paths": {
        "input_dir":     "input",
        "output_dir":    "output",
        "test_data_dir": "test_data"
    },
    "kernels": {
        "sha256": { "batch_size": 1024, "threads_per_block": 64 },
        "fft":    { "batch_size": 8192, "threads_per_block": 256, "fft_size": 8192 }
    },
    "pipeline": { "reader_threads": 4 },
    "redis": {
        "host": "redis",
        "port": 6379,
        "db": 0
    },
    "verbose": true
}
```
## Redis Key Schema

| Key | Value | Written by |
|-----|-------|------------|
| `sha256:<hex>` | ISO 8601 timestamp | SHA-256 pipeline GPU thread |
| `fft_mag:0:<xxhash>` | Raw float magnitudes | FFT-only pipeline writer thread |
| `fft_mag_sha256:0:<xxhash>` | Raw float magnitudes | SHA-256 pipeline writer thread |

The `0` in `fft_mag:0:` and `fft_mag_sha256:0:` indicates the entry has not yet been consumed by the LSTM autoencoder. The autoencoder writes `fft_mag:1:` and `fft_mag_sha256:1:` keys after processing.

---
## Tests

123 tests across 10 test suites covering SHA-256, FFT, Redis, pipeline, utils, and gRPC:

```bash
# Linux / Docker
cd /app/x64/Release && ./SignalForge_Tests

# Windows
x64\Release\SignalForge_Tests.exe
```

## Credits
[cuFFT](https://developer.nvidia.com/cufft) by NVIDIA Corporation. Used for GPU-accelerated Fast Fourier Transform batch processing.

The CUDA SHA-256 implementation in `SignalForge_GPU/src/gpu/SignalForge.cu` is based on [cuda-hashing-algos](https://github.com/mochimodev/cuda-hashing-algos) by mochimodev, released into the Public Domain (June 2019).

Original implementation by Brad Conte: [crypto-algorithms](https://github.com/B-Con/crypto-algorithms), Public Domain.

The `cuda_sha256_init`, `cuda_sha256_update`, `cuda_sha256_final`, `cuda_sha256_transform`, and `kernel_sha256_hash` functions were copied directly into `SignalForge.cu` and wrapped with `SHA256HashWrapper_CPU` and `SHA256BatchWrapper_CPU` to fit SignalForge's batch processing pipeline.

[xxHash](https://github.com/Cyan4973/xxHash) by Yann Collet. Used for fast non-cryptographic hashing of FFT magnitude arrays for Redis deduplication keys. Released under the BSD 2-Clause License.