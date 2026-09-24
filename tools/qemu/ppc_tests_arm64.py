"""The PPC hardware corpus on the Thor's ARM64 JIT (a64), on the PC under qemu.

  python tools/qemu/ppc_tests_arm64.py [--cvars "a=1 b=2"] [--build-only]
      [--baseline scratch/ppc_arm64/baseline.txt]

The 169,117 hardware-captured cases in src/xenia/cpu/ppc/testing/ run on the
x64 backend natively (build/bin/Windows/Release/xenia-cpu-ppc-tests.exe); the
Thor runs the a64 JIT, which the PC cannot execute. This links a minimal static
ARM64 runner - ppc_testing_main.cc against the Release static libraries of
the last NativeCore build (xenia-cpu, the a64 backend, core, base; ThinLTO,
NDK clang) with stubs for the Android logging calls and its own main
(ppc_tests_main_static.cc: cxxopts' std::regex throws in static Bionic) - and
runs it under
qemu-aarch64 in WSL. It prints the totals and the failures per test file, and
with --baseline the files whose failure count changed.

The full app's ARM64 test target cannot run under qemu (it links libandroid,
liblog and the shared libLLVM). The LLVM backend is not covered here.
2026-08-18: a64 failures 7,907 -> 3,038 on the device (the last run); it was
OOM-killed on the Thor after that and never ran again.
"""
import argparse
import glob
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'scratch', 'ppc_arm64')
NDK = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Android', 'Sdk', 'ndk',
                   '25.0.8775105', 'toolchains', 'llvm', 'prebuilt', 'windows-x86_64', 'bin')
CXX = os.path.join(NDK, 'clang++.exe')
CC = os.path.join(NDK, 'clang.exe')
TARGET = '--target=aarch64-linux-android29'  # 29: the NDK aligns the TLS segment for static Bionic
FLAGS = ['-O2', '-std=c++20', '-march=armv8.2-a+lse+crypto+sha3+crc+dotprod',
         '-mno-outline-atomics', '-fno-omit-frame-pointer', '-flto=thin',
         '-D_UNICODE', '-DUNICODE', '-DNDEBUG', '-D_NO_DEBUG_HEAP=1',
         '-I' + ROOT, '-I' + os.path.join(ROOT, 'src'),
         '-I' + os.path.join(ROOT, 'third_party'),
         '-I' + os.path.join(ROOT, 'third_party', 'fmt', 'include'),
         '-I' + os.path.join(ROOT, 'build', 'version')]
LIBS = ['xenia-cpu-backend-arm64', 'xenia-cpu', 'xenia-core', 'xenia-base',
        'xbyak_aarch64', 'capstone', 'fmt']
LOG_SHIM = r'''
#include <stdarg.h>
#include <stdio.h>
int __android_log_write(int prio, const char* tag, const char* text) {
  return fprintf(stderr, "%s: %s\n", tag ? tag : "", text ? text : "");
}
int __android_log_vprint(int prio, const char* tag, const char* fmt, va_list ap) {
  fprintf(stderr, "%s: ", tag ? tag : "");
  int n = vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  return n;
}
int __android_log_print(int prio, const char* tag, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = __android_log_vprint(prio, tag, fmt, ap);
  va_end(ap);
  return n;
}
void __android_log_assert(const char* cond, const char* tag, const char* fmt, ...) {
  fprintf(stderr, "assert %s\n", cond ? cond : "");
  __builtin_trap();
}

/* xenia's guest memory and the a64 code cache use ASharedMemory_create from
   libandroid.so (memory_posix.cc: dlopen + dlsym); a static binary under qemu
   has neither libandroid nor /dev/ashmem, so these hand out memfd. */
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
static int static_runner_libandroid;
static int ShimASharedMemoryCreate(const char* name, size_t size) {
  int fd = (int)syscall(__NR_memfd_create, name ? name : "xenia", 0);
  if (fd < 0) return -1;
  if (ftruncate(fd, (off_t)size) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}
void* dlopen(const char* filename, int flags) {
  return filename && strstr(filename, "libandroid") ? &static_runner_libandroid : 0;
}
void* dlsym(void* handle, const char* symbol) {
  if (handle == &static_runner_libandroid && !strcmp(symbol, "ASharedMemory_create")) {
    return (void*)ShimASharedMemoryCreate;
  }
  return 0;
}
int dlclose(void* handle) { return 0; }
char* dlerror(void) { return (char*)"static PPC test runner"; }
struct Dl_info;
int dladdr(const void* address, struct Dl_info* info) { return 0; }
'''


# The CPU libraries reference four symbols of layers the tests never reach
# (the LLVM backend census, the GPU ring buffer, a kernel user setting).
LINK_STUBS = r'''
#include "xenia/base/cvar.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/kernel/xam/user_settings.h"
DEFINE_bool(cpu_llvm_guest_entry_census, false, "Stub for the PPC test runner.",
            "CPU");
namespace xe {
namespace cpu {
namespace backend {
namespace llvm_backend {
uint64_t LlvmGuestEntryCount() { return 0; }
}  // namespace llvm_backend
}  // namespace backend
}  // namespace cpu
namespace gpu {
void CommandProcessor::UpdateWritePointer(uint32_t value) {}
}  // namespace gpu
namespace kernel {
namespace xam {
UserSetting::UserSetting(UserSettingId setting_id, UserDataTypes setting_data) {}
}  // namespace xam
}  // namespace kernel
}  // namespace xe
'''


