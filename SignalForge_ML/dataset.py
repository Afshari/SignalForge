# dataset.py
# Reads FFT magnitude data from Redis or a directory of WAV files,
# preprocesses it, and exposes it as a PyTorch Dataset for use in
# train.py and infer.py.
#
# Two data sources are supported:
#   - Redis:     load_all_fft_keys()    -- used at inference and when the
#                                          full C++ pipeline is running
#   - Directory: load_from_directory()  -- used for local testing; replicates
#                                          the C++ FFT pipeline in Python
#
# Preprocessing pipeline (must be identical at training and inference):
#   1. Frequency range filter  -- keep bins FREQ_BIN_MIN to FREQ_BIN_MAX (0-5000 Hz)
#   2. Downsample              -- keep every Nth bin
#   3. Log-magnitude           -- np.log1p to compress dynamic range

from pathlib import Path

import numpy as np
import redis
import scipy.io.wavfile as wav
import torch
from torch.utils.data import Dataset

import config

# FFT size must match the C++ pipeline (cufftExecR2C called with n=65536)
_FFT_SIZE = 65536


# ---------------------------------------------------------------------------
# Redis helpers
# ---------------------------------------------------------------------------

def connect_redis() -> redis.Redis:
    """Create and return a Redis client using settings from config."""
    return redis.Redis(
        host=config.REDIS_HOST,
        port=config.REDIS_PORT,
        db=config.REDIS_DB,
    )


def decode_fft(raw: bytes) -> np.ndarray:
    """Decode a raw Redis binary blob into a float32 numpy array."""
    return np.frombuffer(raw, dtype=np.float32).copy()


# ---------------------------------------------------------------------------
# Preprocessing
# ---------------------------------------------------------------------------

def preprocess(magnitudes: np.ndarray) -> np.ndarray:
    """
    Preprocess FFT magnitudes into a feature vector.

    Steps:
      1. Frequency range filter - keep bins FREQ_BIN_MIN to FREQ_BIN_MAX (0-5000 Hz).
         This keeps the engine harmonic range and the 1350 Hz anomaly spike
         while discarding irrelevant high-frequency content.
      2. Downsample             - keep every Nth bin.
      3. Log-magnitude          - np.log1p to compress dynamic range.

    No per-sample normalization is applied. Log1p provides sufficient dynamic
    range compression while preserving absolute magnitude differences between
    normal and anomaly signals. The 1350 Hz spike will retain its absolute
    log-magnitude value, making it detectable as an out-of-distribution feature.

    Returns a float32 array of shape (INPUT_SIZE,).
    """
    # 1. Frequency range filter - focus on 0-5000 Hz
    magnitudes = magnitudes[config.FREQ_BIN_MIN: config.FREQ_BIN_MAX]

    # 2. Downsample
    magnitudes = magnitudes[:: config.DOWNSAMPLE_FACTOR]

    # Trim to exactly INPUT_SIZE
    magnitudes = magnitudes[: config.INPUT_SIZE]
    if len(magnitudes) < config.INPUT_SIZE:
        magnitudes = np.pad(magnitudes, (0, config.INPUT_SIZE - len(magnitudes)))

    # 3. Log-magnitude compression
    if config.USE_LOG_MAGNITUDE:
        magnitudes = np.log1p(magnitudes)

    return magnitudes.astype(np.float32)


# ---------------------------------------------------------------------------
# Data loading
# ---------------------------------------------------------------------------

