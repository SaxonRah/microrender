#!/usr/bin/env python3
"""
MicroRender Pico screenshot receiver.

This script asks the Pico firmware for an RGB565 screenshot over USB CDC and
saves it as a PNG. It is robust against the Pico rebooting when the COM port is
opened: it keeps sending SCREENSHOT until the firmware answers with MRSHOT1.

Firmware protocol:
  host -> Pico:
    SCREENSHOT\n

  Pico -> host:
    MRSHOT1 <width> <height> <byte_count>\n
    <raw RGB565 little-endian bytes>

Install:
  py -m pip install pyserial pillow

Example:
  py tools_capture_pico_screenshot.py COM5 .\pico2_screenshot.png
"""

import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    raise SystemExit("Missing pyserial. Install with: py -m pip install pyserial")

try:
    from PIL import Image
except ImportError:
    raise SystemExit("Missing pillow. Install with: py -m pip install pillow")


def rgb565_to_rgb888(buf, w, h):
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


def main():
    if len(sys.argv) < 2:
        print("Usage: py tools_capture_pico_screenshot.py COM5 [output.png]")
        return 2

    port = sys.argv[1]
    if len(sys.argv) >= 3:
        out = Path(sys.argv[2])
    else:
        out = Path.home() / "Desktop" / ("microrender_pico_%s.png" % time.strftime("%Y%m%d_%H%M%S"))

    header = None

    with serial.Serial(port, 115200, timeout=0.25, write_timeout=2) as ser:
        # Opening USB CDC often resets the board. Give it a moment, but also keep
        # retrying the command so it is not lost during reboot.
        ser.reset_input_buffer()

        deadline = time.time() + 20.0
        next_send = 0.0

        print("Waiting for MRSHOT1 from Pico on %s..." % port)

        while time.time() < deadline:
            now = time.time()
            if now >= next_send:
                ser.write(b"SCREENSHOT\n")
                ser.flush()
                next_send = now + 0.5

            line = ser.readline()
            if not line:
                continue

            text = line.decode("ascii", "replace").strip()
            if text.startswith("MRSHOT1 "):
                header = text
                break

            if text:
                print("Pico:", text)

        if header is None:
            raise SystemExit("Did not receive MRSHOT1 header. Reflash firmware with the screenshot patch.")

        parts = header.split()
        if len(parts) != 4:
            raise SystemExit("Bad screenshot header: %r" % header)

        w = int(parts[1])
        h = int(parts[2])
        n = int(parts[3])
        expected = w * h * 2
        if n != expected:
            raise SystemExit("Bad byte count: got %d expected %d" % (n, expected))

        print("Receiving %dx%d RGB565 screenshot (%d bytes)..." % (w, h, n))

        data = bytearray()
        while len(data) < n:
            chunk = ser.read(n - len(data))
            if not chunk:
                raise SystemExit("Timed out after %d/%d bytes" % (len(data), n))
            data.extend(chunk)

    img = Image.frombytes("RGB", (w, h), rgb565_to_rgb888(data, w, h))
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)
    print("Saved:", out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
