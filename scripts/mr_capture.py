#!/usr/bin/env python3
"""Capture screenshots and metrics from MicroRender frontends into one report.

Every platform emits the same capture format, so one parser handles all of
them and a file written by the Raylib frontend is byte-comparable with one
pulled off a Pico over USB:

    MRSHOT1 <width> <height> <bytes>\\n
    <width*height little-endian RGB565 pixels>

Usage
-----
    python scripts/mr_capture.py all                    everything, unattended
    python scripts/mr_capture.py raylib                 the Raylib matrix
    python scripts/mr_capture.py dos                    the DOS matrix
    python scripts/mr_capture.py pico                   capture from a Pico
    python scripts/mr_capture.py pico flash             build, flash, capture
    python scripts/mr_capture.py pico COM5 stress-raw   explicit port and preset

"all" builds and flashes the Pico before capturing, because a stale image is
the one way this report can be quietly wrong. The port is found by USB vendor
ID, so it does not need naming. Rows accumulate across runs, so capturing one
platform at a time builds the same table up.

Pico capture talks to whatever firmware happens to be flashed, which is not
necessarily the firmware you last built. The reported configuration is checked
against the named preset (default stress-lace) and the capture is refused on a
mismatch, rather than filing a screenshot under the wrong label.

Output lands in capture/ as PNGs plus report.md and report.csv.

Only the standard library is required for Raylib capture; PNG encoding uses
zlib directly rather than pulling in Pillow. Pico capture needs pyserial.
"""

import csv
import os
import struct
import subprocess
import sys
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "capture")

# label, extra args. Kept short deliberately: this is a capture set for
# comparison and documentation, not the exhaustive matrix that
# mr_test_raylib.bat covers.
RAYLIB_CASES = [
    ("game-tiled16", ["--demo", "game", "--mode", "tiled", "--tile", "16",
                      "--autoplay"]),
    ("game-raw", ["--demo", "game", "--mode", "raw", "--autoplay"]),
    ("stress-visible-512", ["--demo", "stress", "--mode", "visible",
                            "--sprites", "512"]),
    ("stress-lace-1024", ["--demo", "stress", "--mode", "lace", "--sprites",
                          "1024", "--lace-block", "8"]),
    ("stress-render-1024", ["--demo", "stress", "--mode", "render",
                            "--sprites", "1024"]),
]


# --------------------------------------------------------------------------
# capture format
# --------------------------------------------------------------------------

def parse_shot(blob):
    """Split an MRSHOT1 blob into (width, height, rgb565 bytes)."""
    nl = blob.find(b"\n")
    if nl < 0:
        raise ValueError("no MRSHOT1 header line")
    parts = blob[:nl].split()
    if len(parts) != 4 or parts[0] != b"MRSHOT1":
        raise ValueError("bad header: %r" % blob[:nl][:60])
    w, h, nbytes = int(parts[1]), int(parts[2]), int(parts[3])
    body = blob[nl + 1:nl + 1 + nbytes]
    if len(body) != nbytes:
        raise ValueError("short body: got %d of %d bytes" % (len(body), nbytes))
    return w, h, body


def rgb565_to_rgb888(body, w, h):
    """Expand to 8 bits per channel, replicating high bits into the low ones so
    full-scale input stays full scale (0x1F -> 0xFF, not 0xF8)."""
    out = bytearray(w * h * 3)
    for i in range(w * h):
        v = body[2 * i] | (body[2 * i + 1] << 8)
        r = (v >> 11) & 0x1F
        g = (v >> 5) & 0x3F
        b = v & 0x1F
        out[3 * i] = (r << 3) | (r >> 2)
        out[3 * i + 1] = (g << 2) | (g >> 4)
        out[3 * i + 2] = (b << 3) | (b >> 2)
    return bytes(out)


def write_png(path, rgb, w, h):
    """Minimal PNG writer. Avoids a Pillow dependency for what is a handful of
    fixed-size screenshots."""
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter type 0
        raw += rgb[y * w * 3:(y + 1) * w * 3]

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def read_report(path):
    vals = {}
    if not os.path.exists(path):
        return vals
    with open(path, "r") as f:
        for line in f:
            if "=" in line:
                k, v = line.strip().split("=", 1)
                vals[k] = v
    return vals


