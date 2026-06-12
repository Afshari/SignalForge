# SignalForge

A GPU-accelerated distributed signal processing pipeline built with CUDA, cuFFT, Redis, and C++20.

## Contents

- [SignalForge](#signalforge)
  - [Contents](#contents)
  - [Architecture](#architecture)
    - [Pipeline stages](#pipeline-stages)
    - [Signal types](#signal-types)
  - [Project Structure](#project-structure)
  - [Build](#build)
    - [Windows (Visual Studio 2022)](#windows-visual-studio-2022)
    - [Linux / Docker](#linux--docker)
  - [Run](#run)
    - [Interactive mode](#interactive-mode)
    - [Generate test signals](#generate-test-signals)
    - [Pipeline mode - FFT-only (default, no flag needed)](#pipeline-mode---fft-only-default-no-flag-needed)
    - [Pipeline mode - SHA-256 + FFT](#pipeline-mode---sha-256--fft)
    - [Hash mode - SHA-256 only, sequential](#hash-mode---sha-256-only-sequential)
    - [FFT mode - cuFFT only, sequential](#fft-mode---cufft-only-sequential)
    - [Profile mode - SHA-256 with timing](#profile-mode---sha-256-with-timing)
    - [gRPC mode](#grpc-mode)
  - [Tests](#tests)
    - [Test categories](#test-categories)
  - [Redis](#redis)
    - [WSL2 (Windows development)](#wsl2-windows-development)
    - [Docker](#docker)
    - [Inspect data](#inspect-data)
    - [Redis databases](#redis-databases)
    - [Persistence](#persistence)
  - [NCU Profiling](#ncu-profiling)
    - [Profiling results summary](#profiling-results-summary)
  - [Python Baseline Benchmark](#python-baseline-benchmark)
  - [Git](#git)
    - [Important notes](#important-notes)
    - [Branch strategy](#branch-strategy)
  - [Docker Compose](#docker-compose)
  - [AWS (g4dn.xlarge, Tesla T4)](#aws-g4dnxlarge-tesla-t4)
  - [Environment Variables](#environment-variables)

## Architecture

![Pipeline Architecture](assets/pipeline.svg)

### Pipeline stages

**FFT pipeline (default):**
- **Scanner** — reads file paths from `input_dir` (root and one level of subdirectories)
- **Reader** — reads WAV files into memory, batches PCM data up to `fft.batch_size`
- **GPU worker** — runs cuFFT on each batch, pushes magnitude arrays to result queue
- **Redis writer** — computes xxHash64 of magnitude arrays, deduplicates, stores as `fft_mag:0:<xxhash>`

**SHA-256 pipeline (`--pipeline-sha256`):**
- **Scanner** — same as above
- **Reader** — batches PCM data up to `sha256.batch_size`
- **GPU worker** — runs SHA-256 on each batch, pushes hashes to result queue
- **Redis writer** — deduplicates and stores as `sha256:<hex>`
  

### Signal types
- **Clean** -- engine fundamental (80Hz) + harmonics, deterministic (same content = same SHA-256)
- **Noisy** -- same harmonics + Gaussian noise, unique per file
- **Anomaly** -- same harmonics + 1350Hz spike, simulates engine knock

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

## Build

![Build Flow](assets//build_flow.svg)

### Windows (Visual Studio 2022)
Open `SignalForge.sln` and build in Release x64.

### Linux / Docker

Build the Docker image:
```bash
docker compose build
```

Rebuild after code changes:
```bash
docker compose build --no-cache
```

---

## Run

### Interactive mode
```bash
# Start Redis first
docker compose up -d redis

# Enter container interactively
docker compose --profile shell run shell
```

### Generate test signals
```bash
# Inside container
cd /app/SignalForge_Tools
python3 generate_signals.py
```

### Pipeline mode - FFT-only (default, no flag needed)
```bash
docker compose --profile shell run --rm shell -c "/app/x64/Release/SignalForge"
```

### Pipeline mode - SHA-256 + FFT
```bash
docker compose --profile shell run --rm shell -c "/app/x64/Release/SignalForge --pipeline-sha256"
```

### Hash mode - SHA-256 only, sequential
```bash
docker compose --profile shell run --rm shell -c "/app/x64/Release/SignalForge --hash"
```

### FFT mode - cuFFT only, sequential
```bash
docker compose --profile shell run --rm shell -c "/app/x64/Release/SignalForge --fft"
```

### Profile mode - SHA-256 with timing
```bash
docker compose --profile shell run --rm shell -c "/app/x64/Release/SignalForge --profile"
```

### gRPC mode
```bash
docker compose --profile shell run --rm shell -c "/app/x64/Release/SignalForge --grpc"
```

---

## Tests

Run all tests:
```bash
docker compose --profile test up
```

### Test categories
- **Unit tests** -- no external dependencies, always run
- **Integration tests** -- require Redis (RedisClientTests, PipelineTests, GrpcTests)

Redis is started automatically via Docker Compose when running tests.

---

## Redis

### WSL2 (Windows development)

Check if Redis is running:
```bash
redis-cli ping
```

Start Redis:
```bash
sudo service redis-server start
```

Stop Redis:
```bash
sudo service redis-server stop
```

Flush all data:
```bash
redis-cli flushall
```

Flush test database only (db=1):
```bash
redis-cli -n 1 flushdb
```

Connect to Redis CLI:
```bash
redis-cli
```

Connect to test database:
```bash
redis-cli -n 1
```

### Docker

Flush production database:
```bash
redis-cli -h redis flushdb
```

Flush test database (db=1):
```bash
redis-cli -h redis -n 1 flushdb
```

Connect to Redis CLI:
```bash
redis-cli -h redis
```

### Inspect data

List FFT pipeline magnitudes not yet consumed by the autoencoder:
```bash
redis-cli -h redis -n 0 KEYS "fft_mag:0:*"
```

List FFT pipeline magnitudes consumed by the autoencoder:
```bash
redis-cli -h redis -n 0 KEYS "fft_mag:1:*"
```

List SHA-256 pipeline hashes in production database:
```bash
redis-cli -h redis -n 0 KEYS "sha256:*"
```

### Redis databases
- **db=0** -- production data (hashes and magnitudes)
- **db=1** -- test data (used by integration tests, safe to flush)
- **db=2** -- Python baseline benchmark data (safe to flush)

### Persistence
RDB snapshots configured in `/etc/redis/redis.conf`:
```
save 900 1
save 300 10
save 60 10000
```

---

## NCU Profiling

Run NCU sweep (requires privileged container):
```bash
docker compose --profile shell run --privileged shell -c \
    "cd /app/SignalForge_Bench && python3 run_ncu.py"
```

### Profiling results summary
| Kernel   | Bound   | Best config                        | Throughput     |
|----------|---------|------------------------------------|----------------|
| SHA-256  | Compute | batch=5120, threads_per_block=128  | ~574 files/sec |
| cuFFT    | Memory  | batch=1024, threads_per_block=256  | memory-bound   |

---

## Python Baseline Benchmark

Run the Python multiprocessing baseline to compare against SignalForge C++/CUDA:

```bash
# Start Redis and enter container
docker compose up -d redis
docker compose --profile shell run shell

# Inside container — FFT-only baseline (compares against default pipeline)
cd /app/SignalForge_Bench
python3 run_baseline_fft.py

# Inside container — SHA-256 + FFT baseline (compares against --pipeline-sha256)
python3 run_baseline_fft_sha256.py
```

Results saved to `SignalForge_Bench/results/baseline_fft_<timestamp>.csv`.
Configure worker count, input directory, and Redis host in `baseline_params.json`.
Run `python3 run_baseline_fft.py --list-params` to see all available parameters.

---

## Git

### Important notes
- `config.json` is tracked but local changes are ignored:
```bash
git update-index --skip-worktree config.json
```
Re-apply after cloning:
```bash
git update-index --skip-worktree config.json
```

### Branch strategy
- `main` -- stable, all work merged here
- `feature/*` -- feature branches, merge to main with --no-ff

---

## Docker Compose

Start Redis service only:
```bash
docker compose up -d redis
```

Stop all services:
```bash
docker compose down
```

Clean up containers, networks, dangling images:
```bash
docker system prune -f
```

Remove a specific image:
```bash
docker rmi signalforge:latest
```

---

## AWS (g4dn.xlarge, Tesla T4)

SSH into instance:
```bash
ssh ubuntu@<aws-ip>
```

Pull latest code:
```bash
cd ~/SignalForge
git pull origin main
```

Build and run on AWS:
```bash
docker compose build
docker compose --profile run up
```

---

## Environment Variables

| Variable     | Default     | Description                        |
|--------------|-------------|------------------------------------|
| REDIS_HOST   | 127.0.0.1   | Redis server hostname              |
| REDIS_PORT   | 6379        | Redis server port                  |

These are set automatically by Docker Compose via `docker-compose.yml`.

---
