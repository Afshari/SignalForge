# SignalForge

A GPU-accelerated distributed signal processing pipeline built with CUDA, cuFFT, Redis, and C++20.

## Contents

- [Pipeline Stages](#pipeline-stages)
- [Signal Types](#signal-types)
- [Project Structure](#project-structure)
- [Build](#build)
- [Run](#run)
- [Tests](#tests)
- [Redis](#redis)
- [NCU Profiling](#ncu-profiling)
- [Python Baseline Benchmark](#python-baseline-benchmark)
- [Git](#git)
- [Docker Compose](#docker-compose)
- [AWS](#aws-g4dnxlarge-tesla-t4)
- [Environment Variables](#environment-variables)

## Architecture

![Pipeline Architecture](assets/pipeline.svg)

### Pipeline stages
- **Scanner** -- reads file paths from local directory (gRPC will add remote files later)
- **Reader** -- reads WAV files into memory, batches PCM data
- **GPU worker** -- runs SHA-256 for duplicate detection, filters via Redis, accumulates survivors, runs cuFFT
- **Redis writer** -- stores hashes and FFT magnitude fingerprints

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

### Interactive Mode
```bash
# Start Redis first
docker compose up -d redis

# Enter container interactively
docker compose run --rm --entrypoint /bin/bash signalforge
```
### Generate test signals

```bash
docker compose up -d redis
docker compose run --rm --entrypoint /bin/bash signalforge

# Inside container
cd /app/SignalForge_Tools
python3 generate_signals.py --params ../SignalForge_Bench/profiling_params.json
```

### Pipeline mode (multithreaded: SHA-256 + FFT + Redis)
```bash
docker compose run --rm --entrypoint /bin/bash signalforge -c \
    "/app/x64/Release/SignalForge --pipeline"
```

With input files from host machine:
```bash
docker compose run --rm \
    -v /home/ubuntu/signalforge_input:/app/x64/Release/input \
    --entrypoint /bin/bash signalforge -c \
    "/app/x64/Release/SignalForge --pipeline"
```

### Hash mode (SHA-256 only, sequential)
```bash
docker compose run --rm --entrypoint /bin/bash signalforge -c \
    "/app/x64/Release/SignalForge"
```

### FFT mode (cuFFT only, sequential)
```bash
docker compose run --rm --entrypoint /bin/bash signalforge -c \
    "/app/x64/Release/SignalForge --fft"
```

### Profile mode (SHA-256 with timing)
```bash
docker compose run --rm --entrypoint /bin/bash signalforge -c \
    "/app/x64/Release/SignalForge --profile"
```
### gRPC mode
```bash
docker compose run --rm --entrypoint /bin/bash signalforge -c \
    "/app/x64/Release/SignalForge --grpc"
```

---

## Tests

Run all tests:
```bash
docker compose run --rm --entrypoint /bin/bash signalforge -c \
    "cd /app/x64/Release && ./SignalForge_Tests"
```

### Test categories
- **Unit tests** -- no external dependencies, always run
- **Integration tests** -- require Redis (RedisClientTests, PipelineTests)

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

### Redis databases
- **db=0** -- production data (hashes and magnitudes)
- **db=1** -- test data (used by integration tests, safe to flush)

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
docker compose run --rm --entrypoint /bin/bash --privileged signalforge -c \
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
# Start Redis first
docker compose up -d redis

# Enter container
docker compose run --rm --entrypoint /bin/bash signalforge

# Inside container
cd /app/SignalForge_Bench
python3 run_baseline.py
```

Results saved to `SignalForge_Bench/results/baseline_<timestamp>.csv`.
Configure worker count and file sizes in `baseline_params.json`.

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
docker compose run --rm --entrypoint /bin/bash signalforge -c \
    "/app/x64/Release/SignalForge --pipeline"
```

---

## Environment Variables

| Variable     | Default     | Description                        |
|--------------|-------------|------------------------------------|
| REDIS_HOST   | 127.0.0.1   | Redis server hostname              |
| REDIS_PORT   | 6379        | Redis server port                  |

These are set automatically by Docker Compose via `docker-compose.yml`.

---
