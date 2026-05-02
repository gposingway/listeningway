#!/usr/bin/env python3
"""Listeningway integration harness — runs alongside the game.

Single-file, stdlib-only validator that exercises both Listeningway
network outputs in parallel:

  * **OSC receiver** (UDP 9000) — parses incoming messages, validates
    addresses against the expected /listeningway/* tree, tracks Hz +
    range per address, flags missing or unexpected addresses.

  * **OpenRGB mock server** (TCP 6742) — accepts a connection from the
    Listeningway addon, answers the minimum subset of the OpenRGB SDK
    protocol (version handshake, controller list, controller data,
    custom-mode acks, LED frame consumption), tracks frames-per-second
    and the last colour sample. Lets you verify the OpenRGB consumer
    end-to-end *without* installing OpenRGB.

Live dashboard refreshes every 500 ms. Ctrl-C to exit.

Usage:
    python samples/integration_harness.py
    python samples/integration_harness.py --osc-port 9001 --orgb-port 6743
    python samples/integration_harness.py --osc-only
    python samples/integration_harness.py --orgb-only
"""
from __future__ import annotations

import argparse
import math
import os
import socket
import struct
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass, field

# ---------------------------------------------------------------------------
# Expected OSC schema — mirrors src/output/osc_consumer.cpp.
# ---------------------------------------------------------------------------

SCALAR_RANGES: dict[str, tuple[float, float]] = {
    "/listeningway/volume":             (0.0, 2.0),   # post-amplifier may exceed 1
    "/listeningway/volumeleft":         (0.0, 2.0),
    "/listeningway/volumeright":        (0.0, 2.0),
    "/listeningway/volume_norm":        (0.0, 5.0),
    "/listeningway/volume_att":         (0.0, 5.0),
    "/listeningway/bass_norm":          (0.0, 5.0),
    "/listeningway/mid_norm":           (0.0, 5.0),
    "/listeningway/treb_norm":          (0.0, 5.0),
    "/listeningway/bass_att":           (0.0, 5.0),
    "/listeningway/mid_att":            (0.0, 5.0),
    "/listeningway/treb_att":           (0.0, 5.0),
    "/listeningway/audiopan":           (-1.0, 1.0),
    "/listeningway/audioformat":        (0.0, 16.0),
    "/listeningway/numbands":           (0.0, 256.0),
    "/listeningway/beat":               (0.0, 1.0),
    "/listeningway/beat_phase":         (0.0, 1.0),
    "/listeningway/tempo_bpm":          (0.0, 240.0),
    "/listeningway/tempo_confidence":   (0.0, 1.0),
    "/listeningway/phase_volume":       (0.0, 1.0),
    "/listeningway/phase_bass":         (0.0, 1.0),
    "/listeningway/phase_treble":       (0.0, 1.0),
    "/listeningway/spectral_centroid":  (0.0, 1.0),
    "/listeningway/loudness":           (0.0, 1.0),
}
ARRAY_ADDRESSES: dict[str, int] = {
    "/listeningway/freqbands":   -1,   # variable length
    "/listeningway/direction8":  8,
}
OPTIONAL_ADDRESSES = {"/listeningway/test"}
ALL_EXPECTED = set(SCALAR_RANGES) | set(ARRAY_ADDRESSES) | OPTIONAL_ADDRESSES

# ---------------------------------------------------------------------------
# Tiny ANSI helpers for the live dashboard.
# ---------------------------------------------------------------------------

if os.name == "nt":
    # Enable ANSI on cmd.exe / Windows Terminal, and force UTF-8 stdout so
    # the bullet/em-dash glyphs render rather than raising UnicodeEncodeError
    # on the default cp1252 codepage.
    try:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)
    except Exception:
        pass
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass

CSI = "\x1b["
RESET = f"{CSI}0m"
BOLD = f"{CSI}1m"
DIM = f"{CSI}2m"
GREEN = f"{CSI}32m"
YELLOW = f"{CSI}33m"
RED = f"{CSI}31m"
CYAN = f"{CSI}36m"
CLEAR = f"{CSI}H{CSI}2J"


# ---------------------------------------------------------------------------
# OSC parser (no third-party deps). Big-endian; null-padded to 4-byte.
# ---------------------------------------------------------------------------

def _read_string(buf: bytes, offset: int) -> tuple[str, int]:
    end = buf.index(b"\x00", offset)
    s = buf[offset:end].decode("ascii", errors="replace")
    next_off = (end + 4) & ~3
    return s, next_off


