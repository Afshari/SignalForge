import hashlib
import json
import numpy as np
import wave
from pathlib import Path

# SignalForge_Tools - Synthetic engine sound generator
# Reads parameters from tools_params.json and generates .wav test files

PARAMS_FILE = "tools_params.json"

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
    """
    Compute number of samples to produce approximately size_kb kilobytes.
    WAV PCM 16-bit = 2 bytes per sample.
    We subtract 44 bytes for the WAV header.
    """
    target_bytes = size_kb * 1024 - 44
    num_samples = target_bytes // 2  # 16-bit = 2 bytes per sample
    return max(num_samples, 1)


# --------------------------------------------------------------------------------
def generate_clean_signal(num_samples: int, sample_rate: int) -> np.ndarray:
    """
    Clean engine signal: fundamental frequency + harmonics.
    No noise added.
    """
    t = np.linspace(0, num_samples / sample_rate, num_samples, endpoint=False)
    signal = np.zeros(num_samples, dtype=np.float64)

    for multiplier, amplitude in HARMONICS:
        freq = ENGINE_FUNDAMENTAL_HZ * multiplier
        signal += amplitude * np.sin(2.0 * np.pi * freq * t)

    # Normalize to [-1, 1]
    max_val = np.max(np.abs(signal))
    if max_val > 0:
        signal /= max_val

    return signal


# --------------------------------------------------------------------------------
def generate_noisy_signal(num_samples: int, sample_rate: int) -> np.ndarray:
    """
    Noisy engine signal: same harmonics as clean + Gaussian noise.
    Similar to clean signal but SHA-256 will be completely different.
    Simulates real-world sensor noise.
    """
    signal = generate_clean_signal(num_samples, sample_rate)

    noise = np.random.normal(0, 0.08, num_samples)
    signal += noise

    # Normalize to [-1, 1]
    max_val = np.max(np.abs(signal))
    if max_val > 0:
        signal /= max_val

    return signal


# --------------------------------------------------------------------------------
def generate_anomaly_signal(num_samples: int, sample_rate: int) -> np.ndarray:
    """
    Anomalous engine signal: same harmonics as clean + unexpected frequency spike.
    Simulates engine knock or mechanical fault.
    This is what the LSTM should eventually detect.
    """
    signal = generate_clean_signal(num_samples, sample_rate)

    t = np.linspace(0, num_samples / sample_rate, num_samples, endpoint=False)
    anomaly = ANOMALY_AMPLITUDE * np.sin(2.0 * np.pi * ANOMALY_FREQUENCY_HZ * t)
    signal += anomaly

    # Add small noise as well
    noise = np.random.normal(0, 0.03, num_samples)
    signal += noise

    # Normalize to [-1, 1]
    max_val = np.max(np.abs(signal))
    if max_val > 0:
        signal /= max_val

    return signal


# --------------------------------------------------------------------------------
def signal_to_pcm16(signal: np.ndarray) -> bytes:
    """
    Convert float64 signal [-1, 1] to 16-bit PCM bytes.
    """
    clipped = np.clip(signal, -1.0, 1.0)
    pcm = (clipped * 32767).astype(np.int16)
    return pcm.tobytes()


# --------------------------------------------------------------------------------
def write_wav(output_path: Path, pcm_bytes: bytes, sample_rate: int) -> None:
    """
    Write raw PCM bytes to a standard WAV file.
    Always: mono, 16-bit, binary.
    """
    num_samples = len(pcm_bytes) // 2  # 16-bit = 2 bytes per sample

    with wave.open(str(output_path), "wb") as wav_file:
        wav_file.setnchannels(1)       # mono
        wav_file.setsampwidth(2)       # 16-bit
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(pcm_bytes)

    print(f"  Written: {output_path}  ({output_path.stat().st_size / 1024:.1f} KB)")


# --------------------------------------------------------------------------------
def compute_sha256(output_path: Path) -> str:
    with open(output_path, "rb") as f:
        f.read(44)  # skip WAV header
        raw_pcm = f.read()
    return hashlib.sha256(raw_pcm).hexdigest()


# --------------------------------------------------------------------------------
def main():
    # --- Load params ---
    script_dir = Path(__file__).parent
    params_path = script_dir / PARAMS_FILE

    if not params_path.exists():
        print(f"ERROR: {PARAMS_FILE} not found at {params_path}")
        return

    params = load_params(str(params_path))

    sample_rate = params.get("sample_rate", 44100)
    output_dir  = Path(script_dir / params["output_dir"]).resolve()
    signals     = params["signals"]

    # --- Create output directory ---
    output_dir.mkdir(parents=True, exist_ok=True)
    print(f"Output directory: {output_dir}")
    print(f"Sample rate: {sample_rate} Hz")
    print()

    # --- Generate signals ---
    generators = {
        "clean":   generate_clean_signal,
        "noisy":   generate_noisy_signal,
        "anomaly": generate_anomaly_signal,
    }

    all_hashes = {}

    for entry in signals:
        signal_type = entry["type"]
        size_kb     = entry["size_kb"]
        count       = entry["count"]

        if signal_type not in generators:
            print(f"WARNING: Unknown signal type '{signal_type}', skipping.")
            continue

        print(f"Generating {count}x {signal_type} signal(s) at ~{size_kb} KB:")

        generator   = generators[signal_type]
        num_samples = compute_num_samples(size_kb, sample_rate)

        for i in range(1, count + 1):
            filename    = f"engine_{signal_type}_{size_kb}kb_{i:03d}.wav"
            output_path = output_dir / filename

            signal    = generator(num_samples, sample_rate)
            pcm_bytes = signal_to_pcm16(signal)
            write_wav(output_path, pcm_bytes, sample_rate)

            sha256 = compute_sha256(output_path)
            all_hashes[filename] = sha256
            print(f"  SHA-256: {sha256}")

        print()

    # Save all hashes to JSON
    hashes_path = output_dir / "test_data_hashes.json"
    with open(hashes_path, "w") as f:
        json.dump(all_hashes, f, indent=4)
    print(f"Hashes saved to: {hashes_path}")
    print("Done.")


# --------------------------------------------------------------------------------
if __name__ == "__main__":
    main()