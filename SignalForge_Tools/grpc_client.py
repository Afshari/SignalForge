import io
import json
import sys
import wave
from pathlib import Path

import grpc
import numpy as np

# reuse signal generators from generate_signals.py
sys.path.insert(0, str(Path(__file__).parent))
from generate_signals import (
    generate_clean_signal,
    generate_noisy_signal,
    generate_anomaly_signal,
    signal_to_pcm16,
    compute_num_samples,
)

import SignalForge_pb2
import SignalForge_pb2_grpc


# --------------------------------------------------------------------------------
def load_params(params_file: str) -> dict:
    with open(params_file, "r") as f:
        return json.load(f)


# --------------------------------------------------------------------------------
def signal_to_wav_bytes(pcm_bytes: bytes, sample_rate: int) -> bytes:
    # serialize WAV to in-memory bytes - no disk write needed
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(pcm_bytes)
    return buf.getvalue()


# --------------------------------------------------------------------------------
def send_file(stub, wav_bytes: bytes, index: int, total: int, signal_type: str) -> bool:
    request = SignalForge_pb2.FileRequest()
    request.data = wav_bytes

    response = stub.SendFile(request)
    status   = "ok" if response.accepted else f"rejected: {response.message}"
    print(f"  [{index}/{total}] {signal_type} ({len(wav_bytes) / 1024:.1f} KB) -> {status}")
    return response.accepted


# --------------------------------------------------------------------------------
def main():
    script_dir  = Path(__file__).parent
    params_path = script_dir / "grpc_client.json"

    if not params_path.exists():
        print(f"ERROR: params file not found: {params_path}")
        return

    params      = load_params(str(params_path))
    host        = params["server"]["host"]
    port        = params["server"]["port"]
    sample_rate = params["signal"]["sample_rate"]
    size_kb     = params["signal"]["size_kb"]
    batch       = params["batch"]

    num_samples = compute_num_samples(size_kb, sample_rate)

    address = f"{host}:{port}"
    print(f"Connecting to {address}...")

    channel = grpc.insecure_channel(address)
    stub    = SignalForge_pb2_grpc.SignalForgeServiceStub(channel)

    # register this client
    print("Connected.")
    print()

    generators = {
        "clean":   generate_clean_signal,
        "noisy":   generate_noisy_signal,
        "anomaly": generate_anomaly_signal,
    }

    # build send list
    tasks = []
    for signal_type, count in batch.items():
        for _ in range(count):
            tasks.append(signal_type)

    total = len(tasks)
    print(f"Sending {total} files ({batch})...")
    print()

    success = 0
    for i, signal_type in enumerate(tasks, start=1):
        signal    = generators[signal_type](num_samples, sample_rate)
        pcm_bytes = signal_to_pcm16(signal)
        wav_bytes = signal_to_wav_bytes(pcm_bytes, sample_rate)
        if send_file(stub, wav_bytes, i, total, signal_type):
            success += 1

    print()
    print(f"Sent {success}/{total} files successfully.")

    print("Done.")


# --------------------------------------------------------------------------------
if __name__ == "__main__":
    main()