def load_all_fft_keys(
    r: redis.Redis,
    delete_after: bool = False,
) -> tuple[list[str], list[np.ndarray]]:
    """
    Scan Redis for all fft:* keys and load their preprocessed feature vectors.

    Args:
        r:             Redis client.
        delete_after:  If True, delete each key after reading (used in infer.py).

    Returns:
        keys:   List of Redis key strings.
        arrays: List of preprocessed float32 arrays, one per key.
    """
    raw_keys = r.keys(config.FFT_KEY_PATTERN)

    if not raw_keys:
        print("[dataset] No FFT keys found in Redis.")
        return [], []

    keys   = []
    arrays = []

    for raw_key in raw_keys:
        key_str = raw_key.decode("utf-8") if isinstance(raw_key, bytes) else raw_key
        raw     = r.get(raw_key)

        if raw is None:
            print(f"[dataset] Warning: key {key_str} returned None, skipping.")
            continue

        magnitudes = decode_fft(raw)

        if len(magnitudes) != config.FFT_FULL_SIZE:
            print(
                f"[dataset] Warning: key {key_str} has unexpected length "
                f"{len(magnitudes)} (expected {config.FFT_FULL_SIZE}), skipping."
            )
            continue

        features = preprocess(magnitudes)
        keys.append(key_str)
        arrays.append(features)

        if delete_after:
            r.delete(raw_key)
            print(f"[dataset] Processed and deleted key: {key_str}")

    print(f"[dataset] Loaded {len(arrays)} FFT entries from Redis.")
    return keys, arrays


def _wav_to_fft_magnitudes(filepath: str) -> np.ndarray | None:
    """
    Load a WAV file and compute FFT magnitudes, replicating the C++ pipeline:
      1. Read PCM int16 samples.
      2. Normalize to float32 in [-1.0, 1.0] (divide by 32768.0).
      3. Compute real FFT with n=65536 (matches cufftExecR2C in C++).
      4. Return magnitude spectrum of shape (32769,).

    Returns None and prints a warning if the file cannot be processed.
    """
    try:
        sample_rate, samples = wav.read(filepath)
    except Exception as e:
        print(f"[dataset] Warning: could not read {filepath}: {e}")
        return None

    # Handle stereo by taking the left channel only
    if samples.ndim == 2:
        samples = samples[:, 0]

    # Convert int16 PCM to float32 normalized to [-1.0, 1.0]
    if samples.dtype == np.int16:
        samples = samples.astype(np.float32) / 32768.0
    elif samples.dtype != np.float32:
        samples = samples.astype(np.float32)

    # Compute real FFT - n=65536 matches the C++ cufftExecR2C call
    # rfft output length = n/2 + 1 = 32769
    spectrum   = np.fft.rfft(samples, n=_FFT_SIZE)
    magnitudes = np.abs(spectrum).astype(np.float32)  # shape: (32769,)
    return magnitudes


def load_from_directory(directory: str) -> tuple[list[str], list[np.ndarray]]:
    """
    Load and preprocess all WAV files in a directory.
    Replicates the C++ FFT pipeline so training data matches inference data.

    Args:
        directory: Path to a directory containing .wav files.

    Returns:
        keys:   List of filenames.
        arrays: List of preprocessed float32 arrays, one per file.
    """
    wav_files = sorted(Path(directory).glob("*.wav"))

    if not wav_files:
        print(f"[dataset] No .wav files found in '{directory}'.")
        return [], []

    keys   = []
    arrays = []

    for wav_path in wav_files:
        magnitudes = _wav_to_fft_magnitudes(str(wav_path))

        if magnitudes is None:
            continue

        if len(magnitudes) != config.FFT_FULL_SIZE:
            print(
                f"[dataset] Warning: {wav_path.name} produced {len(magnitudes)} bins "
                f"(expected {config.FFT_FULL_SIZE}), skipping."
            )
            continue

        features = preprocess(magnitudes)
        keys.append(wav_path.name)
        arrays.append(features)

    print(f"[dataset] Loaded {len(arrays)} WAV files from '{directory}'.")
    return keys, arrays


# ---------------------------------------------------------------------------
# Dataset
# ---------------------------------------------------------------------------

class FFTDataset(Dataset):
    """
    PyTorch Dataset wrapping preprocessed FFT feature vectors.

    Each sample is a tensor of shape (1, INPUT_SIZE) representing a
    single timestep fed into the LSTM autoencoder.
    The shape is (seq_len=1, features=INPUT_SIZE).
    """

    def __init__(self, arrays: list[np.ndarray]):
        data = np.stack(arrays, axis=0)       # (N, INPUT_SIZE)
        data = data[:, np.newaxis, :]          # (N, 1, INPUT_SIZE)
        self.data = torch.tensor(data, dtype=torch.float32)

    def __len__(self) -> int:
        return len(self.data)

    def __getitem__(self, idx: int) -> torch.Tensor:
        return self.data[idx]