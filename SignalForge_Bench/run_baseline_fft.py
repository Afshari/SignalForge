import argparse
import csv
import json
import multiprocessing
import time
import wave
from datetime import datetime
from pathlib import Path

import numpy as np
import redis
import xxhash

# --------------------------------------------------------------------------------
SCRIPT_NAME    = "run_baseline_fft.py"
SCRIPT_DESC    = "Python FFT-only baseline: read WAV -> FFT -> xxHash -> Redis dedup -> store magnitudes"
DEFAULT_PARAMS = "baseline_params.json"

PARAMS_TEMPLATE = {
    "input_dir":       "Path to directory containing .wav files (searched recursively)",
    "results_dir":     "Path where CSV benchmark results are saved",
    "redis": {
        "host":        "Redis server hostname or IP  [CRITICAL]",
        "port":        "Redis server port (default: 6379)",
        "db":          "Redis database index (0=prod, 1=test, 2=python-baseline)",
    },
    "fft_size":        "FFT window size in samples - must match C++ config  [CRITICAL]",
    "worker_counts":   "List of worker counts to sweep, e.g. [1, 2, 4, 8]",
    "file_sizes_kb":   "Informational only - not used by this script",
    "runs_per_config": "Number of runs per worker count for averaging",
}

PARAMS_EXAMPLE = {
    "input_dir":       "../SignalForge_Tests/test_data",
    "results_dir":     "results",
    "redis": {
        "host":        "redis",
        "port":        6379,
        "db":          2,
    },
    "fft_size":        8192,
    "worker_counts":   [1, 2, 4, 8],
    "file_sizes_kb":   [100, 500, 1024],
    "runs_per_config": 3,
}


# --------------------------------------------------------------------------------
def print_list_params():
    print(f"\n{SCRIPT_NAME} -- parameter reference")
    print(f"Default params file: {DEFAULT_PARAMS} (same directory as script)")
    print(f"Override with:       python {SCRIPT_NAME} --params /path/to/custom.json\n")

    print("Parameters:")
    print(f"  input_dir       {PARAMS_TEMPLATE['input_dir']}")
    print(f"  results_dir     {PARAMS_TEMPLATE['results_dir']}")
    print(f"  redis.host      {PARAMS_TEMPLATE['redis']['host']}")
    print(f"  redis.port      {PARAMS_TEMPLATE['redis']['port']}")
    print(f"  redis.db        {PARAMS_TEMPLATE['redis']['db']}")
    print(f"  fft_size        {PARAMS_TEMPLATE['fft_size']}")
    print(f"  worker_counts   {PARAMS_TEMPLATE['worker_counts']}")
    print(f"  file_sizes_kb   {PARAMS_TEMPLATE['file_sizes_kb']}")
    print(f"  runs_per_config {PARAMS_TEMPLATE['runs_per_config']}")

    print("\nExample baseline_params.json:")
    print(json.dumps(PARAMS_EXAMPLE, indent=4))


# --------------------------------------------------------------------------------
def load_params(params_path: Path) -> dict:
    with open(params_path, "r") as f:
        return json.load(f)


