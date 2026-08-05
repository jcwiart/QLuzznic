#!/usr/bin/env python3
"""Package a qcc-built QDOS executable + boot file into a bootable .mdv
microdrive image (QLAY format) for real QL hardware or the MiSTer QL core.

Re-implements the relevant subset of xXorAa/mdvtool (create/import/write)
in pure Python -- that tool needs libzip to build, which we don't need
since we only ever import two plain files (no zip_import). The on-disk
sector layout, checksums and allocation algorithm below are ported
byte-for-byte from mdvtool.c/mdv.h (https://github.com/xXorAa/mdvtool),
which is itself the tool bundled with MiSTer's own QL core binaries --
so this produces the same output a compiled mdvtool would.

Format notes (see mdv.h):
- image = 255 sectors * 686 bytes (28-byte header + 658-byte body) =
  174930 bytes, matching the QL_MiSTer core's documented MDV size.
- hdr.rnd/csum and sector bh_csum/data_csum are plain 16-bit checksums
  used only for the tool's own self-consistency checks -- mdvtool.c
  assigns them via a raw C struct field (no SWAP16), i.e. host-native
  (little-endian on the x86/ARM machines mdvtool normally runs on) --
  reproduced here as '<H'.
- file_t fields (the QDOS-visible directory entry) ARE explicitly
  byte-swapped in mdvtool.c before storage, i.e. genuine big-endian
  68000 byte order -- reproduced here as '>' struct formats.
- QL SuperBASIC programs are plain ASCII text on disk (no tokenised
  save format, unlike contemporary 8-bit BASICs), so the "boot" file
  is imported byte-for-byte like any other data file.

Usage: mkmdv.py <name> [--medium-name NAME]
Expects <name>_bin (raw qcc/qld output, XTcc trailer intact) and boot
in the current directory; produces <name>.mdv.
"""
from __future__ import annotations

import argparse
import random
import struct
import sys
from pathlib import Path

MAX_SECTORS = 255
HDR_SIZE = 28
SEC_SIZE = 658
ENTRY_SIZE = HDR_SIZE + SEC_SIZE  # 686
FILE_T_SIZE = 64
DATA_SIZE = 512

# offsets within a sector_t (relative to the 658-byte body, i.e. after HDR_SIZE)
SEC_FILE = 12
SEC_BLOCK = 13
SEC_BH_CSUM = 14
SEC_DATA_PRE = 16
SEC_DATA = 24
SEC_DATA_CSUM = 24 + DATA_SIZE


def csum(data: bytes) -> int:
    v = 0x0F0F
    for b in data:
        v = (v + b) & 0xFFFF
    return v


