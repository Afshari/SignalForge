import argparse
import json
from pathlib import Path

# SignalForge_Tools - convert_hashes.py
# Converts test_data_hashes.json to C++ EXPECT_EQ lines for unit tests
# Usage: python convert_hashes.py --input test_data_hashes.json


def hex_to_uint64(hex_str: str) -> list:
    """Convert 64-char hex string to 4 x uint64_t values."""
    if len(hex_str) != 64:
        raise ValueError(f"Expected 64 hex chars, got {len(hex_str)}: {hex_str}")
    return [
        int(hex_str[i*16:(i+1)*16], 16)
        for i in range(4)
    ]


def format_expect_eq(values: list) -> str:
    lines = []
    for i, v in enumerate(values):
        lines.append(f"    EXPECT_EQ(h_hash[{i}], 0x{v:016X}ULL);")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Convert test_data_hashes.json to C++ EXPECT_EQ lines"
    )
    parser.add_argument(
        "--input",
        default="test_data_hashes.json",
        help="Path to test_data_hashes.json (default: test_data_hashes.json)"
    )
    parser.add_argument(
        "--filter",
        default="",
        help="Filter filenames containing this string (e.g. 'noisy', '500kb')"
    )
    args = parser.parse_args()

    input_path = Path(args.input)
    if not input_path.exists():
        print(f"ERROR: file not found: {input_path}")
        return

    with open(input_path, "r") as f:
        hashes = json.load(f)

    print(f"// Generated from {input_path.name}")
    print()

    for filename, hex_hash in hashes.items():
        if args.filter and args.filter not in filename:
            continue

        try:
            values = hex_to_uint64(hex_hash)
        except ValueError as e:
            print(f"// ERROR: {filename}: {e}")
            continue

        print(f"// {filename}")
        print(f"// SHA-256: {hex_hash}")
        print(format_expect_eq(values))
        print()


if __name__ == "__main__":
    main()