# --------------------------------------------------------------------------------
def read_pcm(path: Path) -> bytes:
    with wave.open(str(path), "rb") as wf:
        return wf.readframes(wf.getnframes())


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
def compute_xxhash(magnitudes: np.ndarray) -> str:
    return xxhash.xxh64(magnitudes.tobytes()).hexdigest()


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

        pcm        = read_pcm(path)
        magnitudes = compute_fft(pcm, fft_size)
        xxhash_hex = compute_xxhash(magnitudes)
        redis_key  = f"fft_mag:0:{xxhash_hex}"

        if r.exists(redis_key):
            result["skipped"] = True
            result["elapsed"] = time.perf_counter() - t0
            return result

        r.set(redis_key, magnitudes.tobytes())

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
    redis_cfg = params["redis"]
    fft_size  = params["fft_size"]
    host      = redis_cfg["host"]
    port      = redis_cfg["port"]
    db        = redis_cfg["db"]
    runs      = params["runs_per_config"]
    rows      = []

    for worker_count in params["worker_counts"]:
        run_times = []

        for run_idx in range(runs):
            flush_redis(host, port, db)

            args = [(Path(f), fft_size, host, port, db) for f in files]

            t_start = time.perf_counter()
            with multiprocessing.Pool(processes=worker_count) as pool:
                results = pool.map(process_file, args)
            elapsed = time.perf_counter() - t_start

            skipped   = sum(1 for r in results if r["skipped"])
            errors    = sum(1 for r in results if r["error"])
            processed = len(files) - skipped - errors
            run_times.append(elapsed)

            print(f"  workers={worker_count} run={run_idx + 1}/{runs} "
                  f"elapsed={elapsed:.3f}s "
                  f"processed={processed} skipped={skipped} errors={errors}")

        avg_elapsed = sum(run_times) / len(run_times)
        throughput  = len(files) / avg_elapsed

        rows.append({
            "worker_count":             worker_count,
            "total_files":              len(files),
            "avg_elapsed_sec":          round(avg_elapsed, 3),
            "throughput_files_per_sec": round(throughput, 1),
            "runs":                     runs,
        })

        print(f"  --> workers={worker_count} avg={avg_elapsed:.3f}s "
              f"throughput={throughput:.1f} files/sec\n")

    return rows


# --------------------------------------------------------------------------------
def save_csv(rows: list, results_dir: Path) -> Path:
    results_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_path  = results_dir / f"baseline_fft_{timestamp}.csv"

    with open(csv_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)

    return csv_path


# --------------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        prog=SCRIPT_NAME,
        description=SCRIPT_DESC,
        formatter_class=argparse.RawTextHelpFormatter,
        epilog=(
            "Critical params to check in baseline_params.json before running:\n"
            "  input_dir   -- path to .wav files\n"
            "  redis.host  -- Redis server address\n"
            "  fft_size    -- must match C++ config.json fft.fft_size\n\n"
            "Run 'python run_baseline_fft.py --list-params' for full parameter reference."
        )
    )
    parser.add_argument(
        "--params",
        type=Path,
        default=None,
        help=f"Path to params JSON file (default: {DEFAULT_PARAMS} next to this script)"
    )
    parser.add_argument(
        "--list-params",
        action="store_true",
        help="Show all parameters with descriptions and example JSON, then exit"
    )

    args = parser.parse_args()

    if args.list_params:
        print_list_params()
        return

    script_dir  = Path(__file__).parent
    params_path = args.params if args.params else script_dir / DEFAULT_PARAMS

    if not params_path.exists():
        print(f"ERROR: params file not found: {params_path}")
        print(f"Run 'python {SCRIPT_NAME} --list-params' to see expected format.")
        return

    params      = load_params(params_path)
    input_dir   = Path(script_dir / params["input_dir"]).resolve()
    results_dir = Path(script_dir / params["results_dir"]).resolve()

    if not input_dir.exists():
        print(f"ERROR: input_dir not found: {input_dir}")
        print(f"Check 'input_dir' in {params_path}")
        return

    files = sorted(input_dir.glob("**/*.wav"))
    if not files:
        print(f"ERROR: no .wav files found in {input_dir}")
        return

    print(f"Script:       {SCRIPT_NAME}")
    print(f"Params file:  {params_path}")
    print(f"Input dir:    {input_dir}")
    print(f"Files found:  {len(files)}")
    print(f"FFT size:     {params['fft_size']}")
    print(f"Redis:        {params['redis']['host']}:{params['redis']['port']} db={params['redis']['db']}")
    print(f"Worker sweep: {params['worker_counts']}")
    print(f"Runs/config:  {params['runs_per_config']}")
    print()

    rows     = run_sweep(params, files, results_dir)
    csv_path = save_csv(rows, results_dir)
    print(f"Results saved to: {csv_path}")


# --------------------------------------------------------------------------------
if __name__ == "__main__":
    multiprocessing.freeze_support()
    main()