class MdvImage:
    def __init__(self) -> None:
        self.entries = [bytearray(ENTRY_SIZE) for _ in range(MAX_SECTORS)]
        # files[file_index][block] = physical sector index, or None if unset
        self.files: list[list[int | None]] = [[None] * 256 for _ in range(256)]
        self._create()

    # ---- low level accessors -------------------------------------------------
    def _hdr(self, i: int) -> bytearray:
        return self.entries[i][0:HDR_SIZE]

    def _sec(self, i: int) -> memoryview:
        return memoryview(self.entries[i])[HDR_SIZE:]

    def _set_hdr_csum(self, i: int) -> None:
        e = self.entries[i]
        e[26:28] = struct.pack("<H", csum(e[12:26]))  # sum(&hdr.ff, 14)

    def _set_bh_csum(self, i: int) -> None:
        sec = self._sec(i)
        off = HDR_SIZE + SEC_BH_CSUM
        self.entries[i][off:off + 2] = struct.pack(
            "<H", csum(bytes(sec[SEC_FILE:SEC_FILE + 2]))
        )

    def _set_data_csum(self, i: int) -> None:
        sec = self._sec(i)
        off = HDR_SIZE + SEC_DATA_CSUM
        self.entries[i][off:off + 2] = struct.pack(
            "<H", csum(bytes(sec[SEC_DATA:SEC_DATA + DATA_SIZE]))
        )

    def _map_entry(self, slot: int) -> tuple[int, int]:
        data = self._sec(0)[SEC_DATA:SEC_DATA + DATA_SIZE]
        return data[2 * slot], data[2 * slot + 1]

    def _set_map_entry(self, slot: int, file_index: int, block: int) -> None:
        off = HDR_SIZE + SEC_DATA + 2 * slot
        self.entries[0][off] = file_index
        self.entries[0][off + 1] = block

    # ---- creation --------------------------------------------------------
    def _create(self) -> None:
        rnd = random.randint(0, 0xFFFF)
        medium_name = b"MD        "[:10]
        for c in range(MAX_SECTORS):
            e = self.entries[c]
            e[10] = 0xFF
            e[11] = 0xFF  # header preamble tail
            e[12] = 0xFF  # ff marker
            e[13] = c  # snum
            e[14:24] = medium_name
            e[24:26] = struct.pack("<H", rnd)

            sec_off = HDR_SIZE
            e[sec_off + 10] = 0xFF
            e[sec_off + 11] = 0xFF  # block header preamble tail
            e[sec_off + SEC_FILE] = 0xFD  # free by default
            e[sec_off + SEC_BLOCK] = 0
            data_pre = sec_off + SEC_DATA_PRE
            e[data_pre + 6] = 0xFF
            e[data_pre + 7] = 0xFF  # data preamble tail
            for d in range(DATA_SIZE):
                e[sec_off + SEC_DATA + d] = 0x00
            for d in range(120):
                e[ENTRY_SIZE - 120 + d] = 0x5A  # extra_byte filler

            if c == 0:  # sector 0: the sector mapping table
                e[sec_off + SEC_FILE] = 0x80
                data = e[sec_off + SEC_DATA:sec_off + SEC_DATA + DATA_SIZE]
                for d in range(0, DATA_SIZE, 2):
                    data[d] = 0xFD
                    data[d + 1] = 0x00
                data[0] = 0xF8
                data[2] = 0x00
                e[sec_off + SEC_DATA:sec_off + SEC_DATA + DATA_SIZE] = data
                self.files[0x80][0] = 0
            if c == 1:  # sector 1: root directory, block 0
                e[sec_off + SEC_FILE] = 0x00
                # directory length = 1 entry (64 bytes) stored at data[2:4]
                e[sec_off + SEC_DATA + 2] = 0x00
                e[sec_off + SEC_DATA + 3] = 0x40
                self.files[0][0] = 1

            self._set_hdr_csum(c)
            self._set_bh_csum(c)
            self._set_data_csum(c)

    def rename(self, name: str) -> None:
        lname = (name[:10]).ljust(10)[:10].encode("ascii")
        for i in range(MAX_SECTORS):
            if self.entries[i][12] == 0xFF:
                self.entries[i][14:24] = lname
                self._set_hdr_csum(i)

    # ---- directory helpers -------------------------------------------------
    def _dir_sector_for_entry(self, entry_index: int) -> int:
        block = entry_index // 8
        s = self.files[0][block]
        if s is None:
            raise RuntimeError(f"missing directory sector for block {block}")
        return s

    def _dir_length(self) -> int:
        s = self._dir_sector_for_entry(0)
        data = self._sec(s)[SEC_DATA:SEC_DATA + 4]
        return struct.unpack(">I", bytes(data))[0]

    def _set_dir_length(self, length: int) -> None:
        s = self._dir_sector_for_entry(0)
        off = HDR_SIZE + SEC_DATA
        self.entries[s][off:off + 4] = struct.pack(">I", length)

    def _get_free_block(self, file_index: int, block: int, last_block: int) -> int:
        for i in range(MAX_SECTORS):
            s = last_block - 13 - i
            if s < 0:
                s += MAX_SECTORS
            hi, _lo = self._map_entry(s)
            if hi == 0xFD:
                break
        else:
            raise RuntimeError("MDV image full")
        self._set_map_entry(s, file_index, block)
        self.files[file_index][block] = s
        return s

    # ---- file import -------------------------------------------------------
    def import_file(self, name: str, data: bytes) -> None:
        data = data.replace(b"flp1_", b"mdv1_")

        ftype = 0
        info0 = 0
        if data[-8:-4] == b"XTcc":
            ftype = 1
            info0 = struct.unpack(">I", data[-4:])[0]
            data = data[:-8]

        total_length = len(data) + FILE_T_SIZE

        file_t = bytearray(FILE_T_SIZE)
        struct.pack_into(">I", file_t, 0, total_length)  # length
        file_t[4] = 0  # access
        file_t[5] = ftype  # type
        struct.pack_into(">I", file_t, 6, info0)  # info[0]
        struct.pack_into(">I", file_t, 10, 0)  # info[1]
        name_bytes = name.encode("ascii")
        struct.pack_into(">H", file_t, 14, len(name_bytes))  # name_len
        file_t[16:16 + len(name_bytes)] = name_bytes
        # last_update/version/last_backup left as 0

        entries = self._dir_length() // FILE_T_SIZE
        file_index = entries
        if (entries & 7) == 7:
            raise RuntimeError(
                "directory extension not implemented (only needed past 7 files)"
            )

        dir_sector = self._dir_sector_for_entry(file_index)
        off = HDR_SIZE + SEC_DATA + FILE_T_SIZE * (file_index & 7)
        self.entries[dir_sector][off:off + FILE_T_SIZE] = file_t
        self._set_bh_csum(dir_sector)
        self._set_data_csum(dir_sector)
        self._set_dir_length((entries + 1) * FILE_T_SIZE)
        self._set_bh_csum(self._dir_sector_for_entry(0))
        self._set_data_csum(self._dir_sector_for_entry(0))

        # Block 0 holds the 64-byte header *plus* up to 448 bytes of real
        # data (512 bytes total); later blocks hold up to 512 bytes of
        # real data each. The header doesn't eat into block 0's data
        # budget -- it's additional, not shared.
        last_block = 0
        block = 0
        pos = 0
        remaining_data = len(data)
        first = True
        while first or remaining_data > 0:
            capacity = (DATA_SIZE - FILE_T_SIZE) if block == 0 else DATA_SIZE
            chunk = min(capacity, remaining_data)

            s = self._get_free_block(file_index, block, last_block)
            off = HDR_SIZE + SEC_DATA
            if block == 0:
                self.entries[s][off:off + FILE_T_SIZE] = file_t
                self.entries[s][off + FILE_T_SIZE:off + FILE_T_SIZE + chunk] = (
                    data[pos:pos + chunk]
                )
            else:
                self.entries[s][off:off + chunk] = data[pos:pos + chunk]
            self.entries[s][HDR_SIZE + SEC_FILE] = file_index
            self.entries[s][HDR_SIZE + SEC_BLOCK] = block
            self._set_bh_csum(s)
            self._set_data_csum(s)
            # sector-0 mapping table sector itself just changed -> refresh it
            self._set_bh_csum(0)
            self._set_data_csum(0)

            last_block = s
            block += 1
            pos += chunk
            remaining_data -= chunk
            first = False

        print(f"Imported '{name}' ({total_length} bytes incl. header)")

    # ---- output -------------------------------------------------------------
    def write(self, path: Path) -> None:
        order = [0] + list(range(MAX_SECTORS - 1, 0, -1))
        for i in order:
            if self.entries[i][12] == 0xFF:
                self._set_data_csum(i)
        with open(path, "wb") as f:
            for i in order:
                f.write(self.entries[i])


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("--medium-name", default=None)
    args = ap.parse_args()
    name = args.name

    cwd = Path.cwd()
    bin_path = cwd / f"{name}_bin"
    boot_path = cwd / "boot"

    mdv = MdvImage()
    mdv.rename(args.medium_name or name.upper())
    mdv.import_file(name[:8] + "_bin", bin_path.read_bytes())
    mdv.import_file("boot", boot_path.read_bytes())

    out_path = cwd / f"{name}.mdv"
    mdv.write(out_path)
    size = out_path.stat().st_size
    print(f"Built {out_path.name} ({size} bytes)")
    if size != MAX_SECTORS * ENTRY_SIZE:
        sys.exit(f"ERROR: expected {MAX_SECTORS * ENTRY_SIZE} bytes, got {size}")


if __name__ == "__main__":
    main()