def parse_osc_message(buf: bytes) -> tuple[str, str, list]:
    address, off = _read_string(buf, 0)
    if off >= len(buf):
        return address, "", []
    tag, off = _read_string(buf, off)
    values: list = []
    if not tag.startswith(","):
        return address, tag, values
    for ch in tag[1:]:
        if ch == "f":
            (v,) = struct.unpack_from(">f", buf, off)
            off += 4
            values.append(float(v))
        elif ch == "i":
            (v,) = struct.unpack_from(">i", buf, off)
            off += 4
            values.append(int(v))
        elif ch == "s":
            v, off = _read_string(buf, off)
            values.append(v)
        elif ch in ("T", "F", "N", "I"):
            values.append({"T": True, "F": False, "N": None, "I": math.inf}[ch])
        else:
            values.append(f"<unhandled '{ch}'>")
            break
    return address, tag, values


# ---------------------------------------------------------------------------
# OSC stats tracker.
# ---------------------------------------------------------------------------

@dataclass
class AddrStats:
    arity: int = 0          # 1 = scalar, >1 = array length, 0 = unknown
    last: list = field(default_factory=list)
    count: int = 0
    min_seen: float = math.inf
    max_seen: float = -math.inf
    out_of_range: int = 0
    nan_or_inf: int = 0
    timestamps: deque = field(default_factory=lambda: deque(maxlen=200))

    def update(self, values: list) -> None:
        self.arity = len(values)
        self.last = values
        self.count += 1
        self.timestamps.append(time.monotonic())
        for v in values:
            if not isinstance(v, (int, float)):
                continue
            f = float(v)
            if math.isnan(f) or math.isinf(f):
                self.nan_or_inf += 1
                continue
            if f < self.min_seen:
                self.min_seen = f
            if f > self.max_seen:
                self.max_seen = f

    def hz(self, now: float) -> float:
        cutoff = now - 1.0
        return float(sum(1 for t in self.timestamps if t >= cutoff))


