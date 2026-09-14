#!/usr/bin/env python3
"""Diff of two raw NVS partition dumps (ESP-IDF NVS format 2).

By default compares keys: which were added, removed, or rewritten with
different content, independent of where garbage collection put them.
--entries also lists every entry slot that changed state (empty, written,
erased) or content, page by page. Names namespace, key and type only; it
never prints values, so its output is safe to share even when the dumps hold
credentials. Key names can still say something about the device (network
names are not stored in key names by ESP-IDF or Launcher, but other firmware
might).

    tools/nvs-diff.py [--entries] before.bin after.bin
"""
import struct
import sys
import zlib

PAGE = 4096
ENTRIES_PER_PAGE = 126
TYPES = {0x01: "u8", 0x11: "i8", 0x02: "u16", 0x12: "i16", 0x04: "u32", 0x14: "i32",
         0x08: "u64", 0x18: "i64", 0x21: "str", 0x41: "blob", 0x42: "blob_data", 0x48: "blob_idx"}
STATES = {0b11: "empty", 0b10: "written", 0b00: "erased", 0b01: "illegal"}


def entry_state(page, i):
    return (page[32 + i // 4] >> ((i % 4) * 2)) & 3


def read_pages(path):
    data = open(path, "rb").read()
    if len(data) % PAGE:
        sys.exit(f"{path}: size {len(data)} is not a whole number of 4 KiB pages")
    return [data[p:p + PAGE] for p in range(0, len(data), PAGE)]


def namespaces(pages):
    names = {0: "<namespaces>"}
    for page in pages:
        for i in range(ENTRIES_PER_PAGE):
            e = page[64 + i * 32:96 + i * 32]
            if entry_state(page, i) == 0b10 and e[0] == 0 and e[1] == 0x01 and e[2] == 1:
                names[e[24]] = e[8:24].split(b"\0")[0].decode(errors="replace")
    return names


def entries(page):
    """(index, state, header) for every entry start; header is None for empty slots."""
    out = {}
    i = 0
    while i < ENTRIES_PER_PAGE:
        state = STATES[entry_state(page, i)]
        e = page[64 + i * 32:96 + i * 32]
        span = e[2]
        if state == "empty" or span == 0 or span == 0xFF or i + span > ENTRIES_PER_PAGE:
            out[i] = (state, None)
            i += 1
            continue
        body = page[64 + i * 32:64 + (i + span) * 32]
        # Content fingerprint over everything except the stored CRC.
        fingerprint = zlib.crc32(body[:4] + body[8:])
        key = e[8:24].split(b"\0")[0].decode(errors="replace")
        out[i] = (state, (e[0], e[1], span, e[3], key, fingerprint))
        for covered in range(i + 1, i + span):
            out[covered] = (STATES[entry_state(page, covered)], "cont")
        i += span
    return out


def describe(header, names):
    ns, typ, span, chunk, key, _ = header
    text = f"{names.get(ns, f'ns#{ns}')}/{key} {TYPES.get(typ, hex(typ))} span {span}"
    return text + (f" chunk {chunk}" if typ == 0x42 else "")


def live_keys(pages, names):
    """(namespace, key, type, chunk) -> content fingerprint of every written entry."""
    out = {}
    for page in pages:
        for state, header in entries(page).values():
            if state != "written" or not isinstance(header, tuple) or header[0] == 0:
                continue
            ns, typ, _, chunk, key, fingerprint = header
            label = (names.get(ns, f"ns#{ns}"), key, TYPES.get(typ, hex(typ)), chunk if typ == 0x42 else None)
            out[label] = fingerprint
    return out


def label_text(label):
    ns, key, typ, chunk = label
    return f"{ns}/{key} {typ}" + (f" chunk {chunk}" if chunk is not None else "")


def key_summary(before, after, names_before, names_after):
    """Compares by namespace and key, so garbage collection moving entries between pages
    does not show up as a change."""
    kb, ka = live_keys(before, names_before), live_keys(after, names_after)
    added = sorted(set(ka) - set(kb))
    removed = sorted(set(kb) - set(ka))
    changed = sorted(k for k in set(ka) & set(kb) if ka[k] != kb[k])
    for label in added:
        print(f"+ {label_text(label)}")
    for label in removed:
        print(f"- {label_text(label)}")
    for label in changed:
        print(f"~ {label_text(label)} (content changed)")
    same = len(set(ka) & set(kb)) - len(changed)
    print(f"{len(added)} added, {len(removed)} removed, {len(changed)} changed, {same} unchanged")


def main():
    args = [a for a in sys.argv[1:] if a != "--entries"]
    if len(args) != 2:
        sys.exit("usage: nvs-diff.py [--entries] before.bin after.bin")
    before, after = read_pages(args[0]), read_pages(args[1])
    if len(before) != len(after):
        sys.exit("the dumps differ in size")
    names_before, names_after = namespaces(before), namespaces(after)

    key_summary(before, after, names_before, names_after)
    if "--entries" not in sys.argv[1:]:
        return

    changes = 0
    for p, (pa, pb) in enumerate(zip(before, after)):
        ha, hb = struct.unpack_from("<II", pa), struct.unpack_from("<II", pb)
        if ha != hb:
            print(f"page {p}: header state {ha[0]:#x} seq {ha[1]} -> state {hb[0]:#x} seq {hb[1]}")
        ea, eb = entries(pa), entries(pb)
        for i in range(ENTRIES_PER_PAGE):
            sa, xa = ea.get(i, ("?", None))
            sb, xb = eb.get(i, ("?", None))
            if (sa, xa) == (sb, xb) or (xa == "cont" and xb == "cont"):
                continue
            if xb == "cont" or (xa == "cont" and xb is None and sa == sb):
                continue
            changes += 1
            left = f"{sa} {describe(xa, names_before)}" if isinstance(xa, tuple) else sa
            right = f"{sb} {describe(xb, names_after)}" if isinstance(xb, tuple) else sb
            if isinstance(xa, tuple) and isinstance(xb, tuple) and xa[:5] == xb[:5] and xa[5] != xb[5]:
                right += " (content changed)"
            print(f"page {p} entry {i:3}: {left}  ->  {right}")
    print(f"{changes} entry changes")


if __name__ == "__main__":
    main()
