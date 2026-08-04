#!/usr/bin/env python3
"""Capture a MicroRender RGB565 framebuffer from a Pico over USB CDC.

Firmware protocol:
  host -> Pico:  SCREENSHOT\n
  Pico -> host:  MRSHOT1 <width> <height> <byte_count>\n
                 <raw RGB565 little-endian bytes>

The shared Pico screenshot service is enabled by default for every MicroRender
Pico application. Verbose FPS logging (serial=ON) is not required.
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    raise SystemExit("Missing pyserial. Install with: py -m pip install pyserial")

try:
    from PIL import Image
except ImportError:
    raise SystemExit("Missing pillow. Install with: py -m pip install pillow")


def rgb565_to_rgb888(buf: bytes | bytearray, w: int, h: int) -> bytes:
    expected = w * h * 2
    if len(buf) != expected:
        raise ValueError("RGB565 buffer is %d bytes; expected %d" % (len(buf), expected))

    out = bytearray(w * h * 3)
    j = 0
    for i in range(0, len(buf), 2):
        v = buf[i] | (buf[i + 1] << 8)
        r = (v >> 11) & 0x1F
        g = (v >> 5) & 0x3F
        b = v & 0x1F
        out[j + 0] = (r << 3) | (r >> 2)
        out[j + 1] = (g << 2) | (g >> 4)
        out[j + 2] = (b << 3) | (b >> 2)
        j += 3
    return bytes(out)


def available_ports() -> str:
    ports = list(list_ports.comports())
    if not ports:
        return "none detected"
    return ", ".join("%s (%s)" % (p.device, p.description) for p in ports)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Capture any MicroRender Pico application over USB CDC."
    )
    parser.add_argument("port", help="Serial port, for example COM5")
    parser.add_argument(
        "output",
        nargs="?",
        type=Path,
        default=None,
        help="Output PNG path",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=20.0,
        help="Seconds to wait for the MRSHOT1 header (default: 20)",
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=115200,
        help="Nominal CDC baud setting (default: 115200; USB CDC ignores wire baud)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.timeout <= 0:
        raise SystemExit("--timeout must be greater than zero")

    out = args.output
    if out is None:
        out = Path.home() / "Desktop" / (
            "microrender_pico_%s.png" % time.strftime("%Y%m%d_%H%M%S")
        )

    header = None
    started = time.monotonic()
    last_progress = started

    print("Opening %s at nominal %d baud..." % (args.port, args.baud), flush=True)
    try:
        ser = serial.Serial(
            args.port,
            args.baud,
            timeout=0.25,
            write_timeout=2.0,
        )
    except serial.SerialException as exc:
        raise SystemExit(
            "Could not open %s: %s\nAvailable ports: %s\n"
            "Close the VS Code serial monitor or any other program using the port."
            % (args.port, exc, available_ports())
        )

    with ser:
        # Opening USB CDC can reset/re-enumerate some Pico configurations. Keep
        # retrying the request so a command sent during startup is not lost.
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        deadline = time.monotonic() + args.timeout
        next_send = 0.0
        print(
            "Waiting for MRSHOT1 from Pico on %s (timeout %.1fs)..."
            % (args.port, args.timeout),
            flush=True,
        )

        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_send:
                try:
                    ser.write(b"SCREENSHOT\n")
                    ser.flush()
                except serial.SerialTimeoutException:
                    raise SystemExit("Timed out writing SCREENSHOT to %s" % args.port)
                next_send = now + 0.5

            line = ser.readline()
            if line:
                text = line.decode("ascii", "replace").strip()
                if text.startswith("MRSHOT1 "):
                    header = text
                    break
                if text:
                    print("Pico:", text, flush=True)

            if now - last_progress >= 2.0:
                print(
                    "  still waiting (%.1fs elapsed)..." % (now - started),
                    flush=True,
                )
                last_progress = now

        if header is None:
            raise SystemExit(
                "Did not receive MRSHOT1 from %s after %.1f seconds.\n"
                "Most likely causes:\n"
                "  1. The flashed firmware was not built with serial=ON.\n"
                "  2. The firmware predates screenshot support for this frontend.\n"
                "  3. %s is not the Pico's current CDC port.\n"
                "Available ports: %s"
                % (args.port, args.timeout, args.port, available_ports())
            )

        parts = header.split()
        if len(parts) != 4:
            raise SystemExit("Bad screenshot header: %r" % header)

        try:
            w = int(parts[1])
            h = int(parts[2])
            n = int(parts[3])
        except ValueError as exc:
            raise SystemExit("Bad numeric screenshot header %r: %s" % (header, exc))

        if w <= 0 or h <= 0 or w > 4096 or h > 4096:
            raise SystemExit("Implausible screenshot dimensions: %dx%d" % (w, h))
        expected = w * h * 2
        if n != expected:
            raise SystemExit("Bad byte count: got %d expected %d" % (n, expected))

        print("Receiving %dx%d RGB565 screenshot (%d bytes)..." % (w, h, n), flush=True)

        data = bytearray()
        data_deadline = time.monotonic() + max(args.timeout, 10.0)
        last_report = 0
        while len(data) < n:
            chunk = ser.read(min(n - len(data), 16384))
            if chunk:
                data.extend(chunk)
                pct = (len(data) * 100) // n
                if pct >= last_report + 10:
                    print("  %d%% (%d/%d bytes)" % (pct, len(data), n), flush=True)
                    last_report = pct
                data_deadline = time.monotonic() + max(args.timeout, 10.0)
                continue
            if time.monotonic() >= data_deadline:
                raise SystemExit("Timed out after %d/%d bytes" % (len(data), n))

    img = Image.frombytes("RGB", (w, h), rgb565_to_rgb888(data, w, h))
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)
    print("Saved:", out, flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
