# config.py
# Central configuration for SignalForge LSTM autoencoder.
# All hyperparameters, Redis settings, and paths are defined here.
# Edit this file to tune the model without touching other modules.

# ---------------------------------------------------------------------------
# Redis
# ---------------------------------------------------------------------------
REDIS_HOST = "localhost"
REDIS_PORT = 6379
REDIS_DB   = 0

FFT_KEY_PATTERN = "fft:*"  # pattern used to scan for FFT magnitude keys

# ---------------------------------------------------------------------------
# FFT / preprocessing
# ---------------------------------------------------------------------------
FFT_FULL_SIZE = 32769   # number of floats stored per key (fft_size/2 + 1)
SAMPLE_RATE   = 44100   # Hz - must match the C++ pipeline

# Frequency range to keep (Hz).
# Focusing on 0-5000 Hz captures engine harmonics and the 1350 Hz anomaly
# spike while discarding irrelevant high-frequency content.
FREQ_MIN_HZ = 0
FREQ_MAX_HZ = 5000

# Bin indices corresponding to the frequency range above.
# bin = freq * fft_size / sample_rate = freq * 65536 / 44100
# FREQ_MAX_HZ=5000 -> bin 7436
_FFT_SIZE     = 65536
FREQ_BIN_MIN  = int(FREQ_MIN_HZ * _FFT_SIZE / SAMPLE_RATE)   # 0
FREQ_BIN_MAX  = int(FREQ_MAX_HZ * _FFT_SIZE / SAMPLE_RATE)   # 7436

# Downsample: keep every Nth bin within the selected frequency range.
# (7436 - 0) / 32 = ~232 bins at ~21 Hz resolution over 0-5000 Hz.
DOWNSAMPLE_FACTOR = 32
INPUT_SIZE = (FREQ_BIN_MAX - FREQ_BIN_MIN) // DOWNSAMPLE_FACTOR  # ~232

# Log-magnitude compression before normalization: np.log1p(magnitudes)
USE_LOG_MAGNITUDE = True

# ---------------------------------------------------------------------------
# Model architecture
# ---------------------------------------------------------------------------
HIDDEN_SIZE = 128   # LSTM hidden state size
LATENT_SIZE = 4   # bottleneck dimension (encoder output)
NUM_LAYERS  = 1    # number of LSTM layers in encoder and decoder

# ---------------------------------------------------------------------------
# Training
# ---------------------------------------------------------------------------
BATCH_SIZE       = 32
EPOCHS           = 500
LEARNING_RATE    = 5e-4
VALIDATION_SPLIT = 0.2   # fraction of normal data held out for validation

# Threshold: mean reconstruction error + THRESHOLD_SIGMA * std on validation set
THRESHOLD_SIGMA = 2.0

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
MODEL_SAVE_PATH     = "signalforge_lstm_ae.pt"
THRESHOLD_SAVE_PATH = "anomaly_threshold.txt"

# ---------------------------------------------------------------------------
# Device
# ---------------------------------------------------------------------------
# "cuda" if a CUDA GPU is available, otherwise "cpu".
# Override here if you want to force one or the other.
DEVICE = "cuda"  # set to "cpu" to disable GPU
# Early stopping
EARLY_STOPPING_PATIENCE = 50   # stop if val loss does not improve for this many epochs