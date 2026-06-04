#!/usr/bin/env python3
"""Emit a xenia-canary .patch.toml for the xenia-thor patcher.

The native patcher (src/xenia/patcher/) matches a file by ``title_id`` + build
``hash`` and applies each ``[[patch.<type>]]`` write to guest memory at launch.
This just formats a well-formed file; you supply the addresses/values you found
in Ghidra (see SKILL.md + references/ppc_patch_cookbook.md).

Usage (one patch from the command line):

    python emit_patch_toml.py \
        --title-id 4D5307DF --title-name "Blue Dragon" \
        --hash ABCDEF0123456789 \
        --name "Infinite Gold" --desc "Gold never decreases" --author you \
        --write 0x8265F1A0=0x60000000 \
        --write 0x8265F1A4=0x60000000:be32 \
        --out "4D5307DF - Blue Dragon.patch.toml"

A ``--write ADDR=VALUE[:type]`` may be repeated; ``type`` defaults to ``be32``
(a PPC instruction word / 32-bit int). For floats use e.g.
``0x82001000=60.0:f32``. Multiple ``--patch`` groups are not handled here — for
several independent patches in one file, run once per patch and concatenate the
``[[patch]]`` blocks, or edit the output by hand.
"""

import argparse
import sys

_INT_TYPES = {"be8", "be16", "be32", "be64"}
_FLOAT_TYPES = {"f32", "f64"}
_STR_TYPES = {"string", "u16string", "array"}


def _parse_write(spec):
    """'0xADDR=0xVALUE[:type]' -> (addr_int, value_str_for_toml, type)."""
    if "=" not in spec:
        raise ValueError("expected ADDR=VALUE[:type], got %r" % spec)
    addr_part, value_part = spec.split("=", 1)
    data_type = "be32"
    if ":" in value_part:
        value_part, data_type = value_part.rsplit(":", 1)
    data_type = data_type.strip().lower()
    addr = int(addr_part, 0)

    if data_type in _INT_TYPES:
        value = int(value_part, 0)
        rendered = "0x%X" % value
    elif data_type in _FLOAT_TYPES:
        rendered = repr(float(value_part))
    elif data_type in _STR_TYPES:
        rendered = '"%s"' % value_part.strip().strip('"')
    else:
        raise ValueError("unknown patch type %r" % data_type)
    return addr, rendered, data_type


def build(title_id, title_name, hash_hex, name, desc, author, writes,
          enabled):
    lines = []
    lines.append('title_name = "%s"' % title_name)
    lines.append('title_id = "%s"' % title_id.upper())
    lines.append('hash = "%s"' % hash_hex.upper())
    lines.append("")
    lines.append("[[patch]]")
    lines.append('name = "%s"' % name)
    if desc:
        lines.append('desc = "%s"' % desc)
    if author:
        lines.append('author = "%s"' % author)
    lines.append("is_enabled = %s" % ("true" if enabled else "false"))
    lines.append("")
    for addr, rendered, data_type in writes:
        lines.append("  [[patch.%s]]" % data_type)
        lines.append("  address = 0x%08X" % addr)
        lines.append("  value = %s" % rendered)
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description="Emit a .patch.toml")
    parser.add_argument("--title-id", required=True)
    parser.add_argument("--title-name", required=True)
    parser.add_argument("--hash", required=True,
                        help="16-hex Module Hash from logcat")
    parser.add_argument("--name", required=True)
    parser.add_argument("--desc", default="")
    parser.add_argument("--author", default="")
    parser.add_argument("--write", action="append", default=[],
                        metavar="ADDR=VALUE[:type]",
                        help="repeatable; type defaults to be32")
    parser.add_argument("--enabled", action="store_true",
                        help="set is_enabled = true (default false)")
    parser.add_argument("--out", default="-",
                        help="output file, or - for stdout")
    args = parser.parse_args(argv)

    if not args.write:
        parser.error("at least one --write ADDR=VALUE is required")
    try:
        writes = [_parse_write(w) for w in args.write]
    except ValueError as exc:
        parser.error(str(exc))

    text = build(args.title_id, args.title_name, args.hash, args.name,
                 args.desc, args.author, writes, args.enabled)
    if args.out == "-":
        sys.stdout.write(text)
    else:
        with open(args.out, "w", encoding="utf-8") as handle:
            handle.write(text)
        sys.stderr.write("wrote %s (%d write(s))\n" % (args.out, len(writes)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