class OscReceiver:
    def __init__(self, port: int) -> None:
        self.port = port
        self.stats: dict[str, AddrStats] = {}
        self.unexpected: dict[str, int] = {}
        self.lock = threading.Lock()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.last_packet: float = 0.0
        self.bytes_in = 0

    def start(self) -> None:
        self.thread.start()

    def _run(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(("0.0.0.0", self.port))
        sock.settimeout(1.0)
        while True:
            try:
                data, _addr = sock.recvfrom(8192)
            except socket.timeout:
                continue
            self.bytes_in += len(data)
            self.last_packet = time.monotonic()
            try:
                address, _tag, values = parse_osc_message(data)
            except Exception:
                continue
            with self.lock:
                if address in ALL_EXPECTED:
                    s = self.stats.setdefault(address, AddrStats())
                    s.update(values)
                else:
                    self.unexpected[address] = self.unexpected.get(address, 0) + 1


# ---------------------------------------------------------------------------
# OpenRGB mock server. Implements only the subset cppSDK uses.
# ---------------------------------------------------------------------------

# MessageType values from the protocol description.
MSG_REQUEST_CONTROLLER_COUNT  = 0
MSG_REQUEST_CONTROLLER_DATA   = 1
MSG_REQUEST_PROTOCOL_VERSION  = 40
MSG_SET_CLIENT_NAME           = 50
MSG_DEVICE_LIST_UPDATED       = 100
MSG_RGBCTRL_RESIZEZONE        = 1000
MSG_RGBCTRL_UPDATELEDS        = 1050
MSG_RGBCTRL_UPDATEZONELEDS    = 1051
MSG_RGBCTRL_UPDATESINGLELED   = 1052
MSG_RGBCTRL_SETCUSTOMMODE     = 1100
MSG_RGBCTRL_UPDATEMODE        = 1101
MSG_RGBCTRL_SAVEMODE          = 1102

PROTOCOL_VERSION_REPLY = 4   # we'll claim to support v4, which is what cppSDK is built against
MOCK_DEVICE_NAME = "Listeningway Mock Device"
MOCK_LED_COUNT   = 12


def _str_field(s: str) -> bytes:
    """OpenRGB strings come from server as length-prefixed (uint16) + bytes
    + null terminator counted in the length."""
    encoded = s.encode("utf-8") + b"\x00"
    return struct.pack("<H", len(encoded)) + encoded


def _build_mock_device() -> bytes:
    """Serialise a single fake LedStrip device with 1 zone, MOCK_LED_COUNT LEDs,
    and a single 'Direct' mode (PerLed). Mirrors DeviceDescription layout from
    third_party/Youda008-OpenRGB-cppSDK/protocol_description.txt."""
    out = bytearray()
    # type = LedStrip
    out += struct.pack("<I", 4)
    # identification strings
    out += _str_field(MOCK_DEVICE_NAME)
    out += _str_field("Mock device used by Listeningway integration_harness.py")
    out += _str_field("0.0.0")
    out += _str_field("MOCK-0001")
    out += _str_field("integration_harness.py")
    # modes: 1 mode "Direct"
    out += struct.pack("<H", 1)        # num_modes
    out += struct.pack("<I", 0)        # active_mode
    # ModeDescription
    out += _str_field("Direct")
    out += struct.pack("<I", 0)        # value
    out += struct.pack("<I", 1 << 5)   # flags = HasPerLedColor
    out += struct.pack("<I", 0)        # speed_min (unused)
    out += struct.pack("<I", 0)        # speed_max
    out += struct.pack("<I", 0)        # brightness_min
    out += struct.pack("<I", 0)        # brightness_max
    out += struct.pack("<I", 0)        # colors_min
    out += struct.pack("<I", 0)        # colors_max
    out += struct.pack("<I", 0)        # speed
    out += struct.pack("<I", 0)        # brightness
    out += struct.pack("<I", 0)        # direction
    out += struct.pack("<I", 1)        # color_mode = PerLed
    out += struct.pack("<H", 0)        # num_colors (mode-specific) = 0
    # zones: 1 Linear zone
    out += struct.pack("<H", 1)
    out += _str_field("Strip")
    out += struct.pack("<I", 1)        # ZoneType.Linear
    out += struct.pack("<I", MOCK_LED_COUNT)  # leds_min
    out += struct.pack("<I", MOCK_LED_COUNT)  # leds_max
    out += struct.pack("<I", MOCK_LED_COUNT)  # leds_count
    out += struct.pack("<H", 0)        # matrix_length = 0 (no optional block)
    # LEDs
    out += struct.pack("<H", MOCK_LED_COUNT)
    for i in range(MOCK_LED_COUNT):
        out += _str_field(f"LED {i}")
        out += struct.pack("<I", 0)    # value
    # device colors block (initial state — all black)
    out += struct.pack("<H", MOCK_LED_COUNT)
    for _ in range(MOCK_LED_COUNT):
        out += struct.pack("<BBBB", 0, 0, 0, 0)
    return bytes(out)


_MOCK_DEVICE_BLOB = _build_mock_device()


def _send_reply(conn: socket.socket, dev_idx: int, mtype: int, payload: bytes) -> None:
    header = struct.pack("<4sIII", b"ORGB", dev_idx, mtype, len(payload))
    conn.sendall(header + payload)


@dataclass
class OrgbStats:
    connected: bool = False
    client_name: str = ""
    bytes_in: int = 0
    frames_in: int = 0
    last_frame_time: float = 0.0
    timestamps: deque = field(default_factory=lambda: deque(maxlen=200))
    last_led_count: int = 0
    last_color: tuple[int, int, int] = (0, 0, 0)
    last_error: str = ""
    last_request_type: int = -1
    custom_mode_set: bool = False

    def hz(self, now: float) -> float:
        cutoff = now - 1.0
        return float(sum(1 for t in self.timestamps if t >= cutoff))


class OpenRgbMock:
    def __init__(self, port: int) -> None:
        self.port = port
        self.stats = OrgbStats()
        self.lock = threading.Lock()
        self.thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        self.thread.start()

    def _run(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("0.0.0.0", self.port))
        sock.listen(4)
        sock.settimeout(1.0)
        while True:
            try:
                conn, _addr = sock.accept()
            except socket.timeout:
                continue
            with self.lock:
                self.stats.connected = True
                self.stats.last_error = ""
            try:
                self._serve(conn)
            except Exception as exc:
                with self.lock:
                    self.stats.last_error = f"{type(exc).__name__}: {exc}"
            finally:
                with self.lock:
                    self.stats.connected = False
                conn.close()

    def _read_exact(self, conn: socket.socket, n: int) -> bytes:
        buf = bytearray()
        while len(buf) < n:
            chunk = conn.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("peer closed connection")
            buf.extend(chunk)
        return bytes(buf)

    def _serve(self, conn: socket.socket) -> None:
        while True:
            header = self._read_exact(conn, 16)
            magic, dev_idx, mtype, msize = struct.unpack("<4sIII", header)
            if magic != b"ORGB":
                raise ValueError(f"bad magic: {magic!r}")
            payload = self._read_exact(conn, msize) if msize > 0 else b""

            with self.lock:
                self.stats.bytes_in += 16 + len(payload)
                self.stats.last_request_type = mtype

            if mtype == MSG_REQUEST_PROTOCOL_VERSION:
                # Reply with our supported version.
                _send_reply(conn, dev_idx, MSG_REQUEST_PROTOCOL_VERSION,
                            struct.pack("<I", PROTOCOL_VERSION_REPLY))

            elif mtype == MSG_SET_CLIENT_NAME:
                # Payload is a null-terminated client name. Stash it.
                name = payload.rstrip(b"\x00").decode("ascii", errors="replace")
                with self.lock:
                    self.stats.client_name = name

            elif mtype == MSG_REQUEST_CONTROLLER_COUNT:
                _send_reply(conn, 0, MSG_REQUEST_CONTROLLER_COUNT,
                            struct.pack("<I", 1))   # one mock device

            elif mtype == MSG_REQUEST_CONTROLLER_DATA:
                # Payload contains the protocol version the client wants;
                # we ignore it and ship our static device blob.
                blob = struct.pack("<I", len(_MOCK_DEVICE_BLOB)) + _MOCK_DEVICE_BLOB
                _send_reply(conn, dev_idx, MSG_REQUEST_CONTROLLER_DATA, blob)

            elif mtype == MSG_RGBCTRL_SETCUSTOMMODE:
                with self.lock:
                    self.stats.custom_mode_set = True

            elif mtype in (MSG_RGBCTRL_UPDATELEDS, MSG_RGBCTRL_UPDATEZONELEDS):
                # data_size (uint32) + (zone_idx if zone-leds) + num_colors (uint16) + Color[]
                if len(payload) < 6:
                    continue
                cursor = 4   # skip data_size duplicate
                if mtype == MSG_RGBCTRL_UPDATEZONELEDS:
                    cursor += 4   # skip zone_idx
                if len(payload) < cursor + 2:
                    continue
                num_colors = struct.unpack_from("<H", payload, cursor)[0]
                cursor += 2
                if num_colors > 0 and len(payload) >= cursor + 4:
                    r, g, b, _pad = struct.unpack_from("<BBBB", payload, cursor)
                    sample = (r, g, b)
                else:
                    sample = (0, 0, 0)
                now = time.monotonic()
                with self.lock:
                    self.stats.frames_in += 1
                    self.stats.last_frame_time = now
                    self.stats.timestamps.append(now)
                    self.stats.last_led_count = num_colors
                    self.stats.last_color = sample

            elif mtype == MSG_RGBCTRL_UPDATESINGLELED:
                # cppSDK doesn't use this; ignore but consume the payload.
                pass

            else:
                # Unknown message — log and consume.
                with self.lock:
                    self.stats.last_error = f"unknown msg type {mtype}"


# ---------------------------------------------------------------------------
# Live dashboard.
# ---------------------------------------------------------------------------

def render_dashboard(osc: OscReceiver | None,
                     orgb: OpenRgbMock | None) -> None:
    sys.stdout.write(CLEAR)
    print(f"{BOLD}Listeningway integration harness{RESET}  "
          f"{DIM}(Ctrl-C to exit){RESET}\n")

    now = time.monotonic()

    # ---- OSC pane -------------------------------------------------------
    if osc is not None:
        age = now - osc.last_packet if osc.last_packet else math.inf
        connected = age < 2.0
        lamp = f"{GREEN}●{RESET}" if connected else f"{RED}●{RESET}"
        print(f"  {lamp}  {BOLD}OSC{RESET} on udp/{osc.port}  "
              f"{DIM}({osc.bytes_in/1024:.1f} KiB received){RESET}")
        with osc.lock:
            ordered = sorted(SCALAR_RANGES) + sorted(ARRAY_ADDRESSES)
            print(f"      {DIM}{'address':<36s} {'Hz':>5s} {'last':>10s} "
                  f"{'min..max':>14s} {'flags':<10s}{RESET}")
            seen_count = 0
            for addr in ordered:
                stats = osc.stats.get(addr)
                if stats is None:
                    flag = f"{RED}MISSING{RESET}"
                    dash_last = f"{DIM}—{RESET}"
                    dash_rng  = f"{DIM}—{RESET}"
                    print(f"      {addr:<36s} {0:>5.0f} {dash_last:>19s} "
                          f"{dash_rng:>23s} {flag}")
                    continue
                seen_count += 1
                hz = stats.hz(now)
                if stats.arity == 1:
                    last_str = f"{stats.last[0]:+.4f}" if stats.last else "—"
                else:
                    last_str = f"f[{stats.arity}]"
                rng = f"{stats.min_seen:+.2f}..{stats.max_seen:+.2f}"
                # Range / NaN flags
                flags = []
                exp = SCALAR_RANGES.get(addr)
                if exp and stats.arity == 1:
                    lo, hi = exp
                    if stats.min_seen < lo or stats.max_seen > hi:
                        flags.append(f"{YELLOW}OOR{RESET}")
                if stats.nan_or_inf:
                    flags.append(f"{RED}NaN{RESET}")
                flag = " ".join(flags) if flags else f"{GREEN}OK{RESET}"
                print(f"      {addr:<36s} {hz:>5.1f} {last_str:>10s} "
                      f"{rng:>14s} {flag}")
            unexpected = list(osc.unexpected.items())
            if unexpected:
                print(f"      {YELLOW}Unexpected addresses:{RESET}")
                for addr, count in sorted(unexpected):
                    print(f"        {addr}  ×{count}")
            print(f"      {DIM}{seen_count}/{len(SCALAR_RANGES) + len(ARRAY_ADDRESSES)} "
                  f"expected addresses present{RESET}")

    # ---- OpenRGB pane ---------------------------------------------------
    if orgb is not None:
        with orgb.lock:
            stats = orgb.stats
            connected = stats.connected
            lamp = f"{GREEN}●{RESET}" if connected else f"{DIM}○{RESET}"
            print()
            print(f"  {lamp}  {BOLD}OpenRGB mock{RESET} on tcp/{orgb.port}  "
                  f"{DIM}({stats.bytes_in/1024:.1f} KiB received){RESET}")
            if connected and stats.client_name:
                print(f"      client: {CYAN}{stats.client_name}{RESET}")
            hz = stats.hz(now)
            ago = (now - stats.last_frame_time) if stats.last_frame_time else math.inf
            ago_s = "—" if ago > 1e6 else f"{ago:.2f}s ago"
            print(f"      frames received: {stats.frames_in:>6d}   "
                  f"rate: {hz:>5.1f} Hz   last: {ago_s}")
            print(f"      custom-mode set: "
                  f"{GREEN if stats.custom_mode_set else DIM}"
                  f"{'yes' if stats.custom_mode_set else 'no'}{RESET}   "
                  f"last LED count: {stats.last_led_count}   "
                  f"last colour: rgb({stats.last_color[0]:>3d},"
                  f"{stats.last_color[1]:>3d},{stats.last_color[2]:>3d})")
            if stats.last_error:
                print(f"      {RED}error:{RESET} {stats.last_error}")

    sys.stdout.flush()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                  formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--osc-port",  type=int, default=9000)
    ap.add_argument("--orgb-port", type=int, default=6742)
    ap.add_argument("--osc-only",  action="store_true",
                    help="Skip the OpenRGB mock server.")
    ap.add_argument("--orgb-only", action="store_true",
                    help="Skip the OSC receiver.")
    ap.add_argument("--refresh",   type=float, default=0.5,
                    help="Dashboard refresh interval in seconds (default: 0.5).")
    args = ap.parse_args()

    osc: OscReceiver | None = None
    orgb: OpenRgbMock | None = None

    try:
        if not args.orgb_only:
            osc = OscReceiver(args.osc_port)
            osc.start()
        if not args.osc_only:
            orgb = OpenRgbMock(args.orgb_port)
            orgb.start()
    except OSError as exc:
        print(f"failed to bind: {exc}", file=sys.stderr)
        return 1

    try:
        while True:
            render_dashboard(osc, orgb)
            time.sleep(max(0.1, args.refresh))
    except KeyboardInterrupt:
        sys.stdout.write(f"{CSI}0m\n")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
