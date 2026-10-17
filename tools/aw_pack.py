#!/usr/bin/env python3
"""Build the game data pack of the Another World homebrew.

    python3 aw_pack.py AnotherWorld.aw /path/to/ANOTHER

The folder must hold the files of the PC DOS version (Another World /
Out of This World): MEMLIST.BIN and BANK01 .. BANK0D (any case).

The resources of the banks are unpacked here (ByteKiller, like bank.cpp of
the engine), so that the console uses them in place from its external flash:
nothing is copied to RAM. Pack format: src/port/aw_pack.c.

Only Python 3 is needed (no extra module).
"""
import argparse
import os
import struct
import sys

MAGIC = b"GWAW01\0\0"
ENTRY_NAME_LEN = 24

RT_SOUND, RT_MUSIC, RT_POLY_ANIM, RT_PALETTE, RT_BYTECODE, RT_POLY_CINEMATIC = range(6)


def find_file(folder, name):
    """Case insensitive lookup of `name` in `folder`."""
    want = name.lower()
    for entry in os.listdir(folder):
        if entry.lower() == want:
            return os.path.join(folder, entry)
    return None


def parse_memlist(data):
    entries = []
    pos = 0
    while pos + 20 <= len(data):
        (state, rtype, buf_ptr, unk4, rank, bank, bank_off, unkc, packed, unk10,
         size) = struct.unpack(">BBHHBBIHHHH", data[pos:pos + 20])
        if state == 0xFF:
            return entries
        entries.append(dict(state=state, type=rtype, rank=rank, bank=bank,
                            offset=bank_off, packed=packed, size=size))
        pos += 20
    raise ValueError("MEMLIST.BIN: no end marker")


class UnpackError(Exception):
    pass


def bytekiller_unpack(src, size):
    """Port of Bank::unpack() (src/raw/bank.cpp): the packed data is read
    backwards, 32 bit big endian words, the output is written backwards."""
    if len(src) < 12 or len(src) % 4:
        raise UnpackError("bad packed size")
    i = len(src) - 4

    def word(at):
        if at < 0:
            raise UnpackError("read before the start")
        return struct.unpack_from(">I", src, at)[0]

    datasize = word(i); i -= 4
    if datasize != size:
        raise UnpackError("size %d, expected %d" % (datasize, size))
    out = bytearray(datasize)
    o = datasize - 1
    crc = word(i); i -= 4
    chk = word(i); i -= 4
    crc ^= chk
    state = [chk, crc, i]

    def next_bit():
        chk, crc, i = state
        cf = chk & 1
        chk >>= 1
        if chk == 0:
            chk = word(i); i -= 4
            crc ^= chk
            cf2 = chk & 1
            chk = (chk >> 1) | 0x80000000
            cf = cf2
        state[0], state[1], state[2] = chk, crc, i
        return cf

    def get_code(n):
        c = 0
        for _ in range(n):
            c = (c << 1) | next_bit()
        return c

    remaining = datasize
    unp_size = 0

    def dec_literal(num_bits, add):
        nonlocal o, remaining
        count = get_code(num_bits) + add + 1
        remaining -= count
        for _ in range(count):
            if o < 0:
                raise UnpackError("output overflow")
            out[o] = get_code(8)
            o -= 1

    def dec_copy(num_bits):
        nonlocal o, remaining
        off = get_code(num_bits)
        count = unp_size + 1
        remaining -= count
        for _ in range(count):
            if o < 0 or o + off >= datasize:
                raise UnpackError("bad back reference")
            out[o] = out[o + off]
            o -= 1

    while True:
        if not next_bit():
            unp_size = 1
            if not next_bit():
                dec_literal(3, 0)
            else:
                dec_copy(8)
        else:
            c = get_code(2)
            if c == 3:
                dec_literal(8, 8)
            elif c < 2:
                unp_size = c + 2
                dec_copy(c + 9)
            else:
                unp_size = get_code(8)
                dec_copy(12)
        if remaining <= 0:
            break
    if state[1] != 0:
        raise UnpackError("bad checksum")
    return bytes(out)


