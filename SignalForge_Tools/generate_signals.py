import argparse
import hashlib
import json
import numpy as np
import wave
from pathlib import Path

# SignalForge_Tools - Synthetic engine sound generator
# Supports tools_params.json (small batches) and profiling_params.json (large batches)

# Engine fundamental frequency (Hz) - typical car engine at idle
ENGINE_FUNDAMENTAL_HZ = 80.0

# Harmonics multipliers and their relative amplitudes
HARMONICS = [
    (1.0, 1.00),   # 80 Hz  - fundamental
    (2.0, 0.60),   # 160 Hz
    (3.0, 0.40),   # 240 Hz
    (4.0, 0.25),   # 320 Hz
    (5.0, 0.15),   # 400 Hz
]

# Anomaly frequency spike - simulates engine knock
ANOMALY_FREQUENCY_HZ = 1350.0
ANOMALY_AMPLITUDE    = 0.35


# --------------------------------------------------------------------------------
def load_params(params_file: str) -> dict:
    with open(params_file, "r") as f:
        return json.load(f)
 
 
# --------------------------------------------------------------------------------
def compute_num_samples(size_kb: int, sample_rate: int) -> int:
    target_bytes = size_kb * 1024 - 44
    num_samples  = target_bytes // 2
    return max(num_samples, 1)
 
 
# --------------------------------------------------------------------------------
def generate_clean_signal(num_samples: int, sample_rate: int) -> np.ndarray:
    t      = np.linspace(0, num_samples / sample_rate, num_samples, endpoint=False)
    signal = np.zeros(num_samples, dtype=np.float64)
    for multiplier, amplitude in HARMONICS:
        signal += amplitude * np.sin(2.0 * np.pi * ENGINE_FUNDAMENTAL_HZ * multiplier * t)
    max_val = np.max(np.abs(signal))
    if max_val > 0:
        signal /= max_val
    return signal
 
 
# --------------------------------------------------------------------------------
def generate_noisy_signal(num_samples: int, sample_rate: int) -> np.ndarray:
    signal  = generate_clean_signal(num_samples, sample_rate)
    signal += np.random.normal(0, 0.08, num_samples)
    max_val = np.max(np.abs(signal))
    if max_val > 0:
        signal /= max_val
    return signal
 
 
# --------------------------------------------------------------------------------
def generate_anomaly_signal(num_samples: int, sample_rate: int) -> np.ndarray:
    signal  = generate_clean_signal(num_samples, sample_rate)
    t       = np.linspace(0, num_samples / sample_rate, num_samples, endpoint=False)
    signal += ANOMALY_AMPLITUDE * np.sin(2.0 * np.pi * ANOMALY_FREQUENCY_HZ * t)
    signal += np.random.normal(0, 0.03, num_samples)
    max_val = np.max(np.abs(signal))
    if max_val > 0:
        signal /= max_val
    return signal
 
 
GENERATORS = {
    "clean":   generate_clean_signal,
    "noisy":   generate_noisy_signal,
    "anomaly": generate_anomaly_signal,
}
 
 
# --------------------------------------------------------------------------------
def signal_to_pcm16(signal: np.ndarray) -> bytes:
    clipped = np.clip(signal, -1.0, 1.0)
    return (clipped * 32767).astype(np.int16).tobytes()
 
 
