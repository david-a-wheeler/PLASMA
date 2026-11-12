#!/usr/bin/env python3
"""Write an XLOAD image as a C array, for XLOAD's first part.

Usage: tools/img2h.py IMAGE NAME > OUTPUT.h

Writes `static unsigned char NAME[] = { ... };` holding IMAGE's bytes,
which xload/xload.c includes as its second part (doc/xload.md,
section 4).
"""
import sys


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: tools/img2h.py IMAGE NAME")
    data = open(sys.argv[1], "rb").read()
    print("/* %s: %s, %d bytes, written by tools/img2h.py. */"
          % (sys.argv[2], sys.argv[1].split("/")[-1], len(data)))
    print("static unsigned char %s[] = {" % sys.argv[2])
    for i in range(0, len(data), 12):
        print("  " + " ".join("%d," % b for b in data[i:i + 12]))
    print("};")


if __name__ == "__main__":
    main()
