#!/usr/bin/env python3
# Runs the emulator build (tools/qemu/qemu.ini, env qemu28) in Espressif's
# QEMU on Claude's side (Linux): a FAT32 card image with the game folders
# (scratch space - never the repo) and a tap script, the firmware padded
# to 4 MB, the serial log kept. Needs qemu-system-xtensa (Espressif's
# build, e.g. esp-develop-9.2.2-20250817), mkfs.fat and mtools.
#
#   run_qemu.py --qemu /path/qemu-system-xtensa --game "/scratch/Pool of Radiance" \
#       [--taps taps.txt] [--seconds 120] [--out dir] [--s3]
#
# --s3: the ESP32-S3 board's firmware (env qemus3) in QEMU's ESP32-S3 with
# 8 MB PSRAM; the card is a read-only FAT image in the flash's "card"
# partition (tools/qemu/qemu_s3.csv), so nothing can be saved there.
#
# TAPS.TXT: "x y wait_ms" a line (screen pixels; the next tap after the
# wait), "mem [wait_ms]" prints the heap, "panic" crashes (the
# crash line in the Restarts log), '#' comments. The firmware prints
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
ap.add_argument("--s3", action="store_true", help="the ESP32-S3 build (env qemus3)")
ap.add_argument("--env", help="the emulator env (default qemus3 / qemu28; qemus3_50 = the 5.0\" S3 board, with --s3)")
a = ap.parse_args()

os.makedirs(a.out, exist_ok=True)
env_name = a.env or ("qemus3" if a.s3 else "qemu28")
fw = os.path.join(repo, ".pio", "qemu", env_name, "firmware.factory.bin")
if not os.path.exists(fw):
    sys.exit("build first: pio run -c tools/qemu/qemu.ini -e " + env_name)
flash = os.path.join(a.out, "flash.bin")
data = open(fw, "rb").read()
efuse = os.path.join(a.out, "efuse.bin")
env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
card = os.path.join(a.out, "sd.img")


def make_card(path, size, extra):
    with open(path, "wb") as f:
        f.truncate(size)
    subprocess.run(["mkfs.fat"] + extra + [path], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["mmd", "-i", path, "::GOLDBOX", "::GOLDBOX/_QEMU"], check=True, env=env)
    for g in a.game:
        subprocess.run(["mcopy", "-s", "-i", path, g, "::GOLDBOX/"], check=True, env=env)
    if a.taps:
        subprocess.run(["mcopy", "-i", path, a.taps, "::GOLDBOX/_QEMU/TAPS.TXT"], check=True, env=env)


if a.s3:
    card_at, card_size = 0x310000, 0xCE0000         # (tools/qemu/qemu_s3.csv)
    make_card(card, card_size, ["-S", "4096"])
    img = bytearray(b"\xff" * (16 * 1024 * 1024))
    img[0:len(data)] = data
    img[card_at:card_at + card_size] = open(card, "rb").read()
    open(flash, "wb").write(img)
    open(efuse, "wb").write(b"\0" * 1024)
else:
    open(flash, "wb").write(data + b"\xff" * (4 * 1024 * 1024 - len(data)))
    open(efuse, "wb").write(b"\0" * 124)
    make_card(card, a.card_mb * 1024 * 1024, ["-F", "32"])

log = os.path.join(a.out, "serial.log")
if a.s3:
    cmd = [a.qemu, "-nographic", "-machine", "esp32s3", "-m", "8M",
           "-global", "driver=ssi_psram,property=is_octal,value=true",
           "-drive", "file=%s,if=mtd,format=raw" % flash,
           "-drive", "file=%s,if=none,format=raw,id=efuse" % efuse,
           "-global", "driver=nvram.esp32s3.efuse,property=drive,value=efuse",
           "-serial", "file:%s" % log, "-monitor", "none"]
else:
    cmd = [a.qemu, "-nographic", "-machine", "esp32",
           "-drive", "file=%s,if=mtd,format=raw" % flash,
           "-drive", "file=%s,if=none,format=raw,id=efuse" % efuse,
           "-global", "driver=nvram.esp32.efuse,property=drive,value=efuse",
           "-drive", "file=%s,if=sd,format=raw" % card,
           "-serial", "file:%s" % log, "-monitor", "none"]
# (Espressif's QEMU now and then panics at boot with a cache error before
# the firmware's first line: tried again, 8 times at most)
for attempt in range(8):
    try:
        subprocess.run(cmd, timeout=a.seconds, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        pass
    text = open(log, "rb").read().decode("latin-1")
    if "CYD BYOG" in text:
        break
    print("(boot panic in QEMU - again)")
print(text)
low = [l for l in text.splitlines() if "[qemu]" in l and "largest block" in l]
if low:
    sizes = [int(l.split("largest block")[1].split(",")[0]) for l in low]
    print("lowest largest block: %d" % min(sizes))
