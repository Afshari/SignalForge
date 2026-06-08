# SignalForge

A GPU-accelerated distributed pipeline for ingesting, deduplicating, and analyzing real-world signal data. Designed for noisy environments where exact content hashing alone is insufficient — SignalForge uses GPU SHA-256 for fast exact-duplicate filtering, cuFFT for frequency-domain feature extraction, Redis for distributed caching, and a PyTorch LSTM autoencoder for anomaly detection.

---

## Table of Contents
- [SignalForge](#signalforge)
  - [Table of Contents](#table-of-contents)
  - [The Problem](#the-problem)
  - [Pipeline Architecture](#pipeline-architecture)
  - [Current Limitations](#current-limitations)
  - [Results](#results)
  - [Benchmark](#benchmark)
  - [Project Structure](#project-structure)
  - [Requirements](#requirements)
  - [| OS              | Windows 10/11, Ubuntu 22.04, Docker               |](#-os---------------windows-1011-ubuntu-2204-docker---------------)
  - [Build \& Run](#build--run)
    - [Windows (Visual Studio)](#windows-visual-studio)
    - [Linux / Docker](#linux--docker)
    - [Run](#run)
  - [Configuration (`config.json`)](#configuration-configjson)
  - [Tests](#tests)
  - [Credits](#credits)

## The Problem

In a distributed system ingesting signal recordings from multiple sources, two recordings of the same engine under the same conditions will produce different raw bytes due to environmental noise. Traditional content hashing (SHA-256) would treat them as different files and process both. SignalForge solves this with a two-stage approach:

1. **SHA-256 on the GPU** — eliminates exact duplicates instantly
2. **cuFFT on the GPU** — extracts frequency-domain features from survivors, enabling downstream similarity analysis and anomaly detection

---

## Pipeline Architecture

![Pipeline Architecture](docs/assets/pipeline.svg)

**Stage 1 — Ingestion (two producers in parallel):**
- Scanner thread reads existing WAV files from `input_dir`
- gRPC receiver thread accepts files from remote Python clients over the network

**Stage 2 — Reader:**
- Pops file paths, reads raw PCM data, batches up to SHA-256 batch size

**Stage 3 — GPU worker (SHA-256 + FFT):**
- Runs GPU SHA-256 on the entire batch
- Checks Redis — skips exact duplicates already seen
- Accumulates survivors and runs cuFFT batch when FFT batch size is reached

**Stage 4 — Redis writer:**
- Stores SHA-256 hashes with ISO 8601 timestamps (deduplication index)
- Stores FFT magnitude arrays (frequency features for downstream ML)

**Stage 5 — ML (SignalForge_ML):**
- LSTM autoencoder reads FFT magnitudes from Redis
- Flags signals that deviate from the learned normal pattern as anomalies

## Current Limitations

**Scenario 1 (current implementation):** The pipeline requires all input files
in a batch to be the same size. Mixed file sizes are not yet supported.

Planned — **Scenario 2:** Input files will be organized into subdirectories
by size (`input/100kb/`, `input/500kb/`, etc.), allowing the pipeline to
process each size group as a uniform batch. gRPC receiver will route
incoming files to the correct subdirectory automatically.

---

## Results

LSTM autoencoder trained on 200 clean engine signals, tested on 30 anomaly signals:

| Signal type | Reconstruction error | Result     |
|-------------|----------------------|------------|
| Clean       | ~0.000               | ✓ normal   |
| Anomaly     | ~0.62–0.68           | ✓ detected |

30/30 anomaly files correctly detected. 0 false positives.

Full ML details → [SignalForge_ML/README.md](SignalForge_ML/README.md)

## Benchmark

Tested on AWS EC2 g4dn.xlarge (Tesla T4, 16GB GPU, Ubuntu 22.04), Docker container, CUDA 12.0.

| File size | Files | SHA-256 | FFT    | Redis  | Total  | Throughput     |
|-----------|-------|---------|--------|--------|--------|----------------|
| 100 KB    | 10,004 | 3.0s   | 0.26s  | 0.87s  | 4.6s   | ~2,175 files/s |
| 500 KB    | 10,010 | 8.2s   | 0.27s  | 0.86s  | 11.1s  | ~902 files/s   |

Config: `sha256.batch_size=1024`, `threads_per_block=64`, `reader_threads=4`

**vs Python baseline (same hardware):**

| File size | C++ pipeline | Python (best) | Speedup |
|-----------|-------------|---------------|---------|
| 100 KB    | 4.6s        | 21.9s         | 4.7x    |
| 500 KB    | 11.1s       | 23.6s         | 2.1x    |

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
├── SignalForge_Bench/         # NCU/nsys benchmarking tools
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
# SHA-256 only (Windows)
SignalForge.exe

# SHA-256 + FFT (Windows)
SignalForge.exe --fft

# Full multithreaded pipeline (Windows)
SignalForge.exe --pipeline

# gRPC distributed mode (Windows)
SignalForge.exe --grpc

# Profiling mode (Windows)
SignalForge.exe --profile

# Custom config directory (Windows)
SignalForge.exe --config path/to/config

# Linux / Docker — same flags, replace SignalForge.exe with ./SignalForge
```

For detailed commands, cleanup, and troubleshooting → [DEVGUIDE.md](docs/DEVGUIDE.md)

---

## Configuration (`config.json`)

```json
{
    "file": {
        "max_file_size_kb": 2048,
        "sample_rate": 44100
    },
    "kernels": {
        "sha256": { "batch_size": 1024, "threads_per_block": 64 },
        "fft":    { "batch_size": 1024, "threads_per_block": 256, "fft_size": 65536 }
    },
    "paths": {
        "input_dir":     "input",
        "output_dir":    "output",
        "test_data_dir": "test_data"
    }
}
```

---

## Tests

90 tests across 10 test suites covering SHA-256, FFT, Redis, pipeline, and gRPC:

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
