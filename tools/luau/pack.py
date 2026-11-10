"""Pack the precompiled Luau scripts into one file, scripts/scripts.pack.

Opening hundreds of small files is slow on the Switch's SD card (about 80 ms a script on average), so the game reads
the bytecode from this one file instead: it opens it once and reads each module from it when the module first runs.

Format (little-endian):
    "CTPK"                     magic
    u32 version                1
    u32 count                  number of modules
    count times:
        u16 path length, path  the module's .luau path relative to scripts/, with forward slashes
        u64 offset             of its bytecode, from the start of the file
        u32 size               of its bytecode
        u64 check              FNV-1a of its bytecode, which the game checks before loading it
    the bytecode, one module after another

Only a .luauc compiled from the .luau beside it (its header holds the source's hash) is packed, so the pack never
holds bytecode for a source it was not made from.
"""
import argparse
from pathlib import Path
import struct
import sys

MAGIC = b"CTLB"
HEADER_SIZE = len(MAGIC) + 8
PACK_MAGIC = b"CTPK"
PACK_VERSION = 1


def source_hash(source):
    """FNV-1a, as port/src/luau_bytecode.hpp computes it (also the pack's per-script check)."""
    value = 0xCBF29CE484222325
    for byte in source:
        value ^= byte
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def collect(scripts):
    """The (relative path, bytecode) of every .luauc that matches its source, in a stable order."""
    modules = []
    skipped = 0
    for compiled in sorted(scripts.rglob("*.luauc")):
        source_path = compiled.with_suffix(".luau")
        if not source_path.is_file():
            skipped += 1
            continue
        contents = compiled.read_bytes()
        source = source_path.read_bytes()
        expected = MAGIC + struct.pack("<Q", source_hash(source))
        if len(contents) <= HEADER_SIZE or contents[:HEADER_SIZE] != expected:
            skipped += 1
            continue
        relative = source_path.relative_to(scripts).as_posix()
        modules.append((relative, contents[HEADER_SIZE:]))
    return modules, skipped


def write(modules, out):
    """Writes the pack to a temporary file, then moves it into place."""
    index_size = 12 + sum(2 + len(path.encode("utf-8")) + 8 + 4 + 8 for path, _ in modules)
    offset = index_size
    index = bytearray(PACK_MAGIC + struct.pack("<II", PACK_VERSION, len(modules)))
    for path, bytecode in modules:
        encoded = path.encode("utf-8")
        index += struct.pack("<H", len(encoded)) + encoded
        index += struct.pack("<QIQ", offset, len(bytecode), source_hash(bytecode))
        offset += len(bytecode)
    assert len(index) == index_size
    temporary = out.with_suffix(".pack.tmp")
    with temporary.open("wb") as handle:
        handle.write(index)
        for _, bytecode in modules:
            handle.write(bytecode)
    temporary.replace(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--scripts", type=Path, required=True, help="the published scripts directory")
    parser.add_argument("--out", type=Path, required=True, help="the pack to write")
    args = parser.parse_args()
    modules, skipped = collect(args.scripts)
    write(modules, args.out)
    total = sum(len(bytecode) for _, bytecode in modules)
    print(f"Packed {len(modules)} scripts ({total / 1048576:.1f} MiB of bytecode) into {args.out}; skipped {skipped}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
