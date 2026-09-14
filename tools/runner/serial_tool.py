#!/usr/bin/env python3
"""Watch or reset an ESP32-S3 on its USB Serial/JTAG port. Runs on the runner.

    serial_tool.py PORT monitor [--seconds N] [--reset]
    serial_tool.py PORT reset

The port is opened with DTR high and RTS low, which leaves the chip running.
A plain open on Linux resets it: the kernel raises DTR and RTS, and pyserial
then drops DTR while RTS is still high. The monitor reopens the port after
the device reboots. NVS Manager never logs values, but other firmware (such
as Launcher) may, so lines that look like they carry a credential are hidden.
"""
import argparse
import sys
import time

import serial

# Launcher, for one, logs the names of saved networks.
SECRET_HINTS = ("pass", "pwd", "psk", "ssid")


def open_port(port):
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.2
    s.dtr = True
    s.rts = False
    s.open()
    return s


def reset(s):
    s.dtr = False
    s.rts = True
    time.sleep(0.1)
    s.rts = False
    s.dtr = True


def show(raw):
    line = raw.decode(errors="replace").rstrip("\r")
    if any(h in line.lower() for h in SECRET_HINTS):
        line = "<line hidden: may contain a credential>"
    print(line, flush=True)


def monitor(port, seconds, do_reset):
    end = None if seconds is None else time.time() + seconds
    expired = lambda: end is not None and time.time() >= end
    s = open_port(port)
    if do_reset:
        reset(s)
    pending = b""
    while not expired():
        try:
            pending += s.read(4096)
        except (serial.SerialException, OSError):
            print("<device left USB, waiting for it>", flush=True)
            s.close()
            while not expired():
                time.sleep(0.1)
                try:
                    s = open_port(port)
                    break
                except (serial.SerialException, OSError):
                    pass
            continue
        while b"\n" in pending:
            raw, pending = pending.split(b"\n", 1)
            show(raw)
    if pending:
        show(pending)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    sub = ap.add_subparsers(dest="command", required=True)
    mon = sub.add_parser("monitor")
    mon.add_argument("--seconds", type=float)
    mon.add_argument("--reset", action="store_true", help="reset the device once the port is open")
    sub.add_parser("reset")
    args = ap.parse_args()

    try:
        if args.command == "monitor":
            monitor(args.port, args.seconds, args.reset)
        else:
            reset(open_port(args.port))
    except KeyboardInterrupt:
        pass
    except (serial.SerialException, OSError) as e:
        sys.exit(f"serial: {e}")


if __name__ == "__main__":
    main()