def obj_dir():
    """The newest NativeCore object tree with the ARM64 static libraries."""
    dirs = glob.glob(os.path.join(ROOT, 'android', 'android_studio_project', 'app', 'build',
                                  'intermediates', 'cxx', '*', '*', 'obj', 'local', 'arm64-v8a'))
    dirs = [d for d in dirs if os.path.exists(os.path.join(d, 'libxenia-cpu.a'))]
    return max(dirs, key=lambda d: os.path.getmtime(os.path.join(d, 'libxenia-cpu.a')))


def build():
    os.makedirs(OUT, exist_ok=True)
    objs = []
    for src in ('src/xenia/cpu/ppc/testing/ppc_testing_main.cc',
                'tools/qemu/ppc_tests_main_static.cc'):
        obj = os.path.join(OUT, os.path.basename(src) + '.o')
        subprocess.run([CXX, TARGET, '-c', os.path.join(ROOT, src), '-o', obj] + FLAGS,
                       check=True)
        objs.append(obj)
    stubs_cc = os.path.join(OUT, 'link_stubs.cc')
    open(stubs_cc, 'w').write(LINK_STUBS)
    stubs_o = stubs_cc + '.o'
    subprocess.run([CXX, TARGET, '-c', stubs_cc, '-o', stubs_o] + FLAGS, check=True)
    objs.append(stubs_o)
    shim_c = os.path.join(OUT, 'android_log_shim.c')
    open(shim_c, 'w').write(LOG_SHIM)
    shim_o = shim_c + '.o'
    subprocess.run([CC, TARGET, '-O2', '-c', shim_c, '-o', shim_o], check=True)
    objs.append(shim_o)
    lib_dir = obj_dir()
    exe = os.path.join(OUT, 'ppc_tests_arm64')
    cmd = ([CXX, TARGET, '-static', '-fuse-ld=lld', '-flto=thin', '-O2', '-o', exe] + objs +
           ['-Wl,--start-group'] + [os.path.join(lib_dir, 'lib%s.a' % l) for l in LIBS] +
           ['-Wl,--end-group', '-static-libstdc++', '-lm'])
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode:
        undefined = sorted(set(re.findall(r'undefined symbol: (.+)', p.stderr)))
        print('link failed; %d undefined symbols%s' % (
            len(undefined), (':\n  ' + '\n  '.join(undefined[:40])) if undefined else ''))
        if not undefined:
            print(p.stderr[-3000:])
        return None
    print('built %s (libraries from %s)' % (os.path.relpath(exe, ROOT),
                                            os.path.relpath(lib_dir, ROOT)))
    return exe


def to_wsl(path):
    path = os.path.abspath(path).replace('\\', '/')
    return '/mnt/' + path[0].lower() + path[2:]


def run(exe, cvars):
    testing = to_wsl(os.path.join(ROOT, 'src', 'xenia', 'cpu', 'ppc', 'testing')) + '/'
    cmd = ('qemu-aarch64 %s --test_path=%s --test_bin_path=%sbin/ %s' %
           (to_wsl(exe), testing, testing, ' '.join('--' + c for c in cvars)))
    log = os.path.join(OUT, 'run.log')
    with open(log, 'w', encoding='utf-8') as f:
        subprocess.run(['wsl', '-e', 'sh', '-c', cmd + ' 2>&1'], stdout=f,
                       env=dict(os.environ, MSYS_NO_PATHCONV='1'), timeout=6 * 3600)
    return log


def failures_by_file(log):
    suite = None
    per_file = {}
    totals = {}
    for line in open(log, encoding='utf-8', errors='replace'):
        m = re.search(r'\s(\S+\.s):\s*$', line)
        if m:
            suite = m.group(1)
        if 'TEST FAILED' in line:
            per_file[suite] = per_file.get(suite, 0) + 1
        m = re.search(r'(Total tests|Passed|Failed): (\d+)', line)
        if m:
            totals[m.group(1)] = int(m.group(2))
    return totals, per_file


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cvars', default='')
    ap.add_argument('--build-only', action='store_true')
    ap.add_argument('--baseline', default='')
    args = ap.parse_args()
    exe = build()
    if not exe or args.build_only:
        return 0 if exe else 1
    log = run(exe, [c for c in args.cvars.split() if c])
    totals, per_file = failures_by_file(log)
    print('a64 under qemu: %s' % totals)
    for name, n in sorted(per_file.items(), key=lambda kv: -kv[1])[:40]:
        print('  %6d  %s' % (n, name))
    if args.baseline and os.path.exists(args.baseline):
        _, base = failures_by_file(args.baseline)
        changed = [(k, base.get(k, 0), per_file.get(k, 0))
                   for k in sorted(set(base) | set(per_file))
                   if base.get(k, 0) != per_file.get(k, 0)]
        print('changed against the baseline (file, before, now):')
        for row in changed:
            print('  %s %d -> %d' % row)
    print('log', os.path.relpath(log, ROOT))
    return 0


if __name__ == '__main__':
    sys.exit(main())
