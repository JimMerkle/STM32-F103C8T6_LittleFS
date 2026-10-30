# test_history.py
# Drive the Blue Pill command line over a serial port and check history / line editing.
#
# Each test types an edited single-word "command" and presses Enter.  The firmware answers
#   Command "<word>" not found
# so the word it echoes back shows exactly what the line editor produced.
#
# Usage:  python test_history.py [COM7]
# Close Tera Term (or any other program holding the port) first.
import re
import sys
import time

import serial  # pip install pyserial

UP, DOWN, RIGHT, LEFT = b"\x1b[A", b"\x1b[B", b"\x1b[C", b"\x1b[D"
HOME, END, DEL = b"\x1b[1~", b"\x1b[4~", b"\x1b[3~"
BS = b"\x08"

port = serial.Serial(sys.argv[1] if len(sys.argv) > 1 else "COM7", 115200, timeout=0.2)


def send(data, gap=0.0):
    if gap:
        for b in data:
            port.write(bytes([b]))
            time.sleep(gap)
    else:
        port.write(data)


def run(keys, gap=0.0):
    """Type keys, press Enter, return the word the firmware echoed back."""
    port.reset_input_buffer()
    send(keys, gap)
    send(b"\r")
    time.sleep(0.4)
    out = port.read(4096).decode(errors="replace")
    m = re.findall(r'Command "(.*?)" not found', out)
    return (m[-1] if m else None), out


results = []


def check(name, keys, expect, gap=0.0):
    got, out = run(keys, gap)
    ok = got == expect
    results.append(ok)
    print(f"{'PASS' if ok else 'FAIL'}  {name}: expected {expect!r}, got {got!r}")
    if not ok:
        print("      raw:", repr(out[-200:]))


send(b"\r")
time.sleep(0.3)
port.read(4096)

check("plain command", b"t3", "t3")
check("up arrow recalls last", UP, "t3")
check("split escape sequence (bytes 30ms apart)", UP, "t3", gap=0.03)

# 12 more entries - history depth is 10, so the ring wraps and keeps h5..h14
for n in range(3, 15):
    run(b"h%d" % n)
check("up x20 stops at oldest kept (wrapped ring)", UP * 20, "h5")
# newest first is now: h5 (just run), h14, h13
check("up x3 then down x1", UP * 3 + DOWN, "h14")
# newest first is now: h14, h5.  Re-running h14 must not store a 2nd copy,
# so the 2nd Up reaches h5 (a duplicate would show h14 again)
check("duplicate of newest not saved twice", UP + b"\r" + UP + UP, "h5")

check("down past newest restores draft", b"d77" + UP + UP + DOWN + DOWN, "d77")
check("left + insert", b"ab9" + LEFT + b"2", "ab29")
check("home, right, delete", b"xy19" + HOME + RIGHT * 2 + DEL + b"5", "xy59")
check("backspace mid-line", b"k123" + LEFT * 2 + BS, "k23")  # cursor between 1 and 2, deletes the 1
check("end key", b"e1" + HOME + END + b"0", "e10")
check("xterm Home/End (ESC [ H / F)", b"f2" + b"\x1b[H" + b"\x1b[F" + b"0", "f20")

print(f"\n{sum(results)}/{len(results)} passed")
port.close()
sys.exit(0 if all(results) else 1)
