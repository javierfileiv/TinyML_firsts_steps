#!/usr/bin/env python3
"""Host-side capture tool for the edge_ai data logger firmware.

Reads CSV windows framed by BEGIN/END markers from the serial console
and stores each capture as dataset/<class_name>/sample_XXX.csv.

Pressing Space waits the settle delay, then sends the capture trigger
byte to the board. With --watch, detections are printed live and no
dataset file is ever written.
"""

import argparse
import re
import sys
import termios
import threading
import time
import tty
from pathlib import Path

import serial

CSV_LINE = re.compile(
    r"^-?\d+\.\d+(,-?\d+\.\d+){5}$"
)

# Expected window length, must match CAPTURE_WINDOW_SIZE in capture.h
WINDOW_ROWS = 256


def next_sample_path(out_dir: Path, class_name: str) -> Path:
    class_dir = out_dir / class_name
    class_dir.mkdir(parents=True, exist_ok=True)

    index = 0
    for existing in class_dir.glob("sample_*.csv"):
        try:
            index = max(index, int(existing.stem.split("_")[1]) + 1)
        except (ValueError, IndexError):
            continue

    return class_dir / f"sample_{index:03d}.csv"


def space_forwarder(port: serial.Serial, stop: threading.Event,
                    delay: float) -> None:
    """Wait the settle delay on Space, then send the trigger byte.

    Runs on stdin in cbreak mode so a keypress never needs Enter
    """
    while not stop.is_set():
        ch = sys.stdin.read(1)
        if ch == " ":
            print(f"[space] capture in {delay:g} s, hands off")
            time.sleep(delay)
            port.write(b" ")
            print("[space] trigger sent")
        elif ch == "w":
            port.write(b"w")
            print("[w] toggled watch mode on the board")
        elif ch == "q":
            stop.set()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-p", "--port", required=True,
                        help="serial port, e.g. /dev/ttyUSB0 or /dev/ttyACM0")
    parser.add_argument("-b", "--baud", type=int, default=115200)
    parser.add_argument("-c", "--class-name",
                        help="gesture class label, e.g. A, B or IDLE "
                             "(required unless --watch)")
    parser.add_argument("-o", "--out", default="dataset",
                        help="dataset root directory (default: dataset)")
    parser.add_argument("-d", "--delay", type=float, default=0.4,
                        help="settle delay before a capture starts "
                             "(seconds, default 0.4)")
    parser.add_argument("-w", "--watch", action="store_true",
                        help="show detections only, never write dataset files")
    args = parser.parse_args()

    if not args.watch and not args.class_name:
        parser.error("-c/--class-name is required unless --watch is set")

    out_dir = Path(args.out)
    count = 0

    with serial.Serial(args.port, args.baud, timeout=1) as port:
        forwarder = None
        old_attrs = None
        stop = threading.Event()

        if sys.stdin.isatty():
            old_attrs = termios.tcgetattr(sys.stdin)
            tty.setcbreak(sys.stdin.fileno())
            forwarder = threading.Thread(
                target=space_forwarder,
                args=(port, stop, args.delay),
                daemon=True)
            forwarder.start()

        print(f"Listening on {args.port} @ {args.baud}, "
              + (f"watch mode, no files written. "
                 if args.watch else f"class '{args.class_name}' -> {out_dir}/. ")
              + "Space = capture, q or Ctrl-C = quit.")
        try:
            while not stop.is_set():
                line = port.readline().decode("ascii",
                                              errors="replace").strip()

                if line != "BEGIN":
                    if line:
                        # Detections get a distinctive prefix so they
                        # stand out from the plain firmware chatter
                        prefix = ("det:" if line.startswith("inference:")
                                  or line.startswith(">>>") else "fw:")
                        print(f"{prefix} {line}")
                    continue

                rows = []
                while True:
                    line = port.readline().decode("ascii",
                                                  errors="replace").strip()
                    if line == "END":
                        break
                    if CSV_LINE.match(line):
                        rows.append(line)
                    else:
                        print(f"warning: dropped malformed line '{line}'",
                              file=sys.stderr)

                if len(rows) != WINDOW_ROWS:
                    print(f"warning: incomplete window ({len(rows)} lines), "
                          f"discarded", file=sys.stderr)
                    continue

                if args.watch:
                    print(f"[watch] window of {len(rows)} rows, not saved")
                    continue

                path = next_sample_path(out_dir, args.class_name)
                path.write_text("\n".join(rows) + "\n")
                count += 1
                print(f"[{count}] saved {path}")
        except KeyboardInterrupt:
            pass
        finally:
            stop.set()
            if forwarder is not None:
                forwarder.join(timeout=0.5)
                termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_attrs)
            print(f"\nDone, {count} capture(s) saved.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
