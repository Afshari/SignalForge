import csv
import json
import os
import subprocess
import sys
from datetime import datetime
from pathlib import Path

# SignalForge_Bench - nsys profiling runner
# Sweeps thread_block_size x file_size x batch_size
# Results saved to CSV

PARAMS_FILE = "benchmark_params.json"


# --------------------------------------------------------------------------------
def load_params() -> dict:
    script_dir  = Path(__file__).parent
    params_path = script_dir / PARAMS_FILE
    if not params_path.exists():
        print(f"ERROR: {PARAMS_FILE} not found at {params_path}")
        sys.exit(1)
    with open(params_path, "r") as f:
        return json.load(f)


# --------------------------------------------------------------------------------
def resolve_path(script_dir: Path, rel_path: str) -> Path:
    return (script_dir / rel_path).resolve()


# --------------------------------------------------------------------------------
def update_config(config_path: Path, threads_per_block: int, batch_size: int) -> None:
    with open(config_path, "r") as f:
        config = json.load(f)
    config["gpu"]["threads_per_block"] = threads_per_block
    config["batch"]["batch_size"]      = batch_size
    with open(config_path, "w") as f:
        json.dump(config, f, indent=4)


# --------------------------------------------------------------------------------
def run_nsys(
    nsys_output_path: Path,
    executable: Path,
    extra_args: str
) -> subprocess.CompletedProcess:
    cmd = [
        "nsys", "profile",
        f"--output={nsys_output_path}",
    ] + extra_args.split() + [
        str(executable), "--profile"
    ]
    print(f"  Running: {' '.join(cmd)}")
    return subprocess.run(cmd, capture_output=True, text=True)


# --------------------------------------------------------------------------------
def parse_nsys_duration(stderr: str) -> float:
    # nsys prints kernel duration in its summary — extract total CUDA time
    # Returns duration in milliseconds, or -1 if not found
    for line in stderr.splitlines():
        if "CUDA API Statistics" in line or "gpukernsum" in line:
            parts = line.split()
            for part in parts:
                try:
                    return float(part)
                except ValueError:
                    continue
    return -1.0


# --------------------------------------------------------------------------------
def main():
    script_dir = Path(__file__).parent
    params     = load_params()

    executable   = resolve_path(script_dir, params["executable"])
    config_path  = resolve_path(script_dir, params["config_file"])
    results_dir  = resolve_path(script_dir, params["results_dir"])
    nsys_out_dir = resolve_path(script_dir, params["nsys"]["output_dir"])
    extra_args   = params["nsys"]["extra_args"]

    sweep              = params["sweep"]
    thread_block_sizes = sweep["thread_block_sizes"]
    file_sizes_kb      = sweep["file_sizes_kb"]
    batch_sizes        = sweep["batch_sizes"]

    if not executable.exists():
        print(f"ERROR: executable not found: {executable}")
        sys.exit(1)

    results_dir.mkdir(parents=True, exist_ok=True)
    nsys_out_dir.mkdir(parents=True, exist_ok=True)

    # --- CSV output ---
    timestamp  = datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_path   = results_dir / f"nsys_results_{timestamp}.csv"

    total_runs = len(thread_block_sizes) * len(file_sizes_kb) * len(batch_sizes)
    run_index  = 0

    print(f"SignalForge nsys profiling sweep")
    print(f"Total runs: {total_runs}")
    print(f"Results:    {csv_path}")
    print()

    with open(csv_path, "w", newline="") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow([
            "run",
            "threads_per_block",
            "file_size_kb",
            "batch_size",
            "duration_ms",
            "throughput_files_per_sec",
            "nsys_report",
            "status"
        ])

        for threads in thread_block_sizes:
            for size_kb in file_sizes_kb:
                for batch in batch_sizes:
                    run_index += 1
                    print(f"[{run_index}/{total_runs}] threads={threads} size={size_kb}KB batch={batch}")

                    # Update config.json for this run
                    update_config(config_path, threads, batch)

                    # nsys output file
                    nsys_report = nsys_out_dir / f"nsys_t{threads}_s{size_kb}kb_b{batch}"

                    result = run_nsys(nsys_report, executable, extra_args)

                    status   = "ok" if result.returncode == 0 else "failed"
                    duration = parse_nsys_duration(result.stderr)

                    # Compute throughput
                    if duration > 0:
                        throughput = (batch / (duration / 1000.0))
                    else:
                        throughput = -1.0

                    writer.writerow([
                        run_index,
                        threads,
                        size_kb,
                        batch,
                        f"{duration:.3f}",
                        f"{throughput:.1f}",
                        str(nsys_report) + ".nsys-rep",
                        status
                    ])
                    csv_file.flush()

                    if result.returncode != 0:
                        print(f"  WARNING: run failed (exit code {result.returncode})")
                        print(f"  stderr: {result.stderr[:200]}")

                    print(f"  duration={duration:.3f}ms throughput={throughput:.1f} files/sec status={status}")
                    print()

    print(f"Sweep complete. Results saved to: {csv_path}")


# --------------------------------------------------------------------------------
if __name__ == "__main__":
    main()