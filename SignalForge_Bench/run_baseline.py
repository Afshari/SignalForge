import csv
import hashlib
import json
import multiprocessing
import os
import struct
import time
import wave
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
import redis

# SignalForge_Bench - Python multiprocessing baseline
# Compares against SignalForge C++ + CUDA pipeline
# Each worker: read WAV -> SHA-256 -> Redis dedup -> FFT -> store magnitudes

PARAMS_FILE = "baseline_params.json"


# --------------------------------------------------------------------------------
def load_params() -> dict:
    script_dir = Path(__file__).parent
    with open(script_dir / PARAMS_FILE, "r") as f:
        return json.load(f)


# --------------------------------------------------------------------------------
def now_iso8601() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


# --------------------------------------------------------------------------------
def read_pcm(path: Path) -> bytes:
    with wave.open(str(path), "rb") as wf:
        return wf.readframes(wf.getnframes())


# --------------------------------------------------------------------------------
def compute_sha256(pcm: bytes) -> str:
    return hashlib.sha256(pcm).hexdigest()


# --------------------------------------------------------------------------------
def compute_fft(pcm: bytes, fft_size: int) -> np.ndarray:
    samples = np.frombuffer(pcm, dtype=np.int16).astype(np.float32) / 32767.0
    if len(samples) < fft_size:
        samples = np.pad(samples, (0, fft_size - len(samples)))
    else:
        samples = samples[:fft_size]
    spectrum = np.fft.rfft(samples, n=fft_size)
    return np.abs(spectrum).astype(np.float32)


# --------------------------------------------------------------------------------
def store_magnitudes(r: redis.Redis, hex_hash: str, magnitudes: np.ndarray) -> None:
    raw = magnitudes.tobytes()
    r.set(f"fft:{hex_hash}", raw)


# --------------------------------------------------------------------------------
def process_file(args: tuple) -> dict:
    path, fft_size, redis_host, redis_port, redis_db = args

    result = {
        "file":    str(path),
        "skipped": False,
        "error":   None,
        "elapsed": 0.0,
    }

    t0 = time.perf_counter()

    try:
        r = redis.Redis(host=redis_host, port=redis_port, db=redis_db)

        # read PCM
        pcm = read_pcm(path)

        # SHA-256
        hex_hash = compute_sha256(pcm)

        # Redis dedup check
        # atomic check-and-set - returns True if key was set, False if already existed
        if not r.set(f"hash:{hex_hash}", now_iso8601(), nx=True):
            result["skipped"] = True
            result["elapsed"] = time.perf_counter() - t0
            return result

        # store hash
        r.set(f"hash:{hex_hash}", now_iso8601())

        # FFT
        magnitudes = compute_fft(pcm, fft_size)

        # store magnitudes
        store_magnitudes(r, hex_hash, magnitudes)

    except Exception as e:
        result["error"] = str(e)

    result["elapsed"] = time.perf_counter() - t0
    return result


# --------------------------------------------------------------------------------
def flush_redis(host: str, port: int, db: int) -> None:
    r = redis.Redis(host=host, port=port, db=db)
    r.flushdb()


# --------------------------------------------------------------------------------
def run_sweep(params: dict, files: list, results_dir: Path) -> list:
    redis_cfg  = params["redis"]
    fft_size   = params["fft_size"]
    host       = redis_cfg["host"]
    port       = redis_cfg["port"]
    db         = redis_cfg["db"]
    runs       = params["runs_per_config"]
    rows       = []

    for worker_count in params["worker_counts"]:
        run_times = []

        for run_idx in range(runs):
            # flush Redis before each run for fair comparison
            flush_redis(host, port, db)

            args = [(Path(f), fft_size, host, port, db) for f in files]

            t_start = time.perf_counter()

            with multiprocessing.Pool(processes=worker_count) as pool:
                results = pool.map(process_file, args)

            elapsed = time.perf_counter() - t_start

            skipped = sum(1 for r in results if r["skipped"])
            errors  = sum(1 for r in results if r["error"])
            processed = len(files) - skipped - errors

            run_times.append(elapsed)

            print(f"  workers={worker_count} run={run_idx + 1}/{runs} "
                  f"elapsed={elapsed:.3f}s "
                  f"processed={processed} skipped={skipped} errors={errors}")

        avg_elapsed    = sum(run_times) / len(run_times)
        throughput     = len(files) / avg_elapsed

        rows.append({
            "worker_count":     worker_count,
            "total_files":      len(files),
            "avg_elapsed_sec":  round(avg_elapsed, 3),
            "throughput_files_per_sec": round(throughput, 1),
            "runs":             runs,
        })

        print(f"  --> workers={worker_count} avg={avg_elapsed:.3f}s "
              f"throughput={throughput:.1f} files/sec\n")

    return rows


# --------------------------------------------------------------------------------
def save_csv(rows: list, results_dir: Path) -> Path:
    results_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_path  = results_dir / f"baseline_{timestamp}.csv"

    with open(csv_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)

    return csv_path


# --------------------------------------------------------------------------------
def main():
    script_dir  = Path(__file__).parent
    params      = load_params()
    input_dir   = Path(script_dir / params["input_dir"]).resolve()
    results_dir = Path(script_dir / params["results_dir"]).resolve()

    if not input_dir.exists():
        print(f"ERROR: input_dir not found: {input_dir}")
        return

    # collect WAV files
    files = sorted(input_dir.glob("**/*.wav"))
    if not files:
        print(f"ERROR: no .wav files found in {input_dir}")
        return

    print(f"Input dir:    {input_dir}")
    print(f"Files found:  {len(files)}")
    print(f"FFT size:     {params['fft_size']}")
    print(f"Worker sweep: {params['worker_counts']}")
    print(f"Runs/config:  {params['runs_per_config']}")
    print()

    rows = run_sweep(params, files, results_dir)

    csv_path = save_csv(rows, results_dir)
    print(f"Results saved to: {csv_path}")


# --------------------------------------------------------------------------------
if __name__ == "__main__":
    multiprocessing.freeze_support()
    main()