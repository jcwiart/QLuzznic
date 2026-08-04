#!/usr/bin/env python3
"""Package a qcc-built QDOS executable into a .qlpak for Q-emuLator.

qcc/qld (xtc68) append an "XTcc" trailer (4 bytes 'XTcc' + 4-byte
big-endian dataspace) instead of Q-emuLator's own 30-byte
"]!QDOS File Header" prefix convention. This strips the trailer, reads
the dataspace from it, and re-packages the executable the way
Q-emuLator expects (see tomconte/ql docs/qemulator.md for the prefix
format this mirrors).

Usage: mkqlpak.py <name> [--dataspace N]
Expects <name>_bin, boot and <name>.QCF in the current directory;
produces <name>.qlpak.
"""
from __future__ import annotations

import struct
import sys
import zipfile
import argparse
import shutil
from pathlib import Path


def read_binary_and_dataspace(bin_path: Path, override: int | None) -> tuple[bytes, int]:
    data = bin_path.read_bytes()
    if data[-8:-4] == b"XTcc":
        dataspace = struct.unpack(">I", data[-4:])[0]
        code = data[:-8]
    else:
        if override is None:
            raise SystemExit(
                f"{bin_path}: no XTcc trailer found and no --dataspace given"
            )
        code = data
        dataspace = override
    if override is not None:
        dataspace = override
    return code, dataspace


def qdos_header(dataspace: int) -> bytes:
    hdr = bytearray(30)
    hdr[0:19] = b"]!QDOS File Header\x00"
    hdr[19] = 15  # header length in 16-bit words
    hdr[20:22] = struct.pack(">H", 1)  # file type 1 = executable
    hdr[22:26] = struct.pack(">I", dataspace)
    hdr[26:30] = b"\x00\x00\x00\x00"
    return bytes(hdr)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("--dataspace", type=int, default=None)
    args = ap.parse_args()
    name = args.name

    cwd = Path.cwd()
    bin_path = cwd / f"{name}_bin"
    boot_path = cwd / "boot"
    qcf_path = cwd / f"{name}.QCF"

    code, dataspace = read_binary_and_dataspace(bin_path, args.dataspace)
    header = qdos_header(dataspace)

    stage = cwd / "stage"
    if stage.exists():
        shutil.rmtree(stage)
    pkg_dir = stage / name
    pkg_dir.mkdir(parents=True)

    (pkg_dir / f"{name}_bin").write_bytes(header + code)

    boot_text = boot_path.read_text()
    boot_text = "\n".join(boot_text.splitlines()) + "\n"
    with open(pkg_dir / "boot", "w", newline="") as f:
        f.write(boot_text)

    qcf_text = qcf_path.read_text()
    qcf_text = "\r\n".join(qcf_text.splitlines()) + "\r\n"
    with open(stage / f"{name}.QCF", "w", newline="") as f:
        f.write(qcf_text)

    qlpak_path = cwd / f"{name}.qlpak"
    if qlpak_path.exists():
        qlpak_path.unlink()

    with zipfile.ZipFile(qlpak_path, "w", zipfile.ZIP_DEFLATED) as zf:
        for p in sorted(stage.rglob("*")):
            if p.is_file():
                zf.write(p, p.relative_to(stage).as_posix())

    shutil.rmtree(stage)
    print(f"Built {qlpak_path.name} (dataspace {dataspace} bytes)")


if __name__ == "__main__":
    main()
