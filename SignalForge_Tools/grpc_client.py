import argparse
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
    generate_anomaly_signal,
    signal_to_pcm16,
    compute_num_samples,
)

import SignalForge_pb2
import SignalForge_pb2_grpc

# --------------------------------------------------------------------------------
SCRIPT_NAME    = "grpc_client.py"
SCRIPT_DESC    = "SignalForge gRPC client: generates synthetic WAV files in memory and streams them to a running SignalForge server"
DEFAULT_PARAMS = "grpc_client.json"

PARAMS_TEMPLATE = {
    "server": {
        "host": "gRPC server hostname or IP  [CRITICAL]",
        "port": "gRPC server port (default: 50051)  [CRITICAL]",
    },
    "signal": {
        "sample_rate": "Audio sample rate in Hz (default: 44100)",
        "size_kb":     "Size of each generated file in KB  [CRITICAL]",
    },
    "batch": {
        "clean":   "Number of clean engine signal files to send",
        "anomaly": "Number of anomaly engine signal files to send",
    }
}

PARAMS_EXAMPLE = {
    "server": {
        "host": "localhost",
        "port": 50051
    },
    "signal": {
        "sample_rate": 44100,
        "size_kb":     128
    },
    "batch": {
        "clean":   4,
        "anomaly": 3
    }
}


# --------------------------------------------------------------------------------
def print_list_params():
    print(f"\n{SCRIPT_NAME} -- parameter reference")
    print(f"Default params file: {DEFAULT_PARAMS} (same directory as script)")
    print(f"Override with:       python {SCRIPT_NAME} --params /path/to/custom.json\n")

    print("Parameters:")
    print(f"  server.host     {PARAMS_TEMPLATE['server']['host']}")
    print(f"  server.port     {PARAMS_TEMPLATE['server']['port']}")
    print(f"  signal.size_kb  {PARAMS_TEMPLATE['signal']['size_kb']}")
    print(f"  signal.sample_rate  {PARAMS_TEMPLATE['signal']['sample_rate']}")
    print(f"  batch.clean     {PARAMS_TEMPLATE['batch']['clean']}")
    print(f"  batch.anomaly   {PARAMS_TEMPLATE['batch']['anomaly']}")

    print("\nExample grpc_client.json:")
    print(json.dumps(PARAMS_EXAMPLE, indent=4))


# --------------------------------------------------------------------------------
def load_params(params_path: Path) -> dict:
    with open(params_path, "r") as f:
        return json.load(f)


# --------------------------------------------------------------------------------
def signal_to_wav_bytes(pcm_bytes: bytes, sample_rate: int) -> bytes:
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(pcm_bytes)
    return buf.getvalue()


# --------------------------------------------------------------------------------
def send_file(stub, wav_bytes: bytes, index: int, total: int, signal_type: str) -> bool:
    request      = SignalForge_pb2.FileRequest()
    request.data = wav_bytes
    response     = stub.SendFile(request)
    status       = "ok" if response.accepted else f"rejected: {response.message}"
    print(f"  [{index}/{total}] {signal_type} ({len(wav_bytes) / 1024:.1f} KB) -> {status}")
    return response.accepted


# --------------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(
        prog=SCRIPT_NAME,
        description=SCRIPT_DESC,
        formatter_class=argparse.RawTextHelpFormatter,
        epilog=(
            "Critical params to check in grpc_client.json before running:\n"
            "  server.host    -- address of the running SignalForge server\n"
            "  server.port    -- gRPC port (default 50051)\n"
            "  signal.size_kb -- file size must match what the pipeline expects\n\n"
            "Run 'python grpc_client.py --list-params' for full parameter reference.\n"
            "Note: SignalForge server must be running in --grpc mode before starting this client."
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
    host        = params["server"]["host"]
    port        = params["server"]["port"]
    sample_rate = params["signal"]["sample_rate"]
    size_kb     = params["signal"]["size_kb"]
    batch       = params["batch"]
    num_samples = compute_num_samples(size_kb, sample_rate)

    print(f"Script:      {SCRIPT_NAME}")
    print(f"Params file: {params_path}")
    print(f"Server:      {host}:{port}")
    print(f"Signal:      {size_kb} KB, {sample_rate} Hz")
    print(f"Batch:       {batch}")
    print()

    address = f"{host}:{port}"
    print(f"Connecting to {address}...")
    channel = grpc.insecure_channel(address)
    stub    = SignalForge_pb2_grpc.SignalForgeServiceStub(channel)
    print("Connected.")
    print()

    generators = {
        "clean":   generate_clean_signal,
        "anomaly": generate_anomaly_signal,
    }

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