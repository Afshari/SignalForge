# infer.py
# Inference loop for the SignalForge LSTM autoencoder.
# Reads FFT magnitude data from Redis or a WAV directory, computes
# reconstruction error, and classifies each sample as normal or anomaly.
#
# Usage:
#   python infer.py                        # read from Redis, delete keys after
#   python infer.py --data path/to/wavs   # read WAV files, no Redis, no deletion
#
# Expected files on disk before running:
#   - signalforge_lstm_ae.pt   (produced by train.py)
#   - anomaly_threshold.txt    (produced by train.py)

import argparse
import sys

import torch
from torch.utils.data import DataLoader

import config
from dataset import connect_redis, load_all_fft_keys, load_from_directory, FFTDataset
from model import build_model


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run inference with the SignalForge LSTM autoencoder.")
    parser.add_argument(
        "--data",
        type=str,
        default=None,
        metavar="DIR",
        help="Path to directory of .wav files. If omitted, reads from Redis.",
    )
    return parser.parse_args()


def resolve_device() -> torch.device:
    if config.DEVICE == "cuda" and torch.cuda.is_available():
        device = torch.device("cuda")
        print(f"[infer] Using GPU: {torch.cuda.get_device_name(0)}")
    else:
        device = torch.device("cpu")
        print("[infer] Using CPU.")
    return device


def load_threshold() -> float:
    try:
        with open(config.THRESHOLD_SAVE_PATH, "r") as f:
            threshold = float(f.read().strip())
        print(f"[infer] Loaded anomaly threshold: {threshold:.6f}")
        return threshold
    except FileNotFoundError:
        print(
            f"[infer] ERROR: Threshold file '{config.THRESHOLD_SAVE_PATH}' not found. "
            "Run train.py first."
        )
        sys.exit(1)


def load_model(device: torch.device) -> torch.nn.Module:
    try:
        model = build_model(device)
        model.load_state_dict(
            torch.load(config.MODEL_SAVE_PATH, map_location=device, weights_only=True)
        )
        model.eval()
        print(f"[infer] Loaded model from '{config.MODEL_SAVE_PATH}'.")
        return model
    except FileNotFoundError:
        print(
            f"[infer] ERROR: Model file '{config.MODEL_SAVE_PATH}' not found. "
            "Run train.py first."
        )
        sys.exit(1)


def load_data(data_dir: str | None) -> tuple[list[str], list]:
    if data_dir is not None:
        print(f"[infer] Loading WAV files from directory: {data_dir}")
        return load_from_directory(data_dir)
    else:
        print("[infer] Loading FFT data from Redis.")
        r = connect_redis()
        return load_all_fft_keys(r, delete_after=True)


def print_summary(results: list[dict], threshold: float) -> None:
    total     = len(results)
    anomalies = sum(1 for r in results if r["anomaly"])
    normals   = total - anomalies

    print()
    print("=" * 60)
    print("  Inference summary")
    print("=" * 60)
    print(f"  Threshold               : {threshold:.6f}")
    print(f"  Total samples processed : {total}")
    print(f"  Normal                  : {normals}")
    print(f"  Anomalies detected      : {anomalies}")
    print("=" * 60)

    if anomalies > 0:
        print()
        print("  Anomalous samples:")
        for r in results:
            if r["anomaly"]:
                print(f"    {r['key']}  error={r['error']:.6f}")

    print()


def run_inference(
    model: torch.nn.Module,
    keys: list[str],
    dataset: FFTDataset,
    threshold: float,
    device: torch.device,
) -> list[dict]:
    loader  = DataLoader(dataset, batch_size=1, shuffle=False)
    results = []

    for idx, batch in enumerate(loader):
        batch      = batch.to(device)
        errors     = model.reconstruction_error(batch)
        error      = errors[0].item()
        is_anomaly = error > threshold
        label      = "ANOMALY" if is_anomaly else "normal"

        print(f"[infer] {keys[idx]}  error={error:.6f}  [{label}]")

        results.append({
            "key":     keys[idx],
            "error":   error,
            "anomaly": is_anomaly,
        })

    return results


def main() -> None:
    args      = parse_args()
    device    = resolve_device()
    threshold = load_threshold()
    model     = load_model(device)

    keys, arrays = load_data(args.data)

    if not keys:
        print("[infer] No data found. Nothing to process.")
        return

    dataset = FFTDataset(arrays)
    results = run_inference(model, keys, dataset, threshold, device)
    print_summary(results, threshold)


if __name__ == "__main__":
    main()