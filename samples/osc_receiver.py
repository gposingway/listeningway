#!/usr/bin/env python3
"""Listeningway OSC receiver — debug helper.

Pure-stdlib OSC parser (no pip install required). Listens on UDP and
prints each received OSC message in a single line:

    /listeningway/volume        f  0.4231
    /listeningway/freqbands     f[64]  [0.10, 0.04, ..., 0.00]
    /listeningway/direction8    f[8]   [0.20, 0.10, 0.00, 0.05, 0.00, 0.10, 0.20, 0.30]

Usage:
    python samples/osc_receiver.py             # listen on 127.0.0.1:9000
    python samples/osc_receiver.py 0.0.0.0 9001
"""
from __future__ import annotations

import socket
import struct
import sys


def _read_string(buf: bytes, offset: int) -> tuple[str, int]:
    """Read a NUL-terminated string padded to 4 bytes; return (value, new offset)."""
    end = buf.index(b"\x00", offset)
    s = buf[offset:end].decode("ascii", errors="replace")
    next_off = (end + 4) & ~3  # round up past null to next 4-byte boundary
    return s, next_off


def _format_value(values: list, count_hint: int = 8) -> str:
    if len(values) == 0:
        return "(no args)"
    if len(values) == 1:
        v = values[0]
        return f"{v:+.4f}" if isinstance(v, float) else repr(v)
    head = values[:count_hint]
    formatted = ", ".join(f"{v:+.3f}" if isinstance(v, float) else repr(v) for v in head)
    if len(values) > count_hint:
        formatted += ", ..."
    return f"[{formatted}]"


def parse_osc_message(buf: bytes) -> tuple[str, str, list]:
    """Return (address, type-tag, [values]). Raises on malformed packets."""
    address, offset = _read_string(buf, 0)
    if offset >= len(buf):
        return address, "", []

    type_tag, offset = _read_string(buf, offset)
    if not type_tag.startswith(","):
        return address, type_tag, []

    values: list = []
    for tag in type_tag[1:]:
        if tag == "f":
            (v,) = struct.unpack_from(">f", buf, offset)
            offset += 4
            values.append(float(v))
        elif tag == "i":
            (v,) = struct.unpack_from(">i", buf, offset)
            offset += 4
            values.append(int(v))
        elif tag == "s":
            v, offset = _read_string(buf, offset)
            values.append(v)
        elif tag in ("T", "F", "N", "I"):
            values.append({"T": True, "F": False, "N": None, "I": float("inf")}[tag])
        else:
            # Unknown tag — bail out cleanly rather than crashing.
            values.append(f"<unhandled tag '{tag}'>")
            break
    return address, type_tag, values


def main() -> int:
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 9000

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((host, port))
    print(f"Listening for OSC on {host}:{port}  (Ctrl-C to exit)\n")

    try:
        while True:
            data, _addr = sock.recvfrom(4096)
            try:
                address, tag, values = parse_osc_message(data)
            except Exception as exc:
                print(f"  <parse error: {exc}>  raw={data!r}")
                continue
            arity = max(len(tag) - 1, 0)
            shape = f"f[{arity}]" if arity > 1 else (tag.lstrip(",") or "()")
            print(f"  {address:36s}  {shape:6s}  {_format_value(values)}")
    except KeyboardInterrupt:
        print("\nbye")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
