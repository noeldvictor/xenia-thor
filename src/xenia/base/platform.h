/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_PLATFORM_H_
#define XENIA_BASE_PLATFORM_H_

// This file contains the main platform switches used by xenia as well as any
// fixups required to normalize the environment. Everything in here should be
// largely portable.
// Platform-specific headers, like platform_win.h, are used to house any
// super platform-specific stuff that implies code is not platform-agnostic.
//
// NOTE: ordering matters here as sometimes multiple flags are defined on
// certain platforms.
//
// Great resource on predefined macros:
// https://sourceforge.net/p/predef/wiki/OperatingSystems/
// Original link: https://predef.sourceforge.net/preos.html

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(TARGET_OS_MAC) && TARGET_OS_MAC
#define XE_PLATFORM_MAC 1
#elif defined(WIN32) || defined(_WIN32)
#define XE_PLATFORM_WIN32 1
#elif defined(__ANDROID__)
#define XE_PLATFORM_ANDROID 1
#define XE_PLATFORM_LINUX 1
#elif defined(__gnu_linux__)
#define XE_PLATFORM_GNU_LINUX 1
#define XE_PLATFORM_LINUX 1
#else
#error Unsupported target OS.
#endif

#if defined(__clang__)
#define XE_COMPILER_CLANG 1
#elif defined(__GNUC__)
#define XE_COMPILER_GNUC 1
#elif defined(_MSC_VER)
#define XE_COMPILER_MSVC 1
#elif defined(__MINGW32)
#define XE_COMPILER_MINGW32 1
#elif defined(__INTEL_COMPILER)
#define XE_COMPILER_INTEL 1
#else
#define XE_COMPILER_UNKNOWN 1
#endif

#if defined(_M_AMD64) || defined(__amd64__)
#define XE_ARCH_AMD64 1
#elif defined(_M_ARM64) || defined(__aarch64__)
#define XE_ARCH_ARM64 1
#elif defined(_M_IX86) || defined(__i386__) || defined(_M_ARM) || \
    defined(__arm__)
#error Xenia is not supported on 32-bit platforms.
#elif defined(_M_PPC) || defined(__powerpc__)
#define XE_ARCH_PPC 1
#endif

#if XE_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX  // Don't want windows.h including min/max macros.
#endif            // XE_PLATFORM_WIN32

#if XE_PLATFORM_WIN32
#include <intrin.h>
#elif XE_ARCH_AMD64
#include <x86intrin.h>
#endif  // XE_PLATFORM_WIN32

#if XE_COMPILER_MSVC
#define XE_NOINLINE __declspec(noinline)
#define XE_COLD
#define XE_NOALIAS __declspec(noalias)
#elif XE_COMPILER_CLANG || XE_COMPILER_GNUC
#define XE_NOINLINE __attribute__((noinline))
#define XE_COLD __attribute__((cold))
#define XE_NOALIAS
#else
#define XE_NOINLINE
#define XE_COLD
#define XE_NOALIAS
#endif

#if XE_ARCH_ARM64
#include "xenia/base/platform_arm64.h"
#endif  // XE_ARCH_ARM64

#if XE_PLATFORM_MAC
#include <libkern/OSByteOrder.h>
#endif  // XE_PLATFORM_MAC

#if XE_COMPILER_MSVC
#define _XEPACKEDSCOPE(body) __pragma(pack(push, 1)) body __pragma(pack(pop));
#else
#define _XEPACKEDSCOPE(body)     \
  _Pragma("pack(push, 1)") body; \
  _Pragma("pack(pop)");
#endif  // XE_PLATFORM_WIN32

#define XEPACKEDSTRUCT(name, value) _XEPACKEDSCOPE(struct name value)
#define XEPACKEDSTRUCTANONYMOUS(value) _XEPACKEDSCOPE(struct value)
#define XEPACKEDUNION(name, value) _XEPACKEDSCOPE(union name value)

// Edge kernel-port: branch-hint if-statements used by the merged kernel.
// (Edge only defines these in its fallback compiler branch - we define them
// unconditionally via C++20 attributes, which MSVC and clang both honor.)
#ifndef XE_FORCEINLINE
#if XE_COMPILER_HAS_MSVC_EXTENSIONS == 1
#define XE_FORCEINLINE __forceinline
#elif XE_COMPILER_HAS_GNU_EXTENSIONS == 1
#define XE_FORCEINLINE inline __attribute__((always_inline))
#else
#define XE_FORCEINLINE inline
#endif
#endif

#ifndef XE_RESTRICT
// __restrict is part of the type system on MSVC; clang/gcc get equivalent
// behavior from -fstrict-aliasing, so it is a no-op there (matches Edge).
#if XE_COMPILER_CLANG_CL == 0 && XE_COMPILER_MSVC == 1
#define XE_RESTRICT __restrict
#else
#define XE_RESTRICT
#endif
#endif

#ifndef XE_LIKELY_IF
#define XE_LIKELY_IF(...) if (!!(__VA_ARGS__)) [[likely]]
#endif
#ifndef XE_UNLIKELY_IF
#define XE_UNLIKELY_IF(...) if (!!(__VA_ARGS__)) [[unlikely]]
#endif
// Edge kernel-port: marks a declaration that may legitimately go unused (e.g. a
// value only consumed by assert_true(), which compiles out in release).
#ifndef XE_MAYBE_UNUSED
#if XE_COMPILER_HAS_GNU_EXTENSIONS == 1
#define XE_MAYBE_UNUSED __attribute__((unused))
#else
#define XE_MAYBE_UNUSED
#endif
#endif
#ifndef XE_MSVC_ASSUME
#if XE_COMPILER_HAS_MSVC_EXTENSIONS == 1
#define XE_MSVC_ASSUME(...) __assume(__VA_ARGS__)
#else
#define XE_MSVC_ASSUME(...) static_cast<void>(0)
#endif
#endif
#ifndef XE_NOINLINE
#if XE_COMPILER_HAS_MSVC_EXTENSIONS == 1
#define XE_NOINLINE __declspec(noinline)
#elif XE_COMPILER_HAS_GNU_EXTENSIONS == 1
#define XE_NOINLINE __attribute__((noinline))
#else
#define XE_NOINLINE
#endif
#endif

namespace xe {

#if XE_PLATFORM_WIN32
const char kPathSeparator = '\\';
#else
const char kPathSeparator = '/';
#endif  // XE_PLATFORM_WIN32

const char kGuestPathSeparator = '\\';

}  // namespace xe

#endif  // XENIA_BASE_PLATFORM_H_
