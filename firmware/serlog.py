#!/usr/bin/env python3
"""Print the board's serial log for a while (no interactive monitor needed).
Usage: python firmware/serlog.py <seconds> [--reset] [--port /dev/cu.usbmodem1101]"""
import argparse, glob, sys, time
import serial

ap = argparse.ArgumentParser()
ap.add_argument("seconds", type=float)
ap.add_argument("--reset", action="store_true", help="restart the app first (RTS pulses EN)")
ap.add_argument("--port", default=(sorted(glob.glob("/dev/cu.usbmodem*")) or ["/dev/cu.usbmodem1101"])[0])
a = ap.parse_args()
s = serial.Serial(a.port, 115200, timeout=0.2)
if a.reset:
    s.dtr = False
    s.rts = True
    time.sleep(0.1)
    s.rts = False
end = time.time() + a.seconds
while time.time() < end:  # as it comes, so a long capture can be read while it runs
    chunk = s.read(4096)
    if chunk:
        sys.stdout.write(chunk.decode("utf-8", "replace"))
        sys.stdout.flush()
