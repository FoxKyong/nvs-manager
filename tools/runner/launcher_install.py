#!/usr/bin/env python3
"""Install an app-only firmware image through Launcher's serial console.

Runs on the hardware runner, next to the USB port. The device has to boot
into Launcher's boot screen first, then this uses the console's
"flash firmware <name> <size>" command: Launcher answers "READY <size>",
takes the image in 2048-byte chunks that it acknowledges with
"ACK <written>/<size>", installs it into an OTA partition it creates, sets it
to boot and reboots. After that the new app's log is captured.

Launcher's bootloader starts Launcher only on power-on. Any other reset
(USB, esptool, software) starts the installed app again, and erasing
otadata does not help: the bootloader then picks the first OTA app. With an
app installed, use --wait-replug and unplug the USB cable and plug it back
in, with the device's own battery switched off.

Launcher writes its own NVS namespaces during this (app registry, settings,
BLE bond owner); this script never touches NVS itself.

    launcher_install.py PORT FIRMWARE [--name NAME] [--replace LABEL]
                        [--wait-replug] [--after SECONDS]
"""
import argparse
import os
import sys
import time

import serial

CHUNK = 2048
# Launcher logs the names of saved networks on boot.
SECRET_HINTS = ("pass", "pwd", "psk", "ssid")
BANNER = "Press the button to enter the Launcher!"


def log(line):
    # Launcher prints Wi-Fi related lines; keep anything that looks like a
    # credential out of the output.
    if any(h in line.lower() for h in SECRET_HINTS):
        line = "<line hidden: may contain a credential>"
    print(line, flush=True)


class Console:
    disconnected = False

    def __init__(self, port, attempts=40):
        self.s = serial.Serial()
        self.s.port = port
        self.s.baudrate = 115200
        self.s.timeout = 0.1
        # Linux raises DTR and RTS on open, and pyserial then applies DTR
        # before RTS. Dropping DTR while RTS is still high is the chip's
        # USB Serial/JTAG reset, so keep DTR high: the port opens without a
        # reset.
        self.s.dtr = True
        self.s.rts = False
        for attempt in range(attempts):
            try:
                self.s.open()
                break
            except serial.SerialException:
                if attempt == attempts - 1:
                    raise
                time.sleep(0.05)  # just enumerated; udev may not be done yet
        self.pending = b""

    def reset(self):
        self.s.dtr = False
        self.s.rts = True
        time.sleep(0.1)
        self.s.rts = False
        self.s.dtr = True

    def lines(self, seconds):
        end = time.time() + seconds
        while time.time() < end and not self.disconnected:
            try:
                self.pending += self.s.read(4096)
            except (serial.SerialException, OSError):
                # The chip's USB Serial/JTAG drops off the bus when it reboots.
                self.disconnected = True
            while b"\n" in self.pending:
                raw, self.pending = self.pending.split(b"\n", 1)
                yield raw.decode(errors="replace").rstrip("\r")

    def wait_for(self, prefix, seconds, fail=("ERR",)):
        for line in self.lines(seconds):
            log(line)
            if line.startswith(prefix):
                return line
            if line.startswith(fail):
                raise RuntimeError(line)
        raise TimeoutError(f"no '{prefix}' within {seconds} s")

    def send(self, text):
        self.s.write(text.encode() + b"\n")
        self.s.flush()


def wait_replug(port, seconds=600):
    print("waiting for the USB cable to be unplugged and plugged back in", flush=True)
    deadline = time.time() + seconds
    for present in (False, True):
        while os.path.exists(port) != present:
            if time.time() > deadline:
                raise TimeoutError("the device was not unplugged and plugged back in")
            time.sleep(0.02)
    print("port is back", flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("firmware")
    ap.add_argument("--name", default="NVS-Manager")
    ap.add_argument("--after", type=float, default=15, help="seconds of app log to capture")
    ap.add_argument("--replace", metavar="LABEL",
                    help="delete this app partition first; without it a reinstall adds another partition")
    ap.add_argument("--wait-replug", action="store_true",
                    help="wait for the cable to be unplugged and plugged in, for a power-on boot")
    args = ap.parse_args()

    image = open(args.firmware, "rb").read()
    if not image or image[0] != 0xE9:
        sys.exit("not an app-only ESP image (magic 0xE9 at offset 0)")

    if args.wait_replug:
        wait_replug(args.port)
        con = Console(args.port)
    else:
        con = Console(args.port)
        con.reset()

    # Enter the menu instead of waiting for the boot screen to time out.
    try:
        con.wait_for(BANNER, 10)
    except TimeoutError:
        raise RuntimeError("Launcher's boot screen did not appear; an installed app probably started instead "
                           "(Launcher shows only on power-on)")
    con.send("nav SelPress")
    con.wait_for("Type 'help' for Serial commands.", 30)

    if args.replace:
        con.send(f"partition delete {args.replace}")
        result = con.wait_for(("OK partition deleted", "ERR partition not found"), 30)
        print(f"replace {args.replace}: {result}", flush=True)

    con.send(f"flash firmware {args.name} {len(image)}")
    con.wait_for(f"READY {len(image)}", 30)
    start = time.time()
    written = 0
    while written < len(image):
        chunk = image[written:written + CHUNK]
        con.s.write(chunk)
        written += len(chunk)
        con.wait_for(f"ACK {written}/{len(image)}", 10)
    print(f"sent {written} B in {time.time() - start:.1f} s", flush=True)
    try:
        con.wait_for("OK flashed", 60)
    except TimeoutError:
        if not con.disconnected:
            raise
        print("device rebooted before 'OK flashed' was read", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, TimeoutError) as e:
        sys.exit(f"install failed: {e}")
