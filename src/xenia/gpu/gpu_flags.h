/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GPU_FLAGS_H_
#define XENIA_GPU_GPU_FLAGS_H_
#include "xenia/base/cvar.h"

DECLARE_path(trace_gpu_prefix);
DECLARE_bool(trace_gpu_stream);

DECLARE_path(dump_shaders);

DECLARE_bool(vsync);

DECLARE_bool(gpu_allow_invalid_fetch_constants);
DECLARE_bool(clear_memory_page_state);
DECLARE_bool(gpu_interrupt_on_ring_idle);
DECLARE_bool(gpu_blue_dragon_kick_wait_token);
DECLARE_int32(gpu_blue_dragon_kick_wait_token_budget);
DECLARE_bool(gpu_trace_swap);

DECLARE_bool(non_seamless_cube_map);

DECLARE_bool(half_pixel_offset);

DECLARE_int32(query_occlusion_fake_sample_count);

#define XE_GPU_FINE_GRAINED_DRAW_SCOPES 1

#endif  // XENIA_GPU_GPU_FLAGS_H_
