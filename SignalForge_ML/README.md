# SignalForge ML — LSTM Autoencoder for Anomaly Detection

Part of the [SignalForge](../README.md) pipeline. Reads FFT magnitude data produced by the C++ GPU pipeline and runs anomaly detection using a PyTorch LSTM autoencoder.

---

## Overview

The C++ pipeline processes engine audio files, computes FFT magnitudes on the GPU via cuFFT, and stores the results in Redis. This module picks up from there:

```
Redis (fft:* keys)
    → read FFT magnitudes
    → preprocess (frequency filter + log compression)
    → LSTM autoencoder
    → reconstruction error
    → error > threshold → anomaly detected
    → delete key from Redis
```

For local testing, WAV files can be read directly instead of Redis, bypassing the C++ pipeline entirely.

---

## Architecture

### LSTM Autoencoder

A lightweight encoder-decoder architecture:

- **Encoder** — single LSTM layer compresses the input into a 4-dimensional latent vector
- **Decoder** — single LSTM layer reconstructs the input from the latent vector
- **Anomaly score** — per-sample MSE between input and reconstruction

The model is trained on normal (clean) engine signals only. Signals that deviate from the learned normal pattern produce high reconstruction error and are flagged as anomalies.

### Preprocessing Pipeline

1. **Frequency range filter** — keep bins 0–5000 Hz (covers engine harmonics and known fault frequencies)
2. **Downsample** — keep every 32nd bin → ~232 features per sample
3. **Log-magnitude compression** — `log1p` to compress dynamic range

### Training Strategy

- Train on clean engine signals only
- Threshold = mean + 2σ of reconstruction error on the validation set
- Early stopping with patience=50 to prevent overfitting

---

## Anomaly Signal

The synthetic anomaly signal simulates two real engine fault conditions:

| Component | Frequency | Represents |
|-----------|-----------|------------|
| Spike | 1350 Hz | Engine knock |
| Subharmonic | 40 Hz | Mechanical looseness |

---

## Results

Trained on 200 clean engine signals. Tested on 30 anomaly signals.

| Signal type | Reconstruction error | Result |
|-------------|----------------------|--------|
| Clean | ~0.000 | ✓ normal |
| Anomaly | ~0.62–0.68 | ✓ detected |

30/30 anomaly files correctly detected. 0 false positives.

---

## Files

| File | Description |
|------|-------------|
| `config.py` | All hyperparameters and settings |
| `dataset.py` | Redis and WAV file loading, preprocessing |
| `model.py` | LSTM autoencoder architecture |
| `train.py` | Training loop with early stopping |
| `infer.py` | Inference loop |

---

## Usage

### Train

```bash
# From WAV files (local testing)
python train.py --data path/to/clean/wavs

# From Redis (full pipeline)
python train.py
```

### Infer

```bash
# From WAV files
python infer.py --data path/to/wavs

# From Redis (reads and deletes keys after processing)
python infer.py
```

### Dependencies

```bash
pip install redis
pip install torch --index-url https://download.pytorch.org/whl/cu121
```

Other dependencies are in the root `requirements.txt`.

---

## Design Notes

- The model is intentionally lightweight — the goal is to demonstrate anomaly detection as part of a larger GPU-accelerated pipeline, not to build a production ML system
- Noisy signal deduplication is handled upstream by the FFT similarity check (planned feature), not by this module
- The `--data` flag makes the ML module independently testable without running the full C++ pipeline