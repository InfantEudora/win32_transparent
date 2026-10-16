"""
Names the places in a RELEASE build's crash report (core/CrashReport.cpp).

A release exe is stripped, so its report gives the failing thread's stack as module+offset:

    #0  chasm_nophysics_release.exe+0x1a2b3c

The linker's map of the same build - build/<exe>.map, written beside the exe by engine.mk (RLINK) and never
shipped - lists where every function starts. This reads both and prints each frame of the exe with the function it
falls in. Only the map of the very build that crashed is right: keep the map with any exe handed out.

    python tools/crash_map.py <crash_....txt> <the build's .map>
"""
import bisect
import os
import re
import sys


def read_map(path):
    base = 0x140000000
    symbols = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r"\s+0x([0-9a-fA-F]+)\s+__image_base__ = 0x([0-9a-fA-F]+)", line)
            if m:
                base = int(m.group(2), 16)
                continue
            #A function: its address and its name. Not an input section's line - address, size, object file.
            m = re.match(r"\s+0x([0-9a-fA-F]{8,16})\s+([^\s=].*?)\s*$", line)
            if m and "=" not in m.group(2) and not m.group(2).startswith("0x"):
                symbols.append((int(m.group(1), 16), m.group(2)))
    symbols.sort()
    return base, symbols


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    report, map_path = sys.argv[1], sys.argv[2]
    base, symbols = read_map(map_path)
    addresses = [a for a, _ in symbols]
    exe = os.path.splitext(os.path.basename(map_path))[0].lower() + ".exe"
    with open(report, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\r\n")
            m = re.match(r"\s*#(\d+)\s+(\S+)\+0x([0-9a-fA-F]+)", line)
            if not m or m.group(2).lower() != exe:
                print(line)
                continue
            at = base + int(m.group(3), 16)
            i = bisect.bisect_right(addresses, at) - 1
            name = "?" if i < 0 else "%s +0x%x" % (symbols[i][1], at - symbols[i][0])
            print("%s    %s" % (line, name))
    return 0


if __name__ == "__main__":
    sys.exit(main())
