/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// The one definition of the stb_image_write functions. The trace dump tool
// and the trace viewer's dump mode both write PNGs; a definition inside
// trace_dump.cc pulled that file, and its target_trace_file cvar, into the
// trace viewer's link as a duplicate.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#undef _CRT_SECURE_NO_WARNINGS
#undef _CRT_NONSTDC_NO_DEPRECATE
#include "third_party/stb/stb_image_write.h"
