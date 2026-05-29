# train.py
# Training loop for the SignalForge LSTM autoencoder.
# Reads normal (clean + noisy) FFT data from Redis or a WAV directory,
# trains the model with early stopping, computes the anomaly threshold
# on the clean-only validation subset, and saves the model checkpoint
# and threshold to disk.
#
# Usage:
#   python train.py                        # read from Redis (default)
#   python train.py --data path/to/wavs   # read WAV files from directory
#
# Threshold strategy:
#   Training uses clean + noisy signals as normal.
#   The anomaly threshold is computed only on the clean validation subset,
#   giving a tighter distribution and a lower, more sensitive threshold.
#   Filenames containing "clean" are used to identify clean samples.
#
# Early stopping:
#   Training stops when validation loss has not improved for
#   EARLY_STOPPING_PATIENCE consecutive epochs. The best checkpoint
#   is always restored before threshold computation.

import argparse
import sys

import torch
import torch.nn as nn
from torch.utils.data import DataLoader

import config
from dataset import connect_redis, load_all_fft_keys, load_from_directory, FFTDataset
from model import build_model


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Train the SignalForge LSTM autoencoder.")
    parser.add_argument(
        "--data",
        type=str,
        default=None,
        metavar="DIR",
        help="Path to directory of normal .wav files. If omitted, reads from Redis.",
    )
    return parser.parse_args()


def resolve_device() -> torch.device:
    if config.DEVICE == "cuda" and torch.cuda.is_available():
        device = torch.device("cuda")
        print(f"[train] Using GPU: {torch.cuda.get_device_name(0)}")
    else:
        device = torch.device("cpu")
        print("[train] Using CPU.")
    return device


def load_raw_arrays(data_dir: str | None) -> tuple[list[str], list]:
    """Load keys and feature arrays from Redis or directory."""
    if data_dir is not None:
        print(f"[train] Loading data from directory: {data_dir}")
        return load_from_directory(data_dir)
    else:
        print("[train] Loading data from Redis.")
        r = connect_redis()
        return load_all_fft_keys(r, delete_after=False)


def split_keys_and_arrays(
    keys: list[str],
    arrays: list,
) -> tuple[list[str], list, list[str], list]:
    """Split keys and arrays into train and validation sets."""
    total      = len(keys)
    val_size   = max(1, int(total * config.VALIDATION_SPLIT))
    train_size = total - val_size

    train_keys   = keys[:train_size]
    train_arrays = arrays[:train_size]
    val_keys     = keys[train_size:]
    val_arrays   = arrays[train_size:]

    print(f"[train] Dataset split - train: {train_size}, val: {val_size}")
    return train_keys, train_arrays, val_keys, val_arrays


def filter_clean(
    keys: list[str],
    arrays: list,
) -> tuple[list[str], list]:
    """Filter to only samples whose filename contains 'clean'."""
    clean_keys   = [k for k in keys if "clean" in k.lower()]
    clean_arrays = [a for k, a in zip(keys, arrays) if "clean" in k.lower()]
    return clean_keys, clean_arrays


def train_one_epoch(
    model: nn.Module,
    loader: DataLoader,
    optimizer: torch.optim.Optimizer,
    criterion: nn.Module,
    device: torch.device,
) -> float:
    model.train()
    total_loss = 0.0

    for batch in loader:
        batch = batch.to(device)
        optimizer.zero_grad()
        reconstruction = model(batch)
        loss = criterion(reconstruction, batch)
        loss.backward()
        optimizer.step()
        total_loss += loss.item() * len(batch)

    return total_loss / len(loader.dataset)


def evaluate(
    model: nn.Module,
    loader: DataLoader,
    criterion: nn.Module,
    device: torch.device,
) -> float:
    model.eval()
    total_loss = 0.0

    with torch.no_grad():
        for batch in loader:
            batch = batch.to(device)
            reconstruction = model(batch)
            loss = criterion(reconstruction, batch)
            total_loss += loss.item() * len(batch)

    return total_loss / len(loader.dataset)


