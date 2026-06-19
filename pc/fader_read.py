#!/usr/bin/env python3
"""
LiberArk68 fader reader — Phase 2 bring-up / verification tool.

Opens the dongle's zmk-hid-io interface (HID usage page 0xFF0C) and prints
incoming report bytes whenever they change, so you can confirm both faders
reach the PC and see which byte index maps to the left vs right slider.

Setup (Windows):
    py -m pip install hidapi
    py pc\\fader_read.py

If nothing shows up, run with --list to see every HID interface the dongle
exposes and pass --page / --path explicitly.
"""
import argparse
import sys
import time

try:
    import hid
except ImportError:
    sys.exit("Missing dependency: run `py -m pip install hidapi`")

HID_IO_USAGE_PAGE = 0xFF0C


def list_devices():
    for d in hid.enumerate():
        up = d.get("usage_page", 0)
        print(
            f"  page=0x{up:04X} usage=0x{d.get('usage', 0):04X} "
            f"vid=0x{d['vendor_id']:04X} pid=0x{d['product_id']:04X} "
            f"| {d.get('product_string')} | {d['path']}"
        )


def pick_path(page):
    matches = [d for d in hid.enumerate() if d.get("usage_page", 0) == page]
    if not matches:
        return None
    if len(matches) > 1:
        print(f"Multiple 0x{page:04X} interfaces; using the first:")
        for d in matches:
            print(f"  {d['path']}")
    return matches[0]["path"]


def main():
    ap = argparse.ArgumentParser(description="Read LiberArk68 faders over HID")
    ap.add_argument("--list", action="store_true", help="list all HID interfaces and exit")
    ap.add_argument("--page", type=lambda x: int(x, 0), default=HID_IO_USAGE_PAGE,
                    help="HID usage page to open (default 0xFF0C)")
    ap.add_argument("--path", help="open this exact HID path (from --list)")
    args = ap.parse_args()

    if args.list:
        list_devices()
        return

    path = args.path.encode() if args.path else pick_path(args.page)
    if not path:
        sys.exit(f"No HID interface on usage page 0x{args.page:04X}. "
                 f"Try --list to see what the dongle exposes.")

    dev = hid.device()
    dev.open_path(path)
    dev.set_nonblocking(True)
    print(f"Opened {path}. Move the faders — changed bytes print below. Ctrl+C to stop.\n")

    last = None
    try:
        while True:
            data = dev.read(64)
            if data and data != last:
                changed = []
                if last and len(last) == len(data):
                    changed = [i for i, (a, b) in enumerate(zip(last, data)) if a != b]
                marks = "".join("^" if i in changed else " " for i in range(len(data)))
                print("bytes: " + " ".join(f"{b:3d}" for b in data))
                print("       " + " ".join(f"{'^^^' if i in changed else '   '}"
                                           for i in range(len(data))))
                last = data
            time.sleep(0.01)
    except KeyboardInterrupt:
        pass
    finally:
        dev.close()


if __name__ == "__main__":
    main()
