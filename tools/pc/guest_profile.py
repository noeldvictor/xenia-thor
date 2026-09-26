"""Where does a running xenia spend its CPU? A sampling profiler for the PC build.

  python tools/pc/guest_profile.py --pid PID --log XENIA.LOG [--seconds 20]
      [--top 25] [--json OUT.json]

Attaches to a running xenia.exe (Windows) and, for --seconds, suspends each
of its threads in turn, reads the instruction pointer and resumes it - only
for the threads that used CPU since the previous round, so a waiting thread
adds no samples. Each sample maps to:
- a guest function, when the address is in JIT code: the run must have
  cpu_emit_jit_perf_map=true, whose "JITSYM host size guest" lines (in the
  log) place every compiled guest function;
- a host function of xenia.exe or a DLL (dbghelp with the PDB), otherwise.
Prints per thread (by name) the share of the samples, and the hottest guest
and host functions. The Thor runs the same guest code: a guest function that
is hot here is hot there, where every guest instruction costs more (the a64
JIT and the slower cores), so this names what to make cheaper.

pc_run.py --profile-at SECONDS:DURATION runs this at a point of a route.
"""
import argparse
import bisect
import collections
import ctypes
import ctypes.wintypes as wt
import json
import re
import sys
import time

kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
dbghelp = ctypes.WinDLL('dbghelp', use_last_error=True)

TH32CS_SNAPTHREAD = 0x4
THREAD_SUSPEND_RESUME = 0x2
THREAD_GET_CONTEXT = 0x8
THREAD_QUERY_INFORMATION = 0x40
THREAD_QUERY_LIMITED_INFORMATION = 0x800
PROCESS_ALL_ACCESS = 0x1F0FFF
CONTEXT_AMD64 = 0x100000
CONTEXT_CONTROL = CONTEXT_AMD64 | 0x1
CONTEXT_SIZE = 1232
RIP_OFFSET = 0xF8
CONTEXT_FLAGS_OFFSET = 0x30


class THREADENTRY32(ctypes.Structure):
    _fields_ = [('dwSize', wt.DWORD), ('cntUsage', wt.DWORD),
                ('th32ThreadID', wt.DWORD), ('th32OwnerProcessID', wt.DWORD),
                ('tpBasePri', ctypes.c_long), ('tpDeltaPri', ctypes.c_long),
                ('dwFlags', wt.DWORD)]


class FILETIME(ctypes.Structure):
    _fields_ = [('lo', wt.DWORD), ('hi', wt.DWORD)]


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [('SizeOfStruct', wt.ULONG), ('TypeIndex', wt.ULONG),
                ('Reserved', ctypes.c_uint64 * 2), ('Index', wt.ULONG),
                ('Size', wt.ULONG), ('ModBase', ctypes.c_uint64),
                ('Flags', wt.ULONG), ('Value', ctypes.c_uint64),
                ('Address', ctypes.c_uint64), ('Register', wt.ULONG),
                ('Scope', wt.ULONG), ('Tag', wt.ULONG), ('NameLen', wt.ULONG),
                ('MaxNameLen', wt.ULONG), ('Name', ctypes.c_char * 512)]


kernel32.OpenThread.restype = wt.HANDLE
kernel32.OpenProcess.restype = wt.HANDLE
kernel32.CreateToolhelp32Snapshot.restype = wt.HANDLE
kernel32.GetThreadContext.argtypes = [wt.HANDLE, ctypes.c_void_p]
kernel32.GetThreadTimes.argtypes = [wt.HANDLE] + [ctypes.POINTER(FILETIME)] * 4
dbghelp.SymFromAddr.argtypes = [wt.HANDLE, ctypes.c_uint64,
                                ctypes.POINTER(ctypes.c_uint64),
                                ctypes.POINTER(SYMBOL_INFO)]
dbghelp.SymInitialize.argtypes = [wt.HANDLE, ctypes.c_char_p, wt.BOOL]


def thread_ids(pid):
    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    entry = THREADENTRY32()
    entry.dwSize = ctypes.sizeof(entry)
    ids = []
    ok = kernel32.Thread32First(snap, ctypes.byref(entry))
    while ok:
        if entry.th32OwnerProcessID == pid:
            ids.append(entry.th32ThreadID)
        ok = kernel32.Thread32Next(snap, ctypes.byref(entry))
    kernel32.CloseHandle(snap)
    return ids


def thread_name(handle):
    try:
        get = kernel32.GetThreadDescription
    except AttributeError:
        return ''
    get.argtypes = [wt.HANDLE, ctypes.POINTER(ctypes.c_wchar_p)]
    text = ctypes.c_wchar_p()
    if get(handle, ctypes.byref(text)) >= 0 and text.value:
        name = text.value
        kernel32.LocalFree(ctypes.cast(text, ctypes.c_void_p))
        return name
    return ''


def cpu_time(handle):
    times = [FILETIME() for _ in range(4)]
    if not kernel32.GetThreadTimes(handle, *[ctypes.byref(t) for t in times]):
        return None
    return sum((t.hi << 32) | t.lo for t in times[2:])  # kernel + user, 100 ns


