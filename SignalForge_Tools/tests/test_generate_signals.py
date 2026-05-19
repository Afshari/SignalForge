import hashlib
import json
import os
import sys
import wave
import tempfile
import numpy as np
import pytest
from pathlib import Path

# --------------------------------------------------------------------------------
# Add parent directory to path so we can import generate_signals.py
# --------------------------------------------------------------------------------
sys.path.insert(0, str(Path(__file__).parent.parent))

from generate_signals import (
    compute_num_samples,
    generate_clean_signal,
    generate_noisy_signal,
    generate_anomaly_signal,
    signal_to_pcm16,
    write_wav,
    compute_sha256,
    load_params,
)

# --------------------------------------------------------------------------------
# Constants
# --------------------------------------------------------------------------------
SAMPLE_RATE     = 44100
ENGINE_FUND_HZ  = 80.0
ANOMALY_HZ      = 1350.0


# ================================================================================
# Unit Tests - compute_num_samples
# ================================================================================
class TestComputeNumSamples:

    def test_500kb_returns_reasonable_sample_count(self):
        num_samples = compute_num_samples(500, SAMPLE_RATE)
        # 500KB - 44 bytes header = 511956 bytes / 2 bytes per sample = 255978
        assert num_samples == 255978

    def test_1024kb_returns_reasonable_sample_count(self):
        num_samples = compute_num_samples(1024, SAMPLE_RATE)
        # 1024KB - 44 bytes header = 1048532 bytes / 2 bytes per sample = 524266
        assert num_samples == 524266

    def test_minimum_size_does_not_return_zero(self):
        num_samples = compute_num_samples(1, SAMPLE_RATE)
        assert num_samples >= 1

    def test_larger_size_returns_more_samples(self):
        small = compute_num_samples(500, SAMPLE_RATE)
        large = compute_num_samples(1024, SAMPLE_RATE)
        assert large > small


# ================================================================================
# Unit Tests - generate_clean_signal
# ================================================================================
class TestGenerateCleanSignal:

    def test_output_length_matches_num_samples(self):
        num_samples = 44100
        signal = generate_clean_signal(num_samples, SAMPLE_RATE)
        assert len(signal) == num_samples

    def test_output_is_normalized(self):
        signal = generate_clean_signal(44100, SAMPLE_RATE)
        assert np.max(np.abs(signal)) <= 1.0

    def test_output_is_not_silent(self):
        signal = generate_clean_signal(44100, SAMPLE_RATE)
        assert np.max(np.abs(signal)) > 0.1

    def test_output_dtype_is_float64(self):
        signal = generate_clean_signal(44100, SAMPLE_RATE)
        assert signal.dtype == np.float64

    def test_fundamental_frequency_present(self):
        """Verify ENGINE_FUNDAMENTAL_HZ dominates the frequency spectrum."""
        num_samples = SAMPLE_RATE  # exactly 1 second
        signal = generate_clean_signal(num_samples, SAMPLE_RATE)
        fft_magnitude = np.abs(np.fft.rfft(signal))
        freqs = np.fft.rfftfreq(num_samples, d=1.0 / SAMPLE_RATE)

        # Find the peak frequency
        peak_idx = np.argmax(fft_magnitude)
        peak_freq = freqs[peak_idx]

        # Peak should be at or near the fundamental (80 Hz)
        assert abs(peak_freq - ENGINE_FUND_HZ) < 5.0, (
            f"Expected peak near {ENGINE_FUND_HZ} Hz, got {peak_freq} Hz"
        )

    def test_deterministic_output(self):
        """Clean signal should be identical on every call (no randomness)."""
        signal_a = generate_clean_signal(44100, SAMPLE_RATE)
        signal_b = generate_clean_signal(44100, SAMPLE_RATE)
        np.testing.assert_array_equal(signal_a, signal_b)