def compute_threshold(
    model: nn.Module,
    loader: DataLoader,
    device: torch.device,
) -> float:
    """
    Compute anomaly threshold from clean validation set reconstruction errors.
    Threshold = mean + THRESHOLD_SIGMA * std of per-sample MSE errors.
    """
    model.eval()
    all_errors = []

    with torch.no_grad():
        for batch in loader:
            batch  = batch.to(device)
            errors = model.reconstruction_error(batch)
            all_errors.append(errors.cpu())

    all_errors = torch.cat(all_errors)
    mean      = all_errors.mean().item()
    std       = all_errors.std().item()
    threshold = mean + config.THRESHOLD_SIGMA * std

    print(f"[train] Clean validation reconstruction error - mean: {mean:.6f}, std: {std:.6f}")
    print(f"[train] Anomaly threshold ({config.THRESHOLD_SIGMA} sigma): {threshold:.6f}")
    return threshold


def save_threshold(threshold: float) -> None:
    with open(config.THRESHOLD_SAVE_PATH, "w") as f:
        f.write(str(threshold))
    print(f"[train] Threshold saved to {config.THRESHOLD_SAVE_PATH}")


def main() -> None:
    args   = parse_args()
    device = resolve_device()
    model  = build_model(device)

    keys, arrays = load_raw_arrays(args.data)

    if not keys:
        print(
            "[train] ERROR: No data found. "
            "Provide a directory with --data or populate Redis first."
        )
        sys.exit(1)

    train_keys, train_arrays, val_keys, val_arrays = split_keys_and_arrays(keys, arrays)

    # Filter validation set to clean signals only for threshold computation
    clean_val_keys, clean_val_arrays = filter_clean(val_keys, val_arrays)

    if not clean_val_arrays:
        print(
            "[train] WARNING: No clean samples found in validation set. "
            "Falling back to full validation set for threshold computation. "
            "Check that filenames contain 'clean' for clean signal files."
        )
        clean_val_arrays = val_arrays

    print(f"[train] Clean validation samples for threshold: {len(clean_val_arrays)}")

    train_set     = FFTDataset(train_arrays)
    val_set       = FFTDataset(val_arrays)
    clean_val_set = FFTDataset(clean_val_arrays)

    train_loader     = DataLoader(train_set,     batch_size=config.BATCH_SIZE, shuffle=True)
    val_loader       = DataLoader(val_set,       batch_size=config.BATCH_SIZE, shuffle=False)
    clean_val_loader = DataLoader(clean_val_set, batch_size=config.BATCH_SIZE, shuffle=False)

    optimizer = torch.optim.Adam(model.parameters(), lr=config.LEARNING_RATE)
    criterion = nn.MSELoss()

    best_val_loss    = float("inf")
    epochs_no_improve = 0

    for epoch in range(1, config.EPOCHS + 1):
        train_loss = train_one_epoch(model, train_loader, optimizer, criterion, device)
        val_loss   = evaluate(model, val_loader, criterion, device)

        print(f"[train] Epoch {epoch:3d}/{config.EPOCHS} - "
              f"train loss: {train_loss:.6f}, val loss: {val_loss:.6f}")

        if val_loss < best_val_loss:
            best_val_loss    = val_loss
            epochs_no_improve = 0
            torch.save(model.state_dict(), config.MODEL_SAVE_PATH)
            print(f"[train] Checkpoint saved (val loss: {best_val_loss:.6f})")
        else:
            epochs_no_improve += 1
            if epochs_no_improve >= config.EARLY_STOPPING_PATIENCE:
                print(f"[train] Early stopping at epoch {epoch} "
                      f"(no improvement for {config.EARLY_STOPPING_PATIENCE} epochs).")
                break

    print("[train] Training complete.")
    print(f"[train] Best validation loss: {best_val_loss:.6f}")

    # Load best checkpoint before computing threshold
    model.load_state_dict(
        torch.load(config.MODEL_SAVE_PATH, map_location=device, weights_only=True)
    )

    threshold = compute_threshold(model, clean_val_loader, device)
    save_threshold(threshold)


if __name__ == "__main__":
    main()