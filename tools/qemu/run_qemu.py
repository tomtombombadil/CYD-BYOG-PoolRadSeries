#!/usr/bin/env python3
# Runs the emulator build (tools/qemu/qemu.ini, env qemu28) in Espressif's
# QEMU on Claude's side (Linux): a FAT32 card image with the game folders
# (scratch space - never the repo) and a tap script, the firmware padded
# to 4 MB, the serial log kept. Needs qemu-system-xtensa (Espressif's
# build, e.g. esp-develop-9.2.2-20250817), mkfs.fat and mtools.
#
#   run_qemu.py --qemu /path/qemu-system-xtensa --game "/scratch/Pool of Radiance" \
#       [--taps taps.txt] [--seconds 120] [--out dir]
#
# TAPS.TXT: "x y wait_ms" a line (screen pixels; the next tap after the
# wait), "mem [wait_ms]" prints the heap, '#' comments. The firmware prints
# "[qemu] ... heap free N, largest block N, min free N" after each step.
import argparse
import os
import shutil
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.dirname(os.path.dirname(here))

ap = argparse.ArgumentParser()
ap.add_argument("--qemu", required=True)
ap.add_argument("--game", action="append", default=[], help="a game folder to put in /GOLDBOX (repeatable)")
ap.add_argument("--taps", help="the tap script (TAPS.TXT)")
ap.add_argument("--seconds", type=int, default=120)
ap.add_argument("--out", default="/tmp/claude-0/qemu/run")
ap.add_argument("--card-mb", type=int, default=128)
a = ap.parse_args()

os.makedirs(a.out, exist_ok=True)
fw = os.path.join(repo, ".pio", "qemu", "qemu28", "firmware.factory.bin")
if not os.path.exists(fw):
    sys.exit("build first: pio run -c tools/qemu/qemu.ini -e qemu28")
flash = os.path.join(a.out, "flash.bin")
data = open(fw, "rb").read()
open(flash, "wb").write(data + b"\xff" * (4 * 1024 * 1024 - len(data)))
efuse = os.path.join(a.out, "efuse.bin")
open(efuse, "wb").write(b"\0" * 124)

card = os.path.join(a.out, "sd.img")
with open(card, "wb") as f:
    f.truncate(a.card_mb * 1024 * 1024)
subprocess.run(["mkfs.fat", "-F", "32", card], check=True, stdout=subprocess.DEVNULL)
env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
subprocess.run(["mmd", "-i", card, "::GOLDBOX", "::GOLDBOX/_QEMU"], check=True, env=env)
for g in a.game:
    subprocess.run(["mcopy", "-s", "-i", card, g, "::GOLDBOX/"], check=True, env=env)
if a.taps:
    subprocess.run(["mcopy", "-i", card, a.taps, "::GOLDBOX/_QEMU/TAPS.TXT"], check=True, env=env)

log = os.path.join(a.out, "serial.log")
cmd = [a.qemu, "-nographic", "-machine", "esp32",
       "-drive", "file=%s,if=mtd,format=raw" % flash,
       "-drive", "file=%s,if=none,format=raw,id=efuse" % efuse,
       "-global", "driver=nvram.esp32.efuse,property=drive,value=efuse",
       "-drive", "file=%s,if=sd,format=raw" % card,
       "-serial", "file:%s" % log, "-monitor", "none"]
try:
    subprocess.run(cmd, timeout=a.seconds, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
except subprocess.TimeoutExpired:
    pass
text = open(log, "rb").read().decode("latin-1")
print(text)
low = [l for l in text.splitlines() if "[qemu]" in l and "largest block" in l]
if low:
    sizes = [int(l.split("largest block")[1].split(",")[0]) for l in low]
    print("lowest largest block: %d" % min(sizes))