# ================================================================================
# Unit Tests - generate_noisy_signal
# ================================================================================
class TestGenerateNoisySignal:

    def test_output_length_matches_num_samples(self):
        signal = generate_noisy_signal(44100, SAMPLE_RATE)
        assert len(signal) == 44100

    def test_output_is_normalized(self):
        signal = generate_noisy_signal(44100, SAMPLE_RATE)
        assert np.max(np.abs(signal)) <= 1.0

    def test_output_is_not_silent(self):
        signal = generate_noisy_signal(44100, SAMPLE_RATE)
        assert np.max(np.abs(signal)) > 0.1

    def test_differs_from_clean_signal(self):
        """Noisy signal must differ from clean — SHA-256 would be different."""
        clean = generate_clean_signal(44100, SAMPLE_RATE)
        noisy = generate_noisy_signal(44100, SAMPLE_RATE)
        assert not np.array_equal(clean, noisy)

    def test_two_noisy_signals_differ(self):
        """Each noisy signal should be unique due to random noise."""
        noisy_a = generate_noisy_signal(44100, SAMPLE_RATE)
        noisy_b = generate_noisy_signal(44100, SAMPLE_RATE)
        assert not np.array_equal(noisy_a, noisy_b)

    def test_fundamental_frequency_still_present(self):
        """Despite noise, engine fundamental should still be detectable."""
        num_samples = SAMPLE_RATE
        signal = generate_noisy_signal(num_samples, SAMPLE_RATE)
        fft_magnitude = np.abs(np.fft.rfft(signal))
        freqs = np.fft.rfftfreq(num_samples, d=1.0 / SAMPLE_RATE)

        # Energy around fundamental should be significant
        fund_idx = np.argmin(np.abs(freqs - ENGINE_FUND_HZ))
        fund_energy = fft_magnitude[fund_idx]

        # Fundamental energy should be above noise floor
        mean_energy = np.mean(fft_magnitude)
        assert fund_energy > mean_energy * 2.0, (
            f"Fundamental frequency energy too low: {fund_energy:.2f} vs mean {mean_energy:.2f}"
        )


# ================================================================================
# Unit Tests - generate_anomaly_signal
# ================================================================================
class TestGenerateAnomalySignal:

    def test_output_length_matches_num_samples(self):
        signal = generate_anomaly_signal(44100, SAMPLE_RATE)
        assert len(signal) == 44100

    def test_output_is_normalized(self):
        signal = generate_anomaly_signal(44100, SAMPLE_RATE)
        assert np.max(np.abs(signal)) <= 1.0

    def test_differs_from_clean_signal(self):
        clean   = generate_clean_signal(44100, SAMPLE_RATE)
        anomaly = generate_anomaly_signal(44100, SAMPLE_RATE)
        assert not np.array_equal(clean, anomaly)

    def test_anomaly_frequency_present(self):
        """Anomaly spike frequency should be detectable in the spectrum."""
        num_samples = SAMPLE_RATE
        signal = generate_anomaly_signal(num_samples, SAMPLE_RATE)
        fft_magnitude = np.abs(np.fft.rfft(signal))
        freqs = np.fft.rfftfreq(num_samples, d=1.0 / SAMPLE_RATE)

        # Energy around anomaly frequency should be significant
        anomaly_idx = np.argmin(np.abs(freqs - ANOMALY_HZ))
        anomaly_energy = fft_magnitude[anomaly_idx]

        mean_energy = np.mean(fft_magnitude)
        assert anomaly_energy > mean_energy * 2.0, (
            f"Anomaly frequency energy too low: {anomaly_energy:.2f} vs mean {mean_energy:.2f}"
        )


# ================================================================================
# Unit Tests - signal_to_pcm16
# ================================================================================
class TestSignalToPcm16:

    def test_output_length_is_double_samples(self):
        signal = np.zeros(1000, dtype=np.float64)
        pcm = signal_to_pcm16(signal)
        assert len(pcm) == 2000  # 2 bytes per sample

    def test_silence_maps_to_zero(self):
        signal = np.zeros(100, dtype=np.float64)
        pcm = signal_to_pcm16(signal)
        assert all(b == 0 for b in pcm)

    def test_positive_peak_maps_to_max_int16(self):
        signal = np.array([1.0], dtype=np.float64)
        pcm = signal_to_pcm16(signal)
        value = int.from_bytes(pcm[:2], byteorder="little", signed=True)
        assert value == 32767

    def test_negative_peak_maps_to_min_int16(self):
        signal = np.array([-1.0], dtype=np.float64)
        pcm = signal_to_pcm16(signal)
        value = int.from_bytes(pcm[:2], byteorder="little", signed=True)
        assert value == -32767

    def test_clipping_does_not_overflow(self):
        """Values outside [-1, 1] should be clipped, not overflow."""
        signal = np.array([2.0, -2.0, 1.5], dtype=np.float64)
        pcm = signal_to_pcm16(signal)
        assert len(pcm) == 6  # 3 samples x 2 bytes


