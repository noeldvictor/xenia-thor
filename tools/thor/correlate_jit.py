import sys, re
# Args: <jitsym_logcat.txt> <hot_addrs comma-separated hex like 2a026dd3c,2a0294edc>
jitsym_path, hot = sys.argv[1], sys.argv[2]
entries = []  # (host_start, host_end, guest)
seen = set()
for line in open(jitsym_path, encoding='utf-8', errors='ignore'):
    m = re.search(r'JITSYM ([0-9A-Fa-f]+) ([0-9A-Fa-f]+) ([0-9A-Fa-f]{8})', line)
    if m:
        h = int(m.group(1), 16); sz = int(m.group(2), 16); g = m.group(3)
        key = (h, g)
        if key in seen: continue
        seen.add(key)
        entries.append((h, h+sz, g.upper()))
print(f"parsed {len(entries)} unique JITSYM entries")
for a in hot.split(','):
    addr = int(a, 16)
    hits = [(s,e,g) for (s,e,g) in entries if s <= addr < e]
    if hits:
        for s,e,g in hits:
            print(f"host 0x{addr:X}  ->  guest 0x{g}  (func host range 0x{s:X}-0x{e:X}, off +0x{addr-s:X})")
    else:
        # nearest below
        below = [(s,e,g) for (s,e,g) in entries if s <= addr]
        if below:
            s,e,g = max(below, key=lambda x: x[0])
            print(f"host 0x{addr:X}  ->  (no exact) nearest guest 0x{g} range 0x{s:X}-0x{e:X} off +0x{addr-s:X}")
        else:
            print(f"host 0x{addr:X}  ->  no JITSYM below")