# --------------------------------------------------------------------------
# platforms
# --------------------------------------------------------------------------

# 600 frames at 2,500 FPS is a quarter of a second, so startup dominates and
# the simulation average comes out nearer 50 than 60. Long enough to measure.
def capture_raylib(rows, frames=8000):
    exe = None
    for cand in ("microrender_raylib.exe", "microrender_raylib"):
        for sub in ("build/raylib/Release", "build/raylib", "build-raylib"):
            p = os.path.join(ROOT, sub, cand)
            if os.path.exists(p):
                exe = p
                break
        if exe:
            break
    if not exe:
        print("raylib: no built binary found; run  .\\mr.bat build raylib")
        return

    for label, args in RAYLIB_CASES:
        shot = os.path.join(OUT, "raylib-%s.shot" % label)
        rep = os.path.join(OUT, "raylib-%s.txt" % label)
        cmd = [exe] + args + ["--frames", str(frames), "--fps", "0",
                              "--shot", shot, "--report", rep]
        print("raylib: %s" % label)
        try:
            subprocess.run(cmd, cwd=ROOT, timeout=120,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            print("        timed out")
            continue
        rows.append(finish("raylib", label, shot, read_report(rep)))


# Metrics field -> the preset flag that determines it. Only fields the firmware
# actually reports can be checked.
PICO_EXPECT = {
    "mode": ("MR_STRESS_MODE",
             {"visible": "0", "raw": "1", "dirty": "2", "dirtyfixed": "3",
              "render": "4", "lcdtest": "5", "lace": "6", "lacefixed": "7"}),
    "spr": ("MR_STRESS_SPRITES", None),
    "spi": ("MR_LCD_SPI_BAUD", None),
    "lace": ("MR_STRESS_LACE_BLOCK_H", None),
    "phases": ("MR_STRESS_LACE_PHASES", None),
    "core1": ("MR_PICO_PRESENT_CORE1", {"ON": "1", "OFF": "0"}),
}


def preset_flags(preset):
    """Resolve a preset to its -D flags with the project's own script, so this
    cannot drift from what the build actually does."""
    script = os.path.join(ROOT, "scripts", "mr_preset_flags.py")
    try:
        out = subprocess.run([sys.executable, script, "microrender", preset],
                             cwd=ROOT, capture_output=True, text=True,
                             timeout=30)
    except (OSError, subprocess.TimeoutExpired) as exc:
        print("        cannot resolve preset %s: %s" % (preset, exc))
        return None
    if out.returncode != 0:
        print("        cannot resolve preset %s: %s"
              % (preset, out.stderr.strip()))
        return None
    flags = {}
    for tok in out.stdout.split():
        if tok.startswith("-D") and "=" in tok:
            k, v = tok[2:].split("=", 1)
            flags[k] = v
    return flags


def check_pico_config(fields, preset):
    """Mismatches between the running firmware and a preset. Empty means it
    matches."""
    flags = preset_flags(preset)
    if flags is None:
        return ["could not resolve preset %s" % preset]

    problems = []
    for field, (flag, mapping) in PICO_EXPECT.items():
        if field not in fields:
            # Firmware older than the core1/lace/phases metrics fields.
            continue
        want = flags.get(flag)
        if want is None:
            continue
        if mapping:
            want = mapping.get(want, want)
        got = fields[field]
        try:
            same = int(got) == int(want)
        except ValueError:
            same = str(got) == str(want)
        if not same:
            problems.append("%s: firmware reports %s, preset %s wants %s"
                            % (field, got, preset, want))
    return problems


# mr.bat run target -> label. The two game binaries differ only in present
# mode, which is the comparison worth capturing: tiled strips versus a full
# staged frame. The stress binaries have no capture hooks yet.
DOS_CASES = [
    ("dos", "game-tiled"),
    ("dosraw", "game-raw"),
]


def capture_dos(rows, frames=2000):
    for target, label in DOS_CASES:
        capture_dos_case(rows, target, label, frames)


def capture_dos_case(rows, target, label, frames):
    """Run the DOS build under DOSBox and collect its capture.

    Goes through mr.bat rather than invoking DOSBox directly: the runner
    already locates DOSBox, writes a config with machine=vgaonly so Mode X is
    emulated on real VGA rather than an SVGA compatibility layer, and mounts
    dosroot as C:. Duplicating any of that here would drift.

    MR_DOSBOX_NOPAUSE drops the "press any key" so this is unattended.
    MR_DOSBOX_CYCLES is worth setting for a quotable number: the default is
    cycles=max, which measures the host CPU rather than any period machine.
    """
    dosroot = os.path.join(ROOT, "microrender_dos", "dosroot")
    dist = os.path.join(ROOT, "microrender_dos", "dist")
    exe = {"dos": "mrender.exe", "dosraw": "mraw.exe"}.get(target, "mrender.exe")
    if not (os.path.exists(os.path.join(dosroot, exe)) or
            os.path.exists(os.path.join(dist, exe))):
        print("dos: mrender.exe not in dosroot\\ or dist\\; run")
        print("       .\\mr.bat build dos mode=both tile=16 vsync=0")
        return

    # 8.3 filenames. "dos.shot" has a four-character extension, so DOS writes
    # DOS.SHO and anything waiting on the requested name waits forever. Ask for
    # a name that survives the filesystem in the first place.
    shot = os.path.join(dosroot, "dos.bin")
    rep = os.path.join(dosroot, "dos.txt")
    for stale in (shot, rep):
        if os.path.exists(stale):
            os.remove(stale)

    env = dict(os.environ)
    env["MR_DOSBOX_NOPAUSE"] = "1"
    # =value forms: a single token each, so nothing depends on how the runner
    # or DOS splits a two-token option.
    cmd = [os.path.join(ROOT, "mr.bat"), "run", target, "/auto",
           "/frames=%d" % frames, "/shot=dos.bin", "/report=dos.txt"]
    print("dos: %s under DOSBox (cycles=%s); waiting for the capture ..."
          % (label, env.get("MR_DOSBOX_CYCLES", "max")))
    print("        %s" % " ".join(cmd[1:]))
    print("        watching %s" % shot)
    try:
        # Output is not swallowed: when this goes wrong it goes wrong inside
        # the runner or DOSBox, and hiding that is what made the last three
        # failures indistinguishable from each other.
        subprocess.run(cmd, cwd=ROOT, env=env, timeout=300)
    except (OSError, subprocess.TimeoutExpired) as exc:
        print("        could not run: %s" % exc)
        return

    # Do not trust the runner to have blocked. DOSBox is a GUI-subsystem
    # executable, and depending on the Windows shell and DOSBox build, the
    # launcher can return while it is still running -- so subprocess.run
    # completing says nothing about whether the program has run yet.
    #
    # Wait for the artifact instead, and wait for it to stop growing: the file
    # appears when the frontend opens it and is only complete once the last
    # tile has been written.
    deadline = time.time() + 180.0
    stable = 0
    last = -1
    while time.time() < deadline:
        if os.path.exists(shot):
            size = os.path.getsize(shot)
            if size == last and size > 0:
                stable += 1
                if stable >= 3:
                    break
            else:
                stable = 0
                last = size
        time.sleep(0.5)

    if not os.path.exists(shot):
        # The frontend writes relative to the DOSBox working directory, which
        # is whatever got mounted as C:. If that is not the directory being
        # watched, the file is still on disk somewhere -- find it and say so
        # rather than reporting nothing at all.
        for alt_dir in (dosroot, dist, ROOT,
                        os.path.join(ROOT, "microrender_dos")):
            for alt_name in ("dos.bin", "DOS.BIN", "dos.sho", "DOS.SHO"):
                alt = os.path.join(alt_dir, alt_name)
                if os.path.exists(alt):
                    print("        found the capture at %s" % alt)
                    shot = alt
                    rep = os.path.join(alt_dir, "dos.txt")
                    break
            if os.path.exists(shot):
                break

    if not os.path.exists(shot):
        print("        no capture produced.")
        print("        contents of %s:" % dosroot)
        try:
            for name in sorted(os.listdir(dosroot)):
                print("          %s" % name)
        except OSError as exc:
            print("          cannot list: %s" % exc)
        print("        Run it by hand and read the DOSBox window before it")
        print("        closes; the frontend prints whether it wrote the file:")
        print("       .\\mr.bat run dos /auto /frames=%d /shot=dos.bin"
              % frames)
        return

    dest = os.path.join(OUT, "dos-%s.bin" % label)
    os.replace(shot, dest)
    fields = read_report(rep)
    if os.path.exists(rep):
        os.remove(rep)
    if "cycles" not in fields:
        fields["cycles"] = env.get("MR_DOSBOX_CYCLES", "max")
    rows.append(finish("dos", label, dest, fields))


def find_picotool():
    """picotool, from PATH or the SDK install the Pico extension lays down."""
    for cand in ("picotool", "picotool.exe"):
        try:
            subprocess.run([cand, "version"], capture_output=True, timeout=15)
            return cand
        except (OSError, subprocess.TimeoutExpired):
            pass
    base = os.path.join(os.path.expanduser("~"), ".pico-sdk", "picotool")
    if os.path.isdir(base):
        # Newest version directory first.
        for ver in sorted(os.listdir(base), reverse=True):
            cand = os.path.join(base, ver, "picotool", "picotool.exe")
            if os.path.exists(cand):
                return cand
            cand = os.path.join(base, ver, "picotool", "picotool")
            if os.path.exists(cand):
                return cand
    return None


def pico_ping(port, baud=115200, timeout=2.0):
    """True when a MicroRender firmware answers on this port.

    Vendor ID alone is not enough to identify the board: a debug probe
    enumerates under the same 2E8A, so picking the first match lands on the
    probe as often as on the target. The firmware answers PING with a MRPICO1
    banner, so ask rather than guess.
    """
    try:
        import serial
    except ImportError:
        return False
    try:
        with serial.Serial(port, baud, timeout=timeout) as ser:
            time.sleep(0.2)
            ser.reset_input_buffer()
            ser.write(b"PING\n")
            ser.flush()
            deadline = time.time() + timeout
            while time.time() < deadline:
                line = ser.readline()
                if not line:
                    break
                if b"MRPICO1" in line:
                    return True
                # A running build prints metrics continuously, which is just as
                # good an identification as the banner.
                if line.startswith(b"stress ") or line.startswith(b"game "):
                    return True
    except (OSError, ValueError):
        return False
    return False


def find_pico_port():
    """The port a MicroRender firmware is actually answering on."""
    try:
        from serial.tools import list_ports
    except ImportError:
        return None
    ports = list(list_ports.comports())
    # Prefer the right vendor, but try everything rather than give up: a board
    # behind a USB hub or a generic CDC driver may not report a vid at all.
    ordered = ([p.device for p in ports if p.vid == 0x2E8A] +
               [p.device for p in ports if p.vid != 0x2E8A])
    for dev in ordered:
        if pico_ping(dev):
            return dev
    return None


def build_and_flash_pico(preset):
    """Build the preset and load it over USB.

    picotool can reboot a running board into BOOTSEL itself, because the
    firmware enables the USB reset interface alongside stdio, so this does not
    need the button. -x runs the image afterwards instead of leaving the board
    sitting in the bootloader.
    """
    print("pico: building %s" % preset)
    try:
        r = subprocess.run([os.path.join(ROOT, "mr.bat"), "build", "pico",
                            preset, "serial=ON"], cwd=ROOT, timeout=900)
    except (OSError, subprocess.TimeoutExpired) as exc:
        print("        build failed: %s" % exc)
        return False
    if r.returncode != 0:
        print("        build failed")
        return False

    uf2 = None
    for root, _dirs, files in os.walk(os.path.join(ROOT, "microrender")):
        if os.path.basename(root).startswith("build-" + preset) or \
           os.path.basename(root) == "build-" + preset:
            for f in files:
                if f.endswith(".uf2"):
                    uf2 = os.path.join(root, f)
    if not uf2:
        print("        no .uf2 produced for %s" % preset)
        return False

    tool = find_picotool()
    if not tool:
        print("        picotool not found; flash %s by hand" % uf2)
        return False

    print("        flashing %s" % os.path.basename(uf2))
    try:
        r = subprocess.run([tool, "load", "-f", "-x", uf2], timeout=180)
    except (OSError, subprocess.TimeoutExpired) as exc:
        print("        flash failed: %s" % exc)
        return False
    if r.returncode != 0:
        print("        flash failed; is the board connected?")
        return False

    # The USB device disappears and re-enumerates, so the port is not there
    # immediately after the reset.
    for _ in range(40):
        time.sleep(0.5)
        if find_pico_port():
            return True

    # Known unresolved: on at least one board, a picotool-flashed image comes
    # up with a white display and never runs, while the same UF2 dragged on in
    # BOOTSEL works every time. Rather than fail the whole capture, ask for the
    # manual step and wait for the board to come back.
    print()
    print("        The flashed image is not responding. This board needs a")
    print("        manual flash: hold BOOTSEL, replug, and drag this file on:")
    print("          %s" % uf2)
    print("        Waiting for the board to come back (Ctrl+C to give up) ...")
    deadline = time.time() + 300.0
    while time.time() < deadline:
        time.sleep(1.0)
        port = find_pico_port()
        if port:
            print("        board is back on %s" % port)
            return True
    print("        gave up waiting.")
    return False


def capture_pico(rows, port=None, preset="stress-lace", baud=115200,
                 flash=False):
    if flash and not build_and_flash_pico(preset):
        return
    if port is None:
        port = find_pico_port()
        if port is None:
            print("pico: no board found (USB vendor 2E8A). Pass the port "
                  "explicitly, e.g.  pico COM5")
            return
        print("pico: found board on %s" % port)

    try:
        import serial
    except ImportError:
        print("pico: pyserial not installed  (pip install pyserial)")
        return

    print("pico: opening %s" % port)
    with serial.Serial(port, baud, timeout=5) as ser:
        time.sleep(0.3)
        ser.reset_input_buffer()

        # Collect metrics lines first: the firmware prints them continuously,
        # and one sample is not worth much when the interesting number is an
        # average.
        samples = []
        deadline = time.time() + 6.0
        while time.time() < deadline:
            line = ser.readline().decode("ascii", "replace").strip()
            if line.startswith("stress ") or line.startswith("game "):
                samples.append(line)
        fields = {}
        if samples:
            fields = parse_metrics(samples[-1])
            fps = [float(parse_metrics(s).get("fps", 0)) for s in samples]
            fps = [v for v in fps if v > 0]
            if fps:
                fields["fps_avg"] = "%.2f" % (sum(fps) / len(fps))
                fields["fps_samples"] = str(len(fps))
            # sim_hz arrives straight from the firmware, computed there against
            # the same elapsed time as the frame rate. Without it a Pico row
            # cannot be compared with a DOS or Raylib row, which is the whole
            # point of the report.
            if "sim_hz" not in fields:
                print("        note: firmware predates sim_hz; reflash to "
                      "compare simulation rates")

        if not fields:
            print("pico: no metrics line seen. Is this a serial=ON build, and\n"
                  "      is the firmware actually running?")
            return

        problems = check_pico_config(fields, preset)
        if problems:
            print("pico: flashed firmware does not match preset '%s':" % preset)
            for pb in problems:
                print("        %s" % pb)
            print("      Refusing to capture. Build and flash it first:")
            print("        .\\mr.bat build pico %s serial=ON" % preset)
            return
        print("        firmware matches %s" % preset)

        ser.reset_input_buffer()
        ser.write(b"SHOT\n")
        ser.flush()

        header = ser.readline()
        while header and not header.startswith(b"MRSHOT1"):
            header = ser.readline()
        if not header:
            print("pico: no MRSHOT1 header; is MR_PICO_SCREENSHOT enabled?")
            return
        nbytes = int(header.split()[3])
        body = ser.read(nbytes)
        blob = header + body

    shot = os.path.join(OUT, "pico.shot")
    with open(shot, "wb") as f:
        f.write(blob)
    rows.append(finish("pico", preset, shot, fields))


def parse_metrics(line):
    out = {}
    for tok in line.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


def finish(platform, label, shot_path, fields):
    row = {"platform": platform, "case": label}
    row.update(fields)
    try:
        with open(shot_path, "rb") as f:
            w, h, body = parse_shot(f.read())
        png = os.path.splitext(shot_path)[0] + ".png"
        write_png(png, rgb565_to_rgb888(body, w, h), w, h)
        os.remove(shot_path)
        row["image"] = os.path.basename(png)
        row["width"], row["height"] = str(w), str(h)
        print("        -> %s" % os.path.basename(png))
    except (OSError, ValueError) as exc:
        row["image"] = "FAILED: %s" % exc
        print("        capture failed: %s" % exc)
    return row


# --------------------------------------------------------------------------

def merge_rows(rows):
    """Fold this run's rows into whatever a previous run left behind.

    Each invocation captures one platform, so writing only the current rows
    means the report never shows more than the last thing captured -- and the
    whole point of it is the comparison across platforms. Rows are keyed by
    platform and case, so re-capturing a platform replaces its row rather than
    duplicating it, and the others survive.
    """
    prior = []
    csv_path = os.path.join(OUT, "report.csv")
    if os.path.exists(csv_path):
        try:
            with open(csv_path, "r", newline="") as f:
                prior = [dict(r) for r in csv.DictReader(f)]
        except (OSError, csv.Error):
            prior = []

    fresh = {(r.get("platform"), r.get("case")): r for r in rows}
    merged = []
    for r in prior:
        key = (r.get("platform"), r.get("case"))
        if key in fresh:
            merged.append(fresh.pop(key))
        else:
            # Drop rows whose image has since been deleted, so a cleared
            # capture directory does not leave broken links in the report.
            img = r.get("image", "")
            if img.endswith(".png") and not os.path.exists(
                    os.path.join(OUT, img)):
                continue
            merged.append(r)
    for r in rows:
        if (r.get("platform"), r.get("case")) in fresh:
            merged.append(r)
    return merged


def write_reports(rows):
    rows = merge_rows(rows)
    keys = []
    for r in rows:
        for k in r:
            if k not in keys:
                keys.append(k)

    with open(os.path.join(OUT, "report.csv"), "w", newline="") as f:
        wtr = csv.DictWriter(f, fieldnames=keys)
        wtr.writeheader()
        wtr.writerows(rows)

    with open(os.path.join(OUT, "report.md"), "w") as f:
        f.write("# Capture report\n\n")
        f.write("Generated %s\n\n" % time.strftime("%Y-%m-%d %H:%M:%S"))
        show = [k for k in ("platform", "case", "fps_avg", "fps", "sim_hz",
                            "sim_ticks", "frames", "cycles", "cpuUs",
                            "flushUs", "sentKB") if any(k in r for r in rows)]
        f.write("| " + " | ".join(show) + " |\n")
        f.write("|" + "|".join([" --- "] * len(show)) + "|\n")
        for r in rows:
            f.write("| " + " | ".join(str(r.get(k, "")) for k in show) + " |\n")
        f.write("\n")
        for r in rows:
            img = r.get("image", "")
            if img.endswith(".png"):
                f.write("### %s / %s\n\n![%s](%s)\n\n" %
                        (r["platform"], r["case"], r["case"], img))
        f.write("\n`sim_hz` is the number that should agree across platforms.\n"
                "Frame rate is expected to differ by orders of magnitude; the\n"
                "simulation rate should not, because the timestep is fixed.\n\n"
                "Rows accumulate across runs, so capturing one platform at a\n"
                "time builds the comparison up. Delete `capture/` to start\n"
                "over.\n")


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1

    os.makedirs(OUT, exist_ok=True)
    rows = []

    targets = ("raylib", "dos", "pico", "all")

    i = 1
    while i < len(argv):
        what = argv[i]
        if what == "all":
            # Everything, unattended: build and flash the Pico rather than
            # capturing whatever happens to be on it, since a stale image is
            # the one way this report can be quietly wrong.
            capture_raylib(rows)
            capture_dos(rows)
            capture_pico(rows, None, "stress-lace", flash=True)
        elif what == "raylib":
            capture_raylib(rows)
        elif what == "dos":
            capture_dos(rows)
        elif what == "pico":
            port = None
            preset = "stress-lace"
            flash = False
            while i + 1 < len(argv) and argv[i + 1] not in targets:
                i += 1
                arg = argv[i]
                if arg == "flash":
                    flash = True
                elif arg.upper().startswith("COM") or "/" in arg:
                    port = arg
                else:
                    preset = arg
            capture_pico(rows, port, preset, flash=flash)
        else:
            print("unknown target: %s" % what)
            return 1
        i += 1

    if not rows:
        print("nothing captured")
        return 1

    write_reports(rows)
    print("\nwrote %s" % os.path.join(OUT, "report.md"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