def build(folder, verbose=True):
    memlist_path = find_file(folder, "MEMLIST.BIN")
    if not memlist_path:
        sys.exit("error: MEMLIST.BIN not found in %s (is this the Another World DOS folder?)" % folder)
    memlist = open(memlist_path, "rb").read()
    entries = parse_memlist(memlist)
    banks = {}
    resources = {}
    stats = {"packed": 0, "stored": 0, "bytes": 0}
    for num, e in enumerate(entries):
        if e["bank"] == 0 or e["size"] == 0:
            continue
        bank = e["bank"]
        if bank not in banks:
            p = find_file(folder, "BANK%02X" % bank)
            if not p:
                sys.exit("error: BANK%02X not found in %s" % (bank, folder))
            banks[bank] = open(p, "rb").read()
        raw = banks[bank][e["offset"]:e["offset"] + e["packed"]]
        if len(raw) != e["packed"]:
            sys.exit("error: BANK%02X is too short (resource %d)" % (bank, num))
        if e["packed"] == e["size"]:
            data = raw
            stats["stored"] += 1
        else:
            try:
                data = bytekiller_unpack(raw, e["size"])
            except UnpackError as err:
                sys.exit("error: resource %d of BANK%02X: %s" % (num, bank, err))
            stats["packed"] += 1
        resources[num] = bytearray(data)
        stats["bytes"] += len(data)

    # The music player clears bytes 8..11 of every instrument when it loads a
    # module (SfxPlayer::prepareInstruments()); the console uses the data in
    # read-only flash, so it is done here.
    cleared = set()
    for num, e in enumerate(entries):
        if e["type"] != RT_MUSIC or num not in resources:
            continue
        mod = resources[num]
        for k in range(15):
            ins = struct.unpack_from(">H", mod, 2 + k * 4)[0]
            if ins and ins in resources and entries[ins]["type"] == RT_SOUND:
                if len(resources[ins]) >= 12:
                    resources[ins][8:12] = b"\0\0\0\0"
                    cleared.add(ins)

    files = [("MEMLIST.BIN", memlist)]
    for num in sorted(resources):
        files.append(("RES%03d" % num, bytes(resources[num])))
    if verbose:
        print("resources: %d (%d unpacked, %d stored), %d bytes, %d instruments"
              % (len(resources), stats["packed"], stats["stored"], stats["bytes"], len(cleared)))
    return files


def write_pack(out_path, files, verbose=True):
    header_size = 16 + len(files) * (ENTRY_NAME_LEN + 8)
    offset = (header_size + 3) & ~3
    entries = []
    for name, blob in files:
        entries.append((name, offset, len(blob)))
        offset = (offset + len(blob) + 3) & ~3
    total = offset
    out = bytearray(total)
    out[0:8] = MAGIC
    struct.pack_into("<II", out, 8, len(files), total)
    for i, (name, off, size) in enumerate(entries):
        nb = name.encode("ascii")[:ENTRY_NAME_LEN - 1]
        struct.pack_into("<%dsII" % ENTRY_NAME_LEN, out, 16 + i * (ENTRY_NAME_LEN + 8), nb, off, size)
    for (name, blob), (_, off, size) in zip(files, entries):
        out[off:off + size] = blob
    with open(out_path, "wb") as fh:
        fh.write(out)
    if verbose:
        print("wrote %s: %d files, %d bytes" % (out_path, len(files), total))
    return total


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("output", help="pack to write (same name as the .bin, extension .aw)")
    ap.add_argument("folder", help="folder of the DOS game (MEMLIST.BIN, BANK01..BANK0D)")
    ap.add_argument("-q", "--quiet", action="store_true")
    args = ap.parse_args()
    if not os.path.isdir(args.folder):
        sys.exit("error: %s is not a folder" % args.folder)
    files = build(args.folder, verbose=not args.quiet)
    write_pack(args.output, files, verbose=not args.quiet)


if __name__ == "__main__":
    main()