def jit_map(log_path):
    """Sorted (host start, host end, guest address) from the JITSYM lines."""
    spans = []
    pattern = re.compile(r'JITSYM ([0-9A-F]+) ([0-9A-F]+) ([0-9A-F]{8})')
    with open(log_path, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = pattern.search(line)
            if m:
                start = int(m.group(1), 16)
                spans.append((start, start + int(m.group(2), 16), int(m.group(3), 16)))
    spans.sort()
    return spans


def sample(pid, seconds):
    """{thread id: [rip, ...]} and {thread id: name}."""
    handles, names, last = {}, {}, {}
    samples = collections.defaultdict(list)
    context = ctypes.create_string_buffer(CONTEXT_SIZE + 16)
    base = (ctypes.addressof(context) + 15) & ~15
    rounds = 0
    end = time.time() + seconds
    while time.time() < end:
        for tid in thread_ids(pid):
            if tid not in handles:
                h = kernel32.OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                        THREAD_QUERY_INFORMATION, False, tid)
                if not h:
                    continue
                handles[tid] = h
                names[tid] = thread_name(h) or ('tid %d' % tid)
                last[tid] = cpu_time(h)
                continue
            h = handles[tid]
            t = cpu_time(h)
            if t is None or t == last[tid]:
                continue  # no CPU since the last round
            last[tid] = t
            if kernel32.SuspendThread(h) == 0xFFFFFFFF:
                continue
            ctypes.c_uint32.from_address(base + CONTEXT_FLAGS_OFFSET).value = CONTEXT_CONTROL
            if kernel32.GetThreadContext(h, ctypes.c_void_p(base)):
                samples[tid].append(ctypes.c_uint64.from_address(base + RIP_OFFSET).value)
            kernel32.ResumeThread(h)
        rounds += 1
    for h in handles.values():
        kernel32.CloseHandle(h)
    return samples, names, rounds


def symbolizer(pid):
    process = kernel32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
    dbghelp.SymSetOptions(0x2 | 0x4 | 0x10)  # undecorate, deferred loads, line numbers off
    dbghelp.SymInitialize(process, None, True)
    cache = {}

    def name(address):
        if address in cache:
            return cache[address]
        info = SYMBOL_INFO()
        info.SizeOfStruct = ctypes.sizeof(SYMBOL_INFO) - 512
        info.MaxNameLen = 511
        displacement = ctypes.c_uint64()
        if dbghelp.SymFromAddr(process, address, ctypes.byref(displacement), ctypes.byref(info)):
            cache[address] = info.Name.decode(errors='replace')
        else:
            cache[address] = None
        return cache[address]
    return name


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pid', type=int, required=True)
    ap.add_argument('--log', required=True, help='the xenia log (JITSYM lines)')
    ap.add_argument('--seconds', type=float, default=20)
    ap.add_argument('--top', type=int, default=25)
    ap.add_argument('--json', default='')
    args = ap.parse_args()

    samples, names, rounds = sample(args.pid, args.seconds)
    spans = jit_map(args.log)
    starts = [s[0] for s in spans]
    host_name = symbolizer(args.pid)

    def locate(rip):
        i = bisect.bisect_right(starts, rip) - 1
        if i >= 0 and spans[i][0] <= rip < spans[i][1]:
            return 'guest', '%08X' % spans[i][2]
        return 'host', host_name(rip) or ('?%X' % (rip >> 12 << 12))

    total = sum(len(v) for v in samples.values())
    print('%d samples in %d rounds over %.0f s, %d JIT functions mapped' % (
        total, rounds, args.seconds, len(spans)))
    per_thread = sorted(samples.items(), key=lambda kv: -len(kv[1]))
    result = {'samples': total, 'threads': []}
    guest_all = collections.Counter()
    host_all = collections.Counter()
    for tid, rips in per_thread:
        if not rips:
            continue
        guest, host = collections.Counter(), collections.Counter()
        for rip in rips:
            kind, where = locate(rip)
            (guest if kind == 'guest' else host)[where] += 1
        guest_all.update(guest)
        host_all.update(host)
        share = 100.0 * len(rips) / total
        print('\n%5.1f%%  %s: %d samples, %.0f%% guest code' % (
            share, names[tid], len(rips), 100.0 * sum(guest.values()) / len(rips)))
        for where, n in (guest + host).most_common(8):
            print('        %5.1f%%  %s%s' % (100.0 * n / len(rips),
                                           'guest ' if where in guest else '', where))
        result['threads'].append({'name': names[tid], 'samples': len(rips),
                                  'guest': guest.most_common(args.top),
                                  'host': host.most_common(args.top)})
    print('\nhottest guest functions (all threads):')
    for where, n in guest_all.most_common(args.top):
        print('  %5.1f%%  %s' % (100.0 * n / total, where))
    print('\nhottest host functions (all threads):')
    for where, n in host_all.most_common(args.top):
        print('  %5.1f%%  %s' % (100.0 * n / total, where))
    if args.json:
        json.dump(result, open(args.json, 'w'), indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