# --------------------------------------------------------------------------------
def write_wav(output_path: Path, pcm_bytes: bytes, sample_rate: int) -> None:
    with wave.open(str(output_path), "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(pcm_bytes)
 
 
# --------------------------------------------------------------------------------
def compute_sha256(output_path: Path) -> str:
    with open(output_path, "rb") as f:
        f.read(44)
        raw_pcm = f.read()
    return hashlib.sha256(raw_pcm).hexdigest()
 
 
# --------------------------------------------------------------------------------
def generate_file(
    output_path: Path,
    signal_type: str,
    num_samples: int,
    sample_rate: int,
    index: int,
    total: int
) -> str:
    signal    = GENERATORS[signal_type](num_samples, sample_rate)
    pcm_bytes = signal_to_pcm16(signal)
    write_wav(output_path, pcm_bytes, sample_rate)
    sha256    = compute_sha256(output_path)
 
    # Progress every 100 files for large batches
    if index == 1 or index % 100 == 0 or index == total:
        size_kb = output_path.stat().st_size / 1024
        print(f"  [{index}/{total}] {output_path.name} ({size_kb:.1f} KB) SHA-256: {sha256}")
 
    return sha256
 
 
# --------------------------------------------------------------------------------
def run_tools_mode(params: dict, output_dir: Path, sample_rate: int) -> None:
    all_hashes = {}
 
    for entry in params["signals"]:
        signal_type = entry["type"]
        size_kb     = entry["size_kb"]
        count       = entry["count"]
 
        if signal_type not in GENERATORS:
            print(f"WARNING: Unknown signal type '{signal_type}', skipping.")
            continue
 
        print(f"Generating {count}x {signal_type} at ~{size_kb} KB:")
        num_samples = compute_num_samples(size_kb, sample_rate)
 
        for i in range(1, count + 1):
            filename    = f"engine_{signal_type}_{size_kb}kb_{i:05d}.wav"
            output_path = output_dir / filename
            sha256      = generate_file(output_path, signal_type, num_samples, sample_rate, i, count)
            all_hashes[filename] = sha256
 
        print()
 
    save_hashes(all_hashes, output_dir)
 
 
# --------------------------------------------------------------------------------
def run_profiling_mode(params: dict, output_dir: Path, sample_rate: int) -> None:
    signal_types_cfg = params["signal_types"]
    file_sizes_kb    = params["profiling"]["file_sizes_kb"]
    all_hashes       = {}
 
    for signal_type, type_cfg in signal_types_cfg.items():
        if signal_type not in GENERATORS:
            print(f"WARNING: Unknown signal type '{signal_type}', skipping.")
            continue
 
        for size_kb in file_sizes_kb:
            if "count_per_size" in type_cfg:
                count = type_cfg["count_per_size"]
            elif "count_per_size_map" in type_cfg:
                count = type_cfg["count_per_size_map"].get(str(size_kb), 2)
            else:
                count = 2
 
            print(f"Generating {count}x {signal_type} at ~{size_kb} KB:")
            num_samples = compute_num_samples(size_kb, sample_rate)
 
            for i in range(1, count + 1):
                filename    = f"engine_{signal_type}_{size_kb}kb_{i:05d}.wav"
                output_path = output_dir / filename
 
                # Skip existing files — allows resuming interrupted runs
                if output_path.exists():
                    if i == 1:
                        print(f"  Skipping existing files...")
                    continue
 
                sha256 = generate_file(output_path, signal_type, num_samples, sample_rate, i, count)
                all_hashes[filename] = sha256
 
            print()
 
    if all_hashes:
        save_hashes(all_hashes, output_dir)
    else:
        print("All files already exist, nothing generated.")
 
 
# --------------------------------------------------------------------------------
def save_hashes(all_hashes: dict, output_dir: Path) -> None:
    hashes_path = output_dir / "test_data_hashes.json"
 
    existing = {}
    if hashes_path.exists():
        with open(hashes_path, "r") as f:
            existing = json.load(f)
    existing.update(all_hashes)
 
    with open(hashes_path, "w") as f:
        json.dump(existing, f, indent=4)
    print(f"Hashes saved to: {hashes_path}")
 
 
# --------------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="SignalForge signal generator")
    parser.add_argument(
        "--params",
        default="tools_params.json",
        help="Path to params JSON file (default: tools_params.json)"
    )
    args = parser.parse_args()
 
    script_dir  = Path(__file__).parent
    params_path = Path(args.params)
    if not params_path.is_absolute():
        params_path = script_dir / params_path
 
    if not params_path.exists():
        print(f"ERROR: params file not found: {params_path}")
        return
 
    params      = load_params(str(params_path))
    sample_rate = params.get("sample_rate", 44100)
    output_dir  = Path(script_dir / params["output_dir"]).resolve()
 
    output_dir.mkdir(parents=True, exist_ok=True)
    print(f"Params:           {params_path.name}")
    print(f"Output directory: {output_dir}")
    print(f"Sample rate:      {sample_rate} Hz")
    print()
 
    if "signals" in params:
        run_tools_mode(params, output_dir, sample_rate)
    elif "signal_types" in params:
        run_profiling_mode(params, output_dir, sample_rate)
    else:
        print("ERROR: unrecognized params format.")
        return
 
    print("Done.")
 
 
# --------------------------------------------------------------------------------
if __name__ == "__main__":
    main()
