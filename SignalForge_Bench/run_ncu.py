import csv
import json
import os
import re
import subprocess
import sys
from datetime import datetime
from pathlib import Path

# SignalForge_Bench - ncu profiling runner
# Sweeps thread_block_size x file_size x batch_size
# Collects per-kernel metrics: compute throughput, memory bandwidth, roofline

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
    config["kernels"]["sha256"]["threads_per_block"] = threads_per_block
    config["kernels"]["sha256"]["batch_size"]        = batch_size
    with open(config_path, "w") as f:
        json.dump(config, f, indent=4)

# --------------------------------------------------------------------------------
def run_ncu(
    ncu_output_path: Path,
    executable: Path,
    metrics: list,
    extra_args: str
) -> subprocess.CompletedProcess:
    metrics_str = ",".join(metrics)
    cmd = [
        "ncu",
        f"--output={ncu_output_path}",
        "--force-overwrite",
        f"--metrics={metrics_str}",
    ] + extra_args.split() + [
        str(executable), "--profile"
    ]
    print(f"  Running: {' '.join(cmd)}")
    return subprocess.run(cmd, capture_output=True, text=True)


# --------------------------------------------------------------------------------
def parse_ncu_metrics(stdout: str, metrics: list) -> dict:
    # Parse ncu stdout for metric values
    # ncu outputs metrics in format: "metric_name   value   unit"
    results = {m: -1.0 for m in metrics}
    for line in stdout.splitlines():
        for metric in metrics:
            # Match short metric name (last part after __)
            short_name = metric.split("__")[-1] if "__" in metric else metric
            if short_name in line or metric in line:
                parts = line.split()
                for part in parts:
                    try:
                        val = float(part.replace(",", "."))
                        results[metric] = val
                        break
                    except ValueError:
                        continue
    return results


# --------------------------------------------------------------------------------
def main():
    script_dir = Path(__file__).parent
    params     = load_params()

    executable   = resolve_path(script_dir, params["executable"])
    config_path  = resolve_path(script_dir, params["config_file"])
    results_dir  = resolve_path(script_dir, params["results_dir"])
    ncu_out_dir  = resolve_path(script_dir, params["ncu"]["output_dir"])
    metrics      = params["ncu"]["metrics"]
    extra_args   = params["ncu"]["extra_args"]

    sweep              = params["sweep"]
    thread_block_sizes = sweep["thread_block_sizes"]
    file_sizes_kb      = sweep["file_sizes_kb"]
    batch_sizes        = sweep["batch_sizes"]

    if not executable.exists():
        print(f"ERROR: executable not found: {executable}")
        sys.exit(1)

    results_dir.mkdir(parents=True, exist_ok=True)
    ncu_out_dir.mkdir(parents=True, exist_ok=True)

    # --- CSV output ---
    timestamp  = datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_path   = results_dir / f"ncu_results_{timestamp}.csv"

    total_runs = len(thread_block_sizes) * len(file_sizes_kb) * len(batch_sizes)
    run_index  = 0

    # Build CSV headers dynamically from metrics list
    metric_short_names = [m.split("__")[-1] for m in metrics]

    print(f"SignalForge ncu profiling sweep")
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
            *metric_short_names,
            "ncu_report",
            "status",
            "bound"
        ])

        for threads in thread_block_sizes:
            for size_kb in file_sizes_kb:
                for batch in batch_sizes:
                    run_index += 1
                    print(f"[{run_index}/{total_runs}] threads={threads} size={size_kb}KB batch={batch}")

                    update_config(config_path, threads, batch)

                    ncu_report = ncu_out_dir / f"ncu_t{threads}_s{size_kb}kb_b{batch}"

                    result = run_ncu(ncu_report, executable, metrics, extra_args)

                    status         = "ok" if result.returncode == 0 else "failed"
                    metric_values  = parse_ncu_metrics(result.stdout, metrics)

                    # Determine if kernel is memory-bound or compute-bound
                    # Compare SM throughput vs DRAM throughput
                    sm_pct   = metric_values.get(metrics[0], -1.0)
                    dram_pct = metric_values.get(metrics[3], -1.0)
                    if sm_pct >= 0 and dram_pct >= 0:
                        bound = "compute" if sm_pct > dram_pct else "memory"
                    else:
                        bound = "unknown"

                    row = [
                        run_index,
                        threads,
                        size_kb,
                        batch,
                        *[f"{metric_values[m]:.2f}" for m in metrics],
                        str(ncu_report) + ".ncu-rep",
                        status,
                        bound
                    ]
                    writer.writerow(row)
                    csv_file.flush()

                    if result.returncode != 0:
                        print(f"  WARNING: run failed (exit code {result.returncode})")
                        print(f"  stderr: {result.stderr[:200]}")

                    print(f"  SM={sm_pct:.1f}% DRAM={dram_pct:.1f}% bound={bound} status={status}")
                    print()

    print(f"Sweep complete. Results saved to: {csv_path}")


# --------------------------------------------------------------------------------
if __name__ == "__main__":
    main()