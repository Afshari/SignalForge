import argparse
import hashlib
import json
import numpy as np
import wave
from pathlib import Path

# SignalForge_Tools - Synthetic engine sound generator
# Generates .wav files with clean, noisy, or anomaly engine signals
# Each worker: generate signal -> write WAV -> compute SHA-256

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
ANOMALY_FREQUENCY_HZ          = 1350.0
ANOMALY_AMPLITUDE              = 2.0
ANOMALY_SUBHARMONIC_HZ        = 40.0
ANOMALY_SUBHARMONIC_AMPLITUDE = 0.5

SCRIPT_NAME    = "generate_signals.py"
SCRIPT_DESC    = "Synthetic engine sound generator: generates clean, noisy, and anomaly .wav files for SignalForge benchmarking"
DEFAULT_PARAMS = "generate_signals.json"

PARAMS_TEMPLATE = {
    "output_dir":  "Path where generated .wav files are written  [CRITICAL]",
    "sample_rate": "Audio sample rate in Hz (default: 44100)",
    "signals": [
        {
            "type":    "Signal type: 'clean', 'noisy', or 'anomaly'",
            "size_kb": "Target file size in KB",
            "count":   "Number of files to generate",
            "subdir":  "Optional subdirectory under output_dir (e.g. '500kb')",
        }
    ]
}

PARAMS_EXAMPLE = {
    "output_dir":  "../SignalForge_Tests/test_data",
    "sample_rate": 44100,
    "signals": [
        {"type": "clean",   "size_kb": 500,  "count": 2, "subdir": "500kb"},
        {"type": "clean",   "size_kb": 1024, "count": 2, "subdir": "1024kb"},
        {"type": "noisy",   "size_kb": 500,  "count": 2, "subdir": "500kb"},
        {"type": "noisy",   "size_kb": 1024, "count": 2, "subdir": "1024kb"},
        {"type": "anomaly", "size_kb": 500,  "count": 2, "subdir": "500kb"},
        {"type": "anomaly", "size_kb": 1024, "count": 2, "subdir": "1024kb"},
    ]
}


# --------------------------------------------------------------------------------
def print_list_params():
    print(f"\n{SCRIPT_NAME} -- parameter reference")
    print(f"Default params file: {DEFAULT_PARAMS} (same directory as script)")
    print(f"Override with:       python {SCRIPT_NAME} --params /path/to/custom.json\n")

    print("Parameters:")
    print(f"  output_dir    {PARAMS_TEMPLATE['output_dir']}")
    print(f"  sample_rate   {PARAMS_TEMPLATE['sample_rate']}")
    print(f"  signals       List of signal entries, each with:")
    print(f"    type        {PARAMS_TEMPLATE['signals'][0]['type']}")
    print(f"    size_kb     {PARAMS_TEMPLATE['signals'][0]['size_kb']}")
    print(f"    count       {PARAMS_TEMPLATE['signals'][0]['count']}")
    print(f"    subdir      {PARAMS_TEMPLATE['signals'][0]['subdir']}")

    print("\nExample generate_signals.json:")
    print(json.dumps(PARAMS_EXAMPLE, indent=4))


# --------------------------------------------------------------------------------
def load_params(params_path: Path) -> dict:
    with open(params_path, "r") as f:
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
    signal += ANOMALY_SUBHARMONIC_AMPLITUDE * np.sin(2.0 * np.pi * ANOMALY_SUBHARMONIC_HZ * t)
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

        subdir  = entry.get("subdir", "")
        out_dir = output_dir / subdir if subdir else output_dir
        out_dir.mkdir(parents=True, exist_ok=True)

        print(f"Generating {count}x {signal_type} at ~{size_kb} KB -> {out_dir}")
        num_samples = compute_num_samples(size_kb, sample_rate)

        for i in range(1, count + 1):
            filename    = f"engine_{signal_type}_{size_kb}kb_{i:05d}.wav"
            output_path = out_dir / filename
            sha256      = generate_file(output_path, signal_type, num_samples, sample_rate, i, count)
            all_hashes[filename] = sha256

        print()

    save_hashes(all_hashes, output_dir)


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
    parser = argparse.ArgumentParser(
        prog=SCRIPT_NAME,
        description=SCRIPT_DESC,
        formatter_class=argparse.RawTextHelpFormatter,
        epilog=(
            "Critical params to check in generate_signals.json before running:\n"
            "  output_dir  -- path where .wav files will be written\n\n"
            "Run 'python generate_signals.py --list-params' for full parameter reference."
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
    sample_rate = params.get("sample_rate", 44100)
    output_dir  = Path(script_dir / params["output_dir"]).resolve()

    output_dir.mkdir(parents=True, exist_ok=True)
    print(f"Script:           {SCRIPT_NAME}")
    print(f"Params file:      {params_path}")
    print(f"Output directory: {output_dir}")
    print(f"Sample rate:      {sample_rate} Hz")
    print()

    if "signals" in params:
        run_tools_mode(params, output_dir, sample_rate)
    else:
        print("ERROR: unrecognized params format - expected 'signals' array.")
        print(f"Run 'python {SCRIPT_NAME} --list-params' to see expected format.")
        return

    print("Done.")


# --------------------------------------------------------------------------------
if __name__ == "__main__":
    main()