# ================================================================================
# Unit Tests - load_params
# ================================================================================
class TestLoadParams:

    def test_loads_valid_json(self, tmp_path):
        params = {
            "output_dir": "../SignalForge_Tests/test_data",
            "sample_rate": 44100,
            "signals": [
                {"type": "clean", "size_kb": 500, "count": 1}
            ]
        }
        params_file = tmp_path / "tools_params.json"
        params_file.write_text(json.dumps(params))

        loaded = load_params(str(params_file))
        assert loaded["sample_rate"] == 44100
        assert loaded["signals"][0]["type"] == "clean"

    def test_missing_file_raises_error(self, tmp_path):
        missing = tmp_path / "nonexistent.json"
        with pytest.raises(FileNotFoundError):
            load_params(str(missing))


# ================================================================================
# Integration Test - write_wav + read back + hash
# ================================================================================
class TestIntegration:

    def test_write_and_read_wav_file(self, tmp_path):
        """
        Full pipeline integration test:
        Generate signal → convert to PCM → write WAV → read back → verify.
        """
        num_samples = compute_num_samples(500, SAMPLE_RATE)
        signal      = generate_clean_signal(num_samples, SAMPLE_RATE)
        pcm_bytes   = signal_to_pcm16(signal)
        output_path = tmp_path / "engine_clean_500kb_001.wav"

        write_wav(output_path, pcm_bytes, SAMPLE_RATE)

        # Verify file exists
        assert output_path.exists()

        # Verify file size is approximately 500 KB
        file_size_kb = output_path.stat().st_size / 1024
        assert 490 <= file_size_kb <= 510, (
            f"Expected ~500 KB, got {file_size_kb:.1f} KB"
        )

        # Read back and verify WAV properties
        with wave.open(str(output_path), "rb") as wav_file:
            assert wav_file.getnchannels() == 1       # mono
            assert wav_file.getsampwidth() == 2       # 16-bit
            assert wav_file.getframerate() == SAMPLE_RATE

        # Verify SHA-256 of raw PCM is consistent
        with open(output_path, "rb") as f:
            f.read(44)  # skip WAV header
            raw_pcm = f.read()

        hash_a = hashlib.sha256(raw_pcm).hexdigest()

        # Write same signal again — clean signal is deterministic
        output_path_b = tmp_path / "engine_clean_500kb_002.wav"
        write_wav(output_path_b, pcm_bytes, SAMPLE_RATE)

        with open(output_path_b, "rb") as f:
            f.read(44)
            raw_pcm_b = f.read()

        hash_b = hashlib.sha256(raw_pcm_b).hexdigest()

        # Same signal → same hash
        assert hash_a == hash_b

    def test_noisy_signals_produce_different_hashes(self, tmp_path):
        """
        Two noisy signals must have different SHA-256 hashes.
        This is the core assumption of Stage 1 (exact duplicate detection).
        """
        num_samples = compute_num_samples(500, SAMPLE_RATE)

        signal_a  = generate_noisy_signal(num_samples, SAMPLE_RATE)
        pcm_a     = signal_to_pcm16(signal_a)
        path_a    = tmp_path / "engine_noisy_500kb_001.wav"
        write_wav(path_a, pcm_a, SAMPLE_RATE)

        signal_b  = generate_noisy_signal(num_samples, SAMPLE_RATE)
        pcm_b     = signal_to_pcm16(signal_b)
        path_b    = tmp_path / "engine_noisy_500kb_002.wav"
        write_wav(path_b, pcm_b, SAMPLE_RATE)

        with open(path_a, "rb") as f:
            f.read(44)
            hash_a = hashlib.sha256(f.read()).hexdigest()

        with open(path_b, "rb") as f:
            f.read(44)
            hash_b = hashlib.sha256(f.read()).hexdigest()

        assert hash_a != hash_b, "Two noisy signals should never produce the same hash"