#!/usr/bin/env python3
"""Send a mode command to the Bluepad32 console and print its response."""

import sys
import time

import serial


if len(sys.argv) != 2 or sys.argv[1] not in {"config", "play"}:
    raise SystemExit(f"usage: {sys.argv[0]} config|play")

with serial.Serial("/dev/ttyACM0", 115200, timeout=0.2) as device:
    deadline = time.monotonic() + 12
    sent = False
    output = ""
    while time.monotonic() < deadline:
        data = device.read(4096)
        if data:
            text = data.decode("utf-8", errors="replace")
            print(text, end="", flush=True)
            output += text
        if not sent and ("bp32>" in output or time.monotonic() >= deadline - 9):
            device.write(f"mode {sys.argv[1]}\n".encode())
            device.flush()
            sent = True
