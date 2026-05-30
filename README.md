# SignalForge

A GPU-accelerated distributed pipeline for ingesting, deduplicating, and analyzing real-world signal data. Designed for noisy environments where exact content hashing alone is insufficient — SignalForge uses GPU SHA-256 for fast exact-duplicate filtering, cuFFT for frequency-domain feature extraction, Redis for distributed caching, and a PyTorch LSTM autoencoder for anomaly detection.

---

## The Problem

In a distributed system ingesting signal recordings from multiple sources, two recordings of the same engine under the same conditions will produce different raw bytes due to environmental noise. Traditional content hashing (SHA-256) would treat them as different files and process both. SignalForge solves this with a two-stage approach:

1. **SHA-256 on the GPU** — eliminates exact duplicates instantly
2. **cuFFT on the GPU** — extracts frequency-domain features from survivors, enabling downstream similarity analysis and anomaly detection

---

## Pipeline Architecture

```
[Scanner thread]       \
                        --> [m_path_queue] --> [Reader] --> [m_wav_queue] --> [GPU worker] --> [m_result_queue] --> [Redis writer]
[gRPC receiver]        /
```

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

---

## Results

LSTM autoencoder trained on 200 clean engine signals, tested on 30 anomaly signals:

| Signal type | Reconstruction error | Result     |
|-------------|----------------------|------------|
| Clean       | ~0.000               | ✓ normal   |
| Anomaly     | ~0.62–0.68           | ✓ detected |

30/30 anomaly files correctly detected. 0 false positives.

Full ML details → [SignalForge_ML/README.md](SignalForge_ML/README.md)

> Throughput benchmarks (nsys pipeline profiling) coming in a future update.

---

## Project Structure

```
SignalForge/                  -- main executable (C++)
SignalForge_CPU/              -- CPU-side pipeline, gRPC receiver, Redis client
SignalForge_GPU/              -- CUDA kernels: SHA-256, cuFFT
SignalForge_Tests/            -- GoogleTest unit + integration tests (90 passing)
SignalForge_ML/               -- PyTorch LSTM autoencoder (anomaly detection)
SignalForge_Tools/            -- Python signal generator and gRPC client
SignalForge_Bench/            -- Python NCU/nsys benchmarking tools
SignalForge_Proto/            -- Protobuf definitions and generated stubs
CMakeLists.txt                -- root CMake (Linux/Docker)
Dockerfile
docker-compose.yml
config.json
DEVGUIDE.md                   -- developer operations guide
```

---

## Tech Stack

| Component       | Technology                        |
|-----------------|-----------------------------------|
| Language        | C++20, Python 3.10                |
| GPU compute     | CUDA 12.0, cuFFT                  |
| Distributed RPC | gRPC 1.54 + Protobuf              |
| Caching         | Redis 7 (hiredis)                 |
| ML              | PyTorch, LSTM autoencoder         |
| Config          | Boost.JSON                        |
| Testing         | GoogleTest (90 tests)             |
| Build           | CMake, Visual Studio 2022         |
| Deployment      | Docker, Docker Compose            |
| Target hardware | AWS g4dn.xlarge (NVIDIA Tesla T4) |

---

## Requirements

- NVIDIA GPU with CUDA support (tested on Tesla T4 sm_75 and RTX 3060 sm_86)
- CUDA Toolkit 12.0+
- C++20 compiler (MSVC on Windows, GCC on Linux)
- CMake 3.20+ (Linux / Docker)
- Boost 1.91+ (Boost.JSON)
- hiredis
- gRPC 1.54+ with Protobuf
- Redis server
- Python 3.10+ with PyTorch (ML module)

---

## Build

### Windows (Visual Studio)

Open `SignalForge.sln` in Visual Studio 2022, select `x64 / Release`, and build the solution. Output lands in `x64/Release/`.

Install dependencies via vcpkg:
```bash
vcpkg install grpc:x64-windows
vcpkg install hiredis:x64-windows
```

### Linux (CMake)

```bash
mkdir build && cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CUDA_ARCHITECTURES=75 \
    -DBOOST_ROOT=/usr/local \
    -DCMAKE_PREFIX_PATH="/usr/local"
cmake --build . --config Release -j$(nproc)
```

### Docker

```bash
docker-compose build
docker-compose run --rm --entrypoint /bin/bash signalforge -c \
    "cd /app/x64/Release && ./SignalForge_Tests"
```

---

## Run

### Local pipeline (scan input directory)
```bash
./SignalForge --pipeline
```

### Distributed mode (receive files via gRPC)
```bash
# Terminal 1 -- start the server
./SignalForge --grpc

# Terminal 2 -- send files from Python client
cd SignalForge_Tools
python grpc_client.py
```

### SHA-256 only
```bash
./SignalForge
```

### SHA-256 + FFT
```bash
./SignalForge --fft
```

### Profiling mode
```bash
./SignalForge --profile
```

---

## Configuration (`config.json`)

```json
{
    "file": {
        "max_file_size_kb": 2048,
        "sample_rate": 44100
    },
    "kernels": {
        "sha256": { "batch_size": 5120, "threads_per_block": 128 },
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

SHA-256 GPU implementation based on code from [VanitySearch](https://github.com/JeanLucPons/VanitySearch) by Jean-Luc Pons, licensed under GPL-3.0.