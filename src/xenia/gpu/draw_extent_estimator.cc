/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/draw_extent_estimator.h"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/registers.h"
#include "xenia/gpu/ucode.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/graphics_util.h"

DEFINE_bool(
    execute_unclipped_draw_vs_on_cpu, true,
    "Execute the vertex shader for draws with clipping disabled, primarily "
    "screen-space draws (such as clears), on the CPU when possible to estimate "
    "the extent of the EDRAM involved in the draw.\n"
    "Enabling this may significantly improve GPU performance as otherwise up "
    "to the entire EDRAM may be considered used in draws without clipping, "
    "potentially resulting in spurious EDRAM range ownership transfer round "
    "trips between host render targets.\n"
    "Also, on hosts where certain render target formats have to be emulated in "
    "a lossy way (for instance, 16-bit fixed-point via 16-bit floating-point), "
    "this prevents corruption of other render targets located after the "
    "current ones in the EDRAM by lossy range ownership transfers done for "
    "those draws.",
    "GPU");
DEFINE_bool(
    execute_unclipped_draw_vs_on_cpu_with_scissor, false,
    "Don't restrict the usage of execute_unclipped_draw_vs_on_cpu to only "
    "non-scissored draws (with the right and the bottom sides of the scissor "
    "rectangle at 8192 or beyond) even though if the scissor rectangle is "
    "present, it's usually sufficient for esimating the height of the render "
    "target.\n"
    "Enabling this may cause excessive processing of vertices on the CPU, as "
    "some games draw rectangles (for their UI, for instance) without clipping, "
    "but with a proper scissor rectangle.",
    "GPU");

namespace xe {
namespace gpu {

void DrawExtentEstimator::PositionYExportSink::Export(
    ucode::ExportRegister export_register, const float* value,
    uint32_t value_mask) {
  if (export_register == ucode::ExportRegister::kVSPosition) {
    if (value_mask & 0b0010) {
      position_y_ = value[1];
    }
    if (value_mask & 0b1000) {
      position_w_ = value[3];
    }
  } else if (export_register ==
             ucode::ExportRegister::kVSPointSizeEdgeFlagKillVertex) {
    if (value_mask & 0b0001) {
      point_size_ = value[0];
    }
    if (value_mask & 0b0100) {
      vertex_kill_ = xe::memory::Reinterpret<uint32_t>(value[2]);
    }
  }
}

void DrawExtentEstimator::PositionExportSink::Export(
    ucode::ExportRegister export_register, const float* value,
    uint32_t value_mask) {
  if (export_register == ucode::ExportRegister::kVSPosition) {
    if (value_mask & 0b0001) {
      position_x_ = value[0];
    }
    if (value_mask & 0b0010) {
      position_y_ = value[1];
    }
    if (value_mask & 0b0100) {
      position_z_ = value[2];
    }
    if (value_mask & 0b1000) {
      position_w_ = value[3];
    }
  } else if (export_register ==
             ucode::ExportRegister::kVSPointSizeEdgeFlagKillVertex) {
    if (value_mask & 0b0100) {
      vertex_kill_ = xe::memory::Reinterpret<uint32_t>(value[2]);
    }
  }
}

uint32_t DrawExtentEstimator::EstimateVertexMaxY(const Shader& vertex_shader) {
  SCOPE_profile_cpu_f("gpu");

  const RegisterFile& regs = register_file_;

  auto vgt_draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
  if (!vgt_draw_initiator.num_indices) {
    return 0;
  }
  if (vgt_draw_initiator.source_select != xenos::SourceSelect::kDMA &&
      vgt_draw_initiator.source_select != xenos::SourceSelect::kAutoIndex) {
    // TODO(Triang3l): Support immediate indices.
    return xenos::kTexture2DCubeMaxWidthHeight;
  }

  // Not reproducing tessellation.
  if (xenos::IsMajorModeExplicit(vgt_draw_initiator.major_mode,
                                 vgt_draw_initiator.prim_type) &&
      regs.Get<reg::VGT_OUTPUT_PATH_CNTL>().path_select ==
          xenos::VGTOutputPath::kTessellationEnable) {
    return xenos::kTexture2DCubeMaxWidthHeight;
  }

  assert_true(vertex_shader.type() == xenos::ShaderType::kVertex);
  assert_true(vertex_shader.is_ucode_analyzed());
  if (!ShaderInterpreter::CanInterpretShader(vertex_shader)) {
    return xenos::kTexture2DCubeMaxWidthHeight;
  }

  auto vgt_dma_size = regs.Get<reg::VGT_DMA_SIZE>();
  union {
    const void* index_buffer;
    const uint16_t* index_buffer_16;
    const uint32_t* index_buffer_32;
  };
  xenos::Endian index_endian = vgt_dma_size.swap_mode;
  if (vgt_draw_initiator.source_select == xenos::SourceSelect::kDMA) {
    xenos::IndexFormat index_format = vgt_draw_initiator.index_size;
    uint32_t index_buffer_base = regs[XE_GPU_REG_VGT_DMA_BASE];
    uint32_t index_buffer_read_count =
        std::min(uint32_t(vgt_draw_initiator.num_indices),
                 uint32_t(vgt_dma_size.num_words));
    if (vgt_draw_initiator.index_size == xenos::IndexFormat::kInt16) {
      // Handle the index endianness to same way as the PrimitiveProcessor.
      if (index_endian == xenos::Endian::k8in32) {
        index_endian = xenos::Endian::k8in16;
      } else if (index_endian == xenos::Endian::k16in32) {
        index_endian = xenos::Endian::kNone;
      }
      index_buffer_base &= ~uint32_t(sizeof(uint16_t) - 1);
      if (trace_writer_) {
        trace_writer_->WriteMemoryRead(
            index_buffer_base, sizeof(uint16_t) * index_buffer_read_count);
      }
    } else {
      assert_true(vgt_draw_initiator.index_size == xenos::IndexFormat::kInt32);
      index_buffer_base &= ~uint32_t(sizeof(uint32_t) - 1);
      if (trace_writer_) {
        trace_writer_->WriteMemoryRead(
            index_buffer_base, sizeof(uint32_t) * index_buffer_read_count);
      }
    }
    index_buffer = memory_.TranslatePhysical(index_buffer_base);
  }
  auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  uint32_t reset_index =
      regs.Get<reg::VGT_MULTI_PRIM_IB_RESET_INDX>().reset_indx;
  uint32_t index_offset = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
  uint32_t min_index = regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;
  uint32_t max_index = regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;

  auto pa_cl_vte_cntl = regs.Get<reg::PA_CL_VTE_CNTL>();
  float viewport_y_scale = pa_cl_vte_cntl.vport_y_scale_ena
                               ? regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YSCALE)
                               : 1.0f;
  float viewport_y_offset =
      pa_cl_vte_cntl.vport_y_offset_ena
          ? regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YOFFSET)
          : 0.0f;

  int32_t point_vertex_min_diameter_float = 0;
  int32_t point_vertex_max_diameter_float = 0;
  float point_constant_radius_y = 0.0f;
  if (vgt_draw_initiator.prim_type == xenos::PrimitiveType::kPointList) {
    auto pa_su_point_minmax = regs.Get<reg::PA_SU_POINT_MINMAX>();
    point_vertex_min_diameter_float = xe::memory::Reinterpret<int32_t>(
        float(pa_su_point_minmax.min_size) * (2.0f / 16.0f));
    point_vertex_max_diameter_float = xe::memory::Reinterpret<int32_t>(
        float(pa_su_point_minmax.max_size) * (2.0f / 16.0f));
    point_constant_radius_y =
        float(regs.Get<reg::PA_SU_POINT_SIZE>().height) * (1.0f / 16.0f);
  }

  float max_y = -FLT_MAX;

  shader_interpreter_.SetShader(vertex_shader);

  PositionYExportSink position_y_export_sink;
  shader_interpreter_.SetExportSink(&position_y_export_sink);
  for (uint32_t i = 0; i < vgt_draw_initiator.num_indices; ++i) {
    uint32_t vertex_index;
    if (vgt_draw_initiator.source_select == xenos::SourceSelect::kDMA) {
      if (i < vgt_dma_size.num_words) {
        if (vgt_draw_initiator.index_size == xenos::IndexFormat::kInt16) {
          vertex_index = index_buffer_16[i];
        } else {
          vertex_index = index_buffer_32[i];
        }
        // The Xenos only uses 24 bits of the index (reset_indx is 24-bit).
        vertex_index = xenos::GpuSwap(vertex_index, index_endian) & 0xFFFFFF;
      } else {
        vertex_index = 0;
      }
      if (pa_su_sc_mode_cntl.multi_prim_ib_ena && vertex_index == reset_index) {
        continue;
      }
    } else {
      assert_true(vgt_draw_initiator.source_select ==
                  xenos::SourceSelect::kAutoIndex);
      vertex_index = i;
    }
    vertex_index =
        std::min(max_index,
                 std::max(min_index, (vertex_index + index_offset) & 0xFFFFFF));

    position_y_export_sink.Reset();

    shader_interpreter_.temp_registers()[0] = float(vertex_index);
    shader_interpreter_.Execute();

    if (position_y_export_sink.vertex_kill().has_value() &&
        (position_y_export_sink.vertex_kill().value() & ~(UINT32_C(1) << 31))) {
      continue;
    }
    if (!position_y_export_sink.position_y().has_value()) {
      continue;
    }
    float vertex_y = position_y_export_sink.position_y().value();
    if (!pa_cl_vte_cntl.vtx_xy_fmt) {
      if (!position_y_export_sink.position_w().has_value()) {
        continue;
      }
      vertex_y /= position_y_export_sink.position_w().value();
    }

    vertex_y = vertex_y * viewport_y_scale + viewport_y_offset;

    if (vgt_draw_initiator.prim_type == xenos::PrimitiveType::kPointList) {
      float point_radius_y;
      if (position_y_export_sink.point_size().has_value()) {
        // Vertex-specified diameter. Clamped effectively as a signed integer in
        // the hardware, -NaN, -Infinity ... -0 to the minimum, +Infinity, +NaN
        // to the maximum.
        point_radius_y =
            0.5f *
            xe::memory::Reinterpret<float>(std::min(
                point_vertex_max_diameter_float,
                std::max(point_vertex_min_diameter_float,
                         xe::memory::Reinterpret<int32_t>(
                             position_y_export_sink.point_size().value()))));
      } else {
        // Constant radius.
        point_radius_y = point_constant_radius_y;
      }
      vertex_y += point_radius_y;
    }

    // std::max is `a < b ? b : a`, thus in case of NaN, the first argument is
    // always returned - max_y, which is initialized to a normalized value.
    max_y = std::max(max_y, vertex_y);
  }
  shader_interpreter_.SetExportSink(nullptr);

  int32_t max_y_24p8 = ui::FloatToD3D11Fixed16p8(max_y);
  // 16p8 range is -32768 to 32767+255/256, but it's stored as uint32_t here,
  // as 24p8, so overflowing up to -8388608 to 8388608+255/256 is safe. The
  // range of the window offset plus the half-pixel offset is -16384 to 16384.5,
  // so it's safe to add both - adding it will neither move the 16p8 clamping
  // bounds -32768 and 32767+255/256 into the 0...8192 screen space range, nor
  // cause 24p8 overflow.
  if (regs.Get<reg::PA_SU_VTX_CNTL>().pix_center ==
      xenos::PixelCenter::kD3DZero) {
    max_y_24p8 += 128;
  }
  if (pa_su_sc_mode_cntl.vtx_window_offset_enable) {
    max_y_24p8 += regs.Get<reg::PA_SC_WINDOW_OFFSET>().window_y_offset * 256;
  }
  // Top-left rule - .5 exclusive without MSAA, 1. exclusive with MSAA.
  auto rb_surface_info = regs.Get<reg::RB_SURFACE_INFO>();
  return (uint32_t(std::max(int32_t(0), max_y_24p8)) +
          ((rb_surface_info.msaa_samples == xenos::MsaaSamples::k1X) ? 127
                                                                     : 255)) >>
         8;
}

uint32_t DrawExtentEstimator::EstimateMaxY(bool try_to_estimate_vertex_max_y,
                                           const Shader& vertex_shader) {
  SCOPE_profile_cpu_f("gpu");

  const RegisterFile& regs = register_file_;

  auto pa_sc_window_offset = regs.Get<reg::PA_SC_WINDOW_OFFSET>();
  int32_t window_y_offset = pa_sc_window_offset.window_y_offset;

  // Scissor.
  auto pa_sc_window_scissor_br = regs.Get<reg::PA_SC_WINDOW_SCISSOR_BR>();
  int32_t scissor_bottom = int32_t(pa_sc_window_scissor_br.br_y);
  bool scissor_window_offset =
      !regs.Get<reg::PA_SC_WINDOW_SCISSOR_TL>().window_offset_disable;
  if (scissor_window_offset) {
    scissor_bottom += window_y_offset;
  }
  auto pa_sc_screen_scissor_br = regs.Get<reg::PA_SC_SCREEN_SCISSOR_BR>();
  scissor_bottom =
      std::min(scissor_bottom, int32_t(pa_sc_screen_scissor_br.br_y));
  uint32_t max_y = uint32_t(std::max(scissor_bottom, int32_t(0)));

  if (regs.Get<reg::PA_CL_CLIP_CNTL>().clip_disable) {
    // Actual extent from the vertices.
    if (try_to_estimate_vertex_max_y &&
        cvars::execute_unclipped_draw_vs_on_cpu) {
      bool estimate_vertex_max_y;
      if (cvars::execute_unclipped_draw_vs_on_cpu_with_scissor) {
        estimate_vertex_max_y = true;
      } else {
        estimate_vertex_max_y = false;
        if (scissor_bottom >= xenos::kTexture2DCubeMaxWidthHeight) {
          // Handle just the usual special 8192x8192 case in Direct3D 9 - 8192
          // may be a normal render target height (80x8192 is well within the
          // EDRAM size, for instance), no need to process the vertices on the
          // CPU in this case.
          int32_t scissor_right = int32_t(pa_sc_window_scissor_br.br_x);
          if (scissor_window_offset) {
            scissor_right += pa_sc_window_offset.window_x_offset;
          }
          scissor_right =
              std::min(scissor_right, int32_t(pa_sc_screen_scissor_br.br_x));
          if (scissor_right >= xenos::kTexture2DCubeMaxWidthHeight) {
            estimate_vertex_max_y = true;
          }
        }
      }
      if (estimate_vertex_max_y) {
        max_y = std::min(max_y, EstimateVertexMaxY(vertex_shader));
      }
    }
  } else {
    // Viewport. Though the Xenos itself doesn't have an implicit viewport
    // scissor (it's set by Direct3D 9 when a viewport is used), on hosts, it
    // usually exists and can't be disabled.
    auto pa_cl_vte_cntl = regs.Get<reg::PA_CL_VTE_CNTL>();
    float viewport_bottom = 0.0f;
    // First calculate all the integer.0 or integer.5 offsetting exactly at full
    // precision.
    if (regs.Get<reg::PA_SU_SC_MODE_CNTL>().vtx_window_offset_enable) {
      viewport_bottom += float(window_y_offset);
    }
    if (regs.Get<reg::PA_SU_VTX_CNTL>().pix_center ==
        xenos::PixelCenter::kD3DZero) {
      viewport_bottom += 0.5f;
    }
    // Then apply the floating-point viewport offset.
    if (pa_cl_vte_cntl.vport_y_offset_ena) {
      viewport_bottom += regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YOFFSET);
    }
    viewport_bottom +=
        pa_cl_vte_cntl.vport_y_scale_ena
            ? std::abs(regs.Get<float>(XE_GPU_REG_PA_CL_VPORT_YSCALE))
            : 1.0f;
    // Using floor, or, rather, truncation (because maxing with zero anyway)
    // similar to how viewport scissoring behaves on real AMD, Intel and Nvidia
    // GPUs on Direct3D 12 (but not WARP), also like in
    // draw_util::GetHostViewportInfo.
    // max(0.0f, viewport_bottom) to drop NaN and < 0 - max picks the first
    // argument in the !(a < b) case (always for NaN), min as float (max_y is
    // well below 2^24) to safely drop very large values.
    max_y = uint32_t(std::min(float(max_y), std::max(0.0f, viewport_bottom)));
  }

  return max_y;
}

uint32_t DrawExtentEstimator::CountCullableTriangles(
    const Shader& vertex_shader) {
  SCOPE_profile_cpu_f("gpu");

  const RegisterFile& regs = register_file_;

  auto vgt_draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
  if (!vgt_draw_initiator.num_indices) {
    return 0;
  }
  // Triangle LIST and STRIP are counted. Blue Dragon's scenes are strip-dominated
  // (device-measured heavy intro: tl=0, ts~1600), so a list-only counter reads 0 -
  // useless for sizing the cull on the real content. Strips form primitives across
  // the index stream with ALTERNATING winding, handled in the per-triangle loop
  // below. Fans/lines/points/rects/quads aren't backface/side-cullable the same way
  // and stay uncounted.
  const bool prim_is_strip =
      vgt_draw_initiator.prim_type == xenos::PrimitiveType::kTriangleStrip;
  if (vgt_draw_initiator.prim_type != xenos::PrimitiveType::kTriangleList &&
      !prim_is_strip) {
    return 0;
  }
  if (vgt_draw_initiator.source_select != xenos::SourceSelect::kDMA &&
      vgt_draw_initiator.source_select != xenos::SourceSelect::kAutoIndex) {
    // Immediate indices not supported here.
    return 0;
  }
  // Not reproducing tessellation.
  if (xenos::IsMajorModeExplicit(vgt_draw_initiator.major_mode,
                                 vgt_draw_initiator.prim_type) &&
      regs.Get<reg::VGT_OUTPUT_PATH_CNTL>().path_select ==
          xenos::VGTOutputPath::kTessellationEnable) {
    return 0;
  }
  assert_true(vertex_shader.type() == xenos::ShaderType::kVertex);
  assert_true(vertex_shader.is_ucode_analyzed());
  if (!ShaderInterpreter::CanInterpretShader(vertex_shader)) {
    // Texture-fetch VS - uncounted (the counter is a lower bound over
    // interpretable draws only).
    return 0;
  }

  auto pa_cl_vte_cntl = regs.Get<reg::PA_CL_VTE_CNTL>();
  cull_vtx_xy_fmt_ = pa_cl_vte_cntl.vtx_xy_fmt != 0;
  if (cull_vtx_xy_fmt_) {
    // Positions are pre-divided (screen/NDC); the homogeneous clip-plane test
    // below assumes clip space, so skip conservatively rather than misjudge.
    return 0;
  }

  auto vgt_dma_size = regs.Get<reg::VGT_DMA_SIZE>();
  union {
    const void* index_buffer;
    const uint16_t* index_buffer_16;
    const uint32_t* index_buffer_32;
  };
  index_buffer = nullptr;
  xenos::Endian index_endian = vgt_dma_size.swap_mode;
  if (vgt_draw_initiator.source_select == xenos::SourceSelect::kDMA) {
    uint32_t index_buffer_base = regs[XE_GPU_REG_VGT_DMA_BASE];
    uint32_t index_buffer_read_count =
        std::min(uint32_t(vgt_draw_initiator.num_indices),
                 uint32_t(vgt_dma_size.num_words));
    if (vgt_draw_initiator.index_size == xenos::IndexFormat::kInt16) {
      // Handle the index endianness the same way as the PrimitiveProcessor.
      if (index_endian == xenos::Endian::k8in32) {
        index_endian = xenos::Endian::k8in16;
      } else if (index_endian == xenos::Endian::k16in32) {
        index_endian = xenos::Endian::kNone;
      }
      index_buffer_base &= ~uint32_t(sizeof(uint16_t) - 1);
      if (trace_writer_) {
        trace_writer_->WriteMemoryRead(
            index_buffer_base, sizeof(uint16_t) * index_buffer_read_count);
      }
    } else {
      assert_true(vgt_draw_initiator.index_size == xenos::IndexFormat::kInt32);
      index_buffer_base &= ~uint32_t(sizeof(uint32_t) - 1);
      if (trace_writer_) {
        trace_writer_->WriteMemoryRead(
            index_buffer_base, sizeof(uint32_t) * index_buffer_read_count);
      }
    }
    index_buffer = memory_.TranslatePhysical(index_buffer_base);
  }

  auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  uint32_t reset_index =
      regs.Get<reg::VGT_MULTI_PRIM_IB_RESET_INDX>().reset_indx;
  uint32_t index_offset = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
  uint32_t min_index = regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;
  uint32_t max_index = regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;

  uint32_t num_indices = vgt_draw_initiator.num_indices;
  cull_vertices_scratch_.clear();
  cull_vertices_scratch_.resize(num_indices);

  shader_interpreter_.SetShader(vertex_shader);

  PositionExportSink position_export_sink;
  shader_interpreter_.SetExportSink(&position_export_sink);
  for (uint32_t i = 0; i < num_indices; ++i) {
    CullVertex& out = cull_vertices_scratch_[i];
    out.valid = false;
    out.x = 0.0f;
    out.y = 0.0f;
    out.z = 0.0f;
    out.w = 0.0f;

    uint32_t vertex_index;
    if (vgt_draw_initiator.source_select == xenos::SourceSelect::kDMA) {
      if (i < vgt_dma_size.num_words && index_buffer) {
        if (vgt_draw_initiator.index_size == xenos::IndexFormat::kInt16) {
          vertex_index = index_buffer_16[i];
        } else {
          vertex_index = index_buffer_32[i];
        }
        // The Xenos only uses 24 bits of the index (reset_indx is 24-bit).
        vertex_index = xenos::GpuSwap(vertex_index, index_endian) & 0xFFFFFF;
      } else {
        vertex_index = 0;
      }
      if (pa_su_sc_mode_cntl.multi_prim_ib_ena && vertex_index == reset_index) {
        // Leave this slot invalid - it breaks the triangle it belongs to.
        continue;
      }
    } else {
      assert_true(vgt_draw_initiator.source_select ==
                  xenos::SourceSelect::kAutoIndex);
      vertex_index = i;
    }
    vertex_index =
        std::min(max_index,
                 std::max(min_index, (vertex_index + index_offset) & 0xFFFFFF));

    position_export_sink.Reset();
    shader_interpreter_.temp_registers()[0] = float(vertex_index);
    shader_interpreter_.Execute();

    if (position_export_sink.vertex_kill().has_value() &&
        (position_export_sink.vertex_kill().value() & ~(UINT32_C(1) << 31))) {
      continue;
    }
    if (!position_export_sink.position_x().has_value() ||
        !position_export_sink.position_y().has_value() ||
        !position_export_sink.position_w().has_value()) {
      continue;
    }
    out.x = position_export_sink.position_x().value();
    out.y = position_export_sink.position_y().value();
    out.z = position_export_sink.position_z().has_value()
                ? position_export_sink.position_z().value()
                : 0.0f;
    out.w = position_export_sink.position_w().value();
    out.valid = true;
  }
  shader_interpreter_.SetExportSink(nullptr);

  // Count a triangle if it would be dropped by EITHER a conservative frustum
  // test OR a backface test (counted once).
  //
  // FRUSTUM (conservative, orientation-independent, never over-counts): all 3
  // verts valid, w > 0, and all 3 beyond the SAME side clip plane (x>w / x<-w /
  // y>w / y<-w).
  //
  // BACKFACE (approximate - this is a read-only diagnostic, exact precision is
  // not needed): the signed area is the homogeneous determinant of the 3
  // clip-space (x, y, w) vectors. No perspective divide is needed (sign-exact
  // for w > 0), it is just a few multiply-adds (single-precision, NEON-friendly
  // on the Cortex-X3). face == 0 means the host front face is CCW
  // (PA_SU_SC_MODE_CNTL; matches UpdateDynamicState's VkFrontFace derivation).
  // NOTE: the det>0 <-> CCW mapping assumes the guest clip-space Y orientation;
  // if a device A/B shows the count is the COMPLEMENT of expected, flip the det
  // sign. On closed meshes front/back faces are ~50/50, so the droppable-fraction
  // magnitude (the decision the counter exists to inform) is robust either way.
  const bool cull_front = pa_su_sc_mode_cntl.cull_front != 0;
  const bool cull_back = pa_su_sc_mode_cntl.cull_back != 0;
  const bool front_is_ccw = pa_su_sc_mode_cntl.face == 0;
  uint32_t cullable = 0;
  // Test one triangle (3 scratch slots). `winding_reversed` flips the front/back
  // sense for the odd triangles of a strip (the GPU alternates strip winding). The
  // frustum (same-side clip plane) test is orientation-independent, so it is
  // unaffected. List triangles always pass winding_reversed=false, so the list
  // count is byte-for-byte unchanged from before.
  auto count_if_cullable = [&](uint32_t ia, uint32_t ib, uint32_t ic,
                               bool winding_reversed) {
    const CullVertex& a = cull_vertices_scratch_[ia];
    const CullVertex& b = cull_vertices_scratch_[ib];
    const CullVertex& c = cull_vertices_scratch_[ic];
    if (!a.valid || !b.valid || !c.valid) {
      return;
    }
    if (!(a.w > 0.0f && b.w > 0.0f && c.w > 0.0f)) {
      return;
    }
    bool cull = (a.x > a.w && b.x > b.w && c.x > c.w) ||
                (a.x < -a.w && b.x < -b.w && c.x < -c.w) ||
                (a.y > a.w && b.y > b.w && c.y > c.w) ||
                (a.y < -a.w && b.y < -b.w && c.y < -c.w);
    if (!cull && (cull_front || cull_back)) {
      // Homogeneous signed-area determinant (2x area, divide-free).
      float det = a.x * (b.y * c.w - c.y * b.w) -
                  a.y * (b.x * c.w - c.x * b.w) +
                  a.w * (b.x * c.y - c.x * b.y);
      if (cull_front && cull_back) {
        // Both faces culled - every polygonal triangle is dropped.
        cull = true;
      } else if (det != 0.0f) {
        bool tri_front = ((det > 0.0f) == front_is_ccw) != winding_reversed;
        cull = tri_front ? cull_front : cull_back;
      }
    }
    if (cull) {
      ++cullable;
    }
  };
  if (prim_is_strip) {
    // Strip: triangle t = vertices (t, t+1, t+2); winding alternates by parity. A
    // reset/invalid vertex (primitive restart) invalidates any triangle containing
    // it (skipped above), which also breaks the strip there; winding parity is not
    // re-based after a restart - acceptable for an approximate sizing counter
    // (front/back ~50/50 on closed meshes, so the droppable-fraction MAGNITUDE this
    // counter exists to inform is robust).
    for (uint32_t i = 0; i + 2 < num_indices; ++i) {
      count_if_cullable(i, i + 1, i + 2, (i & 1u) != 0u);
    }
  } else {
    uint32_t triangle_count = num_indices / 3;
    for (uint32_t t = 0; t < triangle_count; ++t) {
      count_if_cullable(t * 3 + 0u, t * 3 + 1u, t * 3 + 2u, false);
    }
  }
  return cullable;
}

// Gauss-Jordan inverse of a 4x4 (partial pivoting). Returns false if singular.
static bool Invert4x4(const double in[4][4], double out[4][4]) {
  double a[4][8];
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      a[r][c] = in[r][c];
      a[r][c + 4] = (r == c) ? 1.0 : 0.0;
    }
  }
  for (int col = 0; col < 4; ++col) {
    int pivot = col;
    double best = a[col][col] < 0 ? -a[col][col] : a[col][col];
    for (int r = col + 1; r < 4; ++r) {
      double v = a[r][col] < 0 ? -a[r][col] : a[r][col];
      if (v > best) {
        best = v;
        pivot = r;
      }
    }
    if (best < 1e-12) {
      return false;
    }
    if (pivot != col) {
      for (int c = 0; c < 8; ++c) {
        double t = a[col][c];
        a[col][c] = a[pivot][c];
        a[pivot][c] = t;
      }
    }
    double inv_p = 1.0 / a[col][col];
    for (int c = 0; c < 8; ++c) {
      a[col][c] *= inv_p;
    }
    for (int r = 0; r < 4; ++r) {
      if (r == col) {
        continue;
      }
      double f = a[r][col];
      for (int c = 0; c < 8; ++c) {
        a[r][c] -= f * a[col][c];
      }
    }
  }
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      out[r][c] = a[r][c + 4];
    }
  }
  return true;
}

DrawExtentEstimator::AffineValidateStatus
DrawExtentEstimator::ValidateAffinePositionReplay(const Shader& vertex_shader) {
  affine_validate_max_error_ = 0.0f;
  const RegisterFile& regs = register_file_;
  auto vgt_draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
  if (!vgt_draw_initiator.num_indices ||
      vgt_draw_initiator.source_select != xenos::SourceSelect::kDMA ||
      !vertex_shader.is_ucode_analyzed() ||
      !ShaderInterpreter::CanInterpretShader(vertex_shader)) {
    return AffineValidateStatus::kSkipped;
  }

  // The position slice's single register leaf = the vfetch'd input position.
  uint64_t read_regs = 0, written_regs = 0;
  for (const ParsedAluInstruction& op : vertex_shader.position_slice_ops()) {
    auto mark_operand = [&](const InstructionOperand& o) {
      if (o.storage_source == InstructionStorageSource::kRegister &&
          o.storage_addressing_mode ==
              InstructionStorageAddressingMode::kAbsolute &&
          o.storage_index < 64) {
        read_regs |= uint64_t(1) << o.storage_index;
      }
    };
    auto mark_result = [&](const InstructionResult& r) {
      if (r.storage_target == InstructionStorageTarget::kRegister &&
          r.storage_addressing_mode ==
              InstructionStorageAddressingMode::kAbsolute &&
          r.storage_index < 64) {
        written_regs |= uint64_t(1) << r.storage_index;
      }
    };
    for (uint32_t i = 0; i < op.vector_operand_count; ++i) {
      mark_operand(op.vector_operands[i]);
    }
    for (uint32_t i = 0; i < op.scalar_operand_count; ++i) {
      mark_operand(op.scalar_operands[i]);
    }
    mark_result(op.vector_and_constant_result);
    mark_result(op.scalar_result);
  }
  uint64_t leaf_regs = read_regs & ~written_regs;
  if (leaf_regs == 0 || (leaf_regs & (leaf_regs - 1)) != 0) {
    return AffineValidateStatus::kUnsupported;
  }
  uint32_t leaf = 0;
  while (!((leaf_regs >> leaf) & 1)) {
    ++leaf;
  }

  auto vgt_dma_size = regs.Get<reg::VGT_DMA_SIZE>();
  const uint32_t stride =
      vgt_draw_initiator.index_size == xenos::IndexFormat::kInt16 ? 2u : 4u;
  xenos::Endian index_endian = vgt_dma_size.swap_mode;
  uint32_t index_buffer_base = regs[XE_GPU_REG_VGT_DMA_BASE];
  if (stride == 2u) {
    if (index_endian == xenos::Endian::k8in32) {
      index_endian = xenos::Endian::k8in16;
    } else if (index_endian == xenos::Endian::k16in32) {
      index_endian = xenos::Endian::kNone;
    }
    index_buffer_base &= ~uint32_t(1);
  } else {
    index_buffer_base &= ~uint32_t(3);
  }
  const uint8_t* index_bytes =
      memory_.TranslatePhysical<const uint8_t*>(index_buffer_base);
  if (!index_bytes) {
    return AffineValidateStatus::kSkipped;
  }
  const uint16_t* index16 = reinterpret_cast<const uint16_t*>(index_bytes);
  const uint32_t* index32 = reinterpret_cast<const uint32_t*>(index_bytes);
  uint32_t index_offset = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
  uint32_t min_index = regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;
  uint32_t max_index = regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;
  uint32_t num_indices = vgt_draw_initiator.num_indices;
  const uint32_t kMaxSamples = 16;
  uint32_t sample_count = std::min(num_indices, kMaxSamples);
  if (sample_count < 5) {
    return AffineValidateStatus::kSkipped;
  }

  shader_interpreter_.SetShader(vertex_shader);
  PositionExportSink sink;
  shader_interpreter_.SetExportSink(&sink);
  double XtX[4][4] = {};
  double Xty[4][4] = {};
  struct Sample {
    float p[4];
    float clip[4];
  } samples[kMaxSamples];
  uint32_t valid_samples = 0;
  for (uint32_t s = 0; s < sample_count; ++s) {
    uint32_t vertex_index;
    if (s < vgt_dma_size.num_words) {
      vertex_index = stride == 2u ? uint32_t(index16[s]) : index32[s];
      vertex_index = xenos::GpuSwap(vertex_index, index_endian) & 0xFFFFFF;
    } else {
      vertex_index = 0;
    }
    vertex_index =
        std::min(max_index,
                 std::max(min_index, (vertex_index + index_offset) & 0xFFFFFF));
    sink.Reset();
    shader_interpreter_.temp_registers()[0] = float(vertex_index);
    shader_interpreter_.Execute();
    if (!sink.position_x().has_value() || !sink.position_y().has_value() ||
        !sink.position_w().has_value()) {
      continue;
    }
    Sample& smp = samples[valid_samples];
    const float* p = &shader_interpreter_.temp_registers()[leaf * 4];
    smp.p[0] = p[0];
    smp.p[1] = p[1];
    smp.p[2] = p[2];
    smp.p[3] = 1.0f;
    smp.clip[0] = sink.position_x().value();
    smp.clip[1] = sink.position_y().value();
    smp.clip[2] =
        sink.position_z().has_value() ? sink.position_z().value() : 0.0f;
    smp.clip[3] = sink.position_w().value();
    for (int a = 0; a < 4; ++a) {
      for (int b = 0; b < 4; ++b) {
        XtX[a][b] += double(smp.p[a]) * smp.p[b];
      }
      for (int k = 0; k < 4; ++k) {
        Xty[a][k] += double(smp.p[a]) * smp.clip[k];
      }
    }
    ++valid_samples;
  }
  shader_interpreter_.SetExportSink(nullptr);
  if (valid_samples < 5) {
    return AffineValidateStatus::kUnsupported;
  }

  for (int a = 0; a < 4; ++a) {
    XtX[a][a] += 1e-6;
  }
  double inv[4][4];
  if (!Invert4x4(XtX, inv)) {
    return AffineValidateStatus::kUnsupported;
  }
  double M[4][4];  // clip_k = sum_a M[k][a] * p[a]
  for (int k = 0; k < 4; ++k) {
    for (int a = 0; a < 4; ++a) {
      double v = 0.0;
      for (int b = 0; b < 4; ++b) {
        v += inv[a][b] * Xty[b][k];
      }
      M[k][a] = v;
    }
  }
  float max_rel = 0.0f;
  for (uint32_t s = 0; s < valid_samples; ++s) {
    const Sample& smp = samples[s];
    for (int k = 0; k < 4; ++k) {
      double pred = 0.0;
      for (int a = 0; a < 4; ++a) {
        pred += M[k][a] * smp.p[a];
      }
      double err = pred - smp.clip[k];
      if (err < 0.0) {
        err = -err;
      }
      double denom = (smp.clip[k] < 0.0f ? -smp.clip[k] : smp.clip[k]) + 1e-3;
      float rel = float(err / denom);
      if (rel > max_rel) {
        max_rel = rel;
      }
    }
  }
  affine_validate_max_error_ = max_rel;
  return max_rel < 1.0e-3f ? AffineValidateStatus::kAffine
                           : AffineValidateStatus::kNonAffine;
}

// Decode a vertex's position-attribute input (xyz) for the fast replay, matching
// ShaderInterpreter::ExecuteVertexFetchInstruction. Used for BOTH the M-recovery
// basis and the per-vertex apply, so the recovered M is self-consistent with the
// decode (never relies on the interpreter's leaf register, which the color path
// may overwrite).
static void DecodeFastInput(const DrawExtentEstimator::FastAffineReplay& fr,
                            const uint32_t* membase, uint32_t vertex_index,
                            float out_p[3]) {
  uint32_t addr = fr.stride_dwords * vertex_index + fr.base_dwords +
                  uint32_t(fr.offset_dwords);
  out_p[0] = out_p[1] = out_p[2] = 0.0f;
  if (fr.format == xenos::VertexFormat::k_16_16_16_16_FLOAT ||
      fr.format == xenos::VertexFormat::k_16_16_FLOAT) {
    if (addr < fr.end_dwords) {
      uint32_t d = xenos::GpuSwap(membase[addr], fr.endian);
      out_p[0] = xe::xenos_half_to_float(uint16_t(d));
      out_p[1] = xe::xenos_half_to_float(uint16_t(d >> 16));
    }
    if (fr.format == xenos::VertexFormat::k_16_16_16_16_FLOAT &&
        addr + 1u < fr.end_dwords) {
      uint32_t d = xenos::GpuSwap(membase[addr + 1u], fr.endian);
      out_p[2] = xe::xenos_half_to_float(uint16_t(d));
    }
  } else {
    if (addr < fr.end_dwords) {
      uint32_t d = xenos::GpuSwap(membase[addr], fr.endian);
      std::memcpy(&out_p[0], &d, sizeof(float));
    }
    if (addr + 1u < fr.end_dwords) {
      uint32_t d = xenos::GpuSwap(membase[addr + 1u], fr.endian);
      std::memcpy(&out_p[1], &d, sizeof(float));
    }
    if (addr + 2u < fr.end_dwords) {
      uint32_t d = xenos::GpuSwap(membase[addr + 2u], fr.endian);
      std::memcpy(&out_p[2], &d, sizeof(float));
    }
  }
}

bool DrawExtentEstimator::SetupFastAffineReplay(const Shader& vertex_shader,
                                                FastAffineReplay& out) {
  // 1. The position slice's single register leaf input.
  uint64_t read_regs = 0, written_regs = 0;
  for (const ParsedAluInstruction& op : vertex_shader.position_slice_ops()) {
    auto mark_operand = [&](const InstructionOperand& o) {
      if (o.storage_source == InstructionStorageSource::kRegister &&
          o.storage_addressing_mode ==
              InstructionStorageAddressingMode::kAbsolute &&
          o.storage_index < 64) {
        read_regs |= uint64_t(1) << o.storage_index;
      }
    };
    auto mark_result = [&](const InstructionResult& r) {
      if (r.storage_target == InstructionStorageTarget::kRegister &&
          r.storage_addressing_mode ==
              InstructionStorageAddressingMode::kAbsolute &&
          r.storage_index < 64) {
        written_regs |= uint64_t(1) << r.storage_index;
      }
    };
    for (uint32_t i = 0; i < op.vector_operand_count; ++i) {
      mark_operand(op.vector_operands[i]);
    }
    for (uint32_t i = 0; i < op.scalar_operand_count; ++i) {
      mark_operand(op.scalar_operands[i]);
    }
    mark_result(op.vector_and_constant_result);
    mark_result(op.scalar_result);
  }
  uint64_t leaf_regs = read_regs & ~written_regs;
  if (leaf_regs == 0 || (leaf_regs & (leaf_regs - 1)) != 0) {
    return false;
  }
  uint32_t leaf = 0;
  while (!((leaf_regs >> leaf) & 1)) {
    ++leaf;
  }

  // 2. The vfetch attribute that writes the leaf, and its decode params.
  const RegisterFile& regs = register_file_;
  bool found = false;
  for (const Shader::VertexBinding& binding : vertex_shader.vertex_bindings()) {
    for (const Shader::VertexBinding::Attribute& attr : binding.attributes) {
      const ParsedVertexFetchInstruction& fi = attr.fetch_instr;
      if (fi.result.storage_target != InstructionStorageTarget::kRegister ||
          fi.result.storage_index != leaf) {
        continue;
      }
      xenos::VertexFormat fmt = fi.attributes.data_format;
      if (fmt != xenos::VertexFormat::k_32_32_32_FLOAT &&
          fmt != xenos::VertexFormat::k_32_32_32_32_FLOAT &&
          fmt != xenos::VertexFormat::k_16_16_16_16_FLOAT &&
          fmt != xenos::VertexFormat::k_16_16_FLOAT) {
        return false;  // unsupported position format for the fast path
      }
      out.format = fmt;
      xenos::xe_gpu_vertex_fetch_t fc = regs.GetVertexFetch(binding.fetch_constant);
      out.base_dwords = fc.address;
      out.end_dwords = fc.address + fc.size;
      out.endian = fc.endian;
      out.stride_dwords =
          fi.attributes.stride ? fi.attributes.stride : binding.stride_words;
      out.offset_dwords = fi.attributes.offset;
      found = true;
      break;
    }
    if (found) {
      break;
    }
  }
  if (!found || !out.stride_dwords) {
    return false;
  }

  // 3. Recover M from interpreter basis samples spread across the draw.
  auto vgt_draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
  if (vgt_draw_initiator.source_select != xenos::SourceSelect::kDMA) {
    return false;
  }
  auto vgt_dma_size = regs.Get<reg::VGT_DMA_SIZE>();
  const uint32_t index_stride =
      vgt_draw_initiator.index_size == xenos::IndexFormat::kInt16 ? 2u : 4u;
  xenos::Endian index_endian = vgt_dma_size.swap_mode;
  uint32_t index_buffer_base = regs[XE_GPU_REG_VGT_DMA_BASE];
  if (index_stride == 2u) {
    if (index_endian == xenos::Endian::k8in32) {
      index_endian = xenos::Endian::k8in16;
    } else if (index_endian == xenos::Endian::k16in32) {
      index_endian = xenos::Endian::kNone;
    }
    index_buffer_base &= ~uint32_t(1);
  } else {
    index_buffer_base &= ~uint32_t(3);
  }
  const uint8_t* index_bytes =
      memory_.TranslatePhysical<const uint8_t*>(index_buffer_base);
  if (!index_bytes) {
    return false;
  }
  const uint16_t* index16 = reinterpret_cast<const uint16_t*>(index_bytes);
  const uint32_t* index32 = reinterpret_cast<const uint32_t*>(index_bytes);
  uint32_t index_offset = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
  uint32_t min_index = regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;
  uint32_t max_index = regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;
  uint32_t num_indices = vgt_draw_initiator.num_indices;
  // 6 samples: 4 independent points exactly determine an affine M; 6 (spread +
  // regularized) for robustness against a degenerate/coplanar basis.
  const uint32_t kSamples = 6;
  if (num_indices < 5) {
    return false;
  }

  const uint32_t* membase =
      reinterpret_cast<const uint32_t*>(memory_.physical_membase());
  shader_interpreter_.SetShader(vertex_shader);
  PositionExportSink sink;
  shader_interpreter_.SetExportSink(&sink);
  double XtX[4][4] = {};
  double Xty[4][4] = {};
  double samp_p[8][4];
  double samp_clip[8][4];
  uint32_t valid_samples = 0;
  for (uint32_t s = 0; s < kSamples; ++s) {
    // Spread the basis across the draw to span the geometry (avoids a degenerate,
    // coplanar basis for a 3D mesh).
    uint32_t si = uint32_t(uint64_t(s) * num_indices / kSamples);
    uint32_t vertex_index;
    if (si < vgt_dma_size.num_words) {
      vertex_index = index_stride == 2u ? uint32_t(index16[si]) : index32[si];
      vertex_index = xenos::GpuSwap(vertex_index, index_endian) & 0xFFFFFF;
    } else {
      vertex_index = 0;
    }
    vertex_index =
        std::min(max_index,
                 std::max(min_index, (vertex_index + index_offset) & 0xFFFFFF));
    sink.Reset();
    shader_interpreter_.temp_registers()[0] = float(vertex_index);
    shader_interpreter_.Execute();
    if (!sink.position_x().has_value() || !sink.position_y().has_value() ||
        !sink.position_w().has_value()) {
      continue;
    }
    // Decode the input the SAME way the apply will (not via the interpreter's leaf
    // register, which the color path may have overwritten by now) so M is fit and
    // applied consistently.
    float pf[3];
    DecodeFastInput(out, membase, vertex_index, pf);
    double pv[4] = {pf[0], pf[1], pf[2], 1.0};
    double clip[4] = {
        sink.position_x().value(), sink.position_y().value(),
        sink.position_z().has_value() ? sink.position_z().value() : 0.0f,
        sink.position_w().value()};
    for (int a = 0; a < 4; ++a) {
      for (int b = 0; b < 4; ++b) {
        XtX[a][b] += pv[a] * pv[b];
      }
      for (int k = 0; k < 4; ++k) {
        Xty[a][k] += pv[a] * clip[k];
      }
      samp_p[valid_samples][a] = pv[a];
      samp_clip[valid_samples][a] = clip[a];
    }
    ++valid_samples;
  }
  shader_interpreter_.SetExportSink(nullptr);
  if (valid_samples < 5) {
    return false;
  }
  for (int a = 0; a < 4; ++a) {
    XtX[a][a] += 1e-6;
  }
  double inv[4][4];
  if (!Invert4x4(XtX, inv)) {
    return false;
  }
  for (int k = 0; k < 4; ++k) {
    for (int a = 0; a < 4; ++a) {
      double v = 0.0;
      for (int b = 0; b < 4; ++b) {
        v += inv[a][b] * Xty[b][k];
      }
      out.m[k][a] = v;
    }
  }
  // Self-validate: M must reproduce the basis samples. If not (e.g. position
  // depends on the input's 4th component or the format assumption is wrong), bail
  // so the caller uses the exact interpreter path - never emit a wrong cull.
  for (uint32_t s = 0; s < valid_samples; ++s) {
    for (int k = 0; k < 4; ++k) {
      double pred = out.m[k][0] * samp_p[s][0] + out.m[k][1] * samp_p[s][1] +
                    out.m[k][2] * samp_p[s][2] + out.m[k][3] * samp_p[s][3];
      double err = pred - samp_clip[s][k];
      if (err < 0.0) {
        err = -err;
      }
      double denom =
          (samp_clip[s][k] < 0.0 ? -samp_clip[s][k] : samp_clip[s][k]) + 1e-3;
      if (err / denom > 1.0e-2) {
        return false;
      }
    }
  }
  return true;
}

bool DrawExtentEstimator::BuildCulledIndexList(const Shader& vertex_shader) {
  SCOPE_profile_cpu_f("gpu");
  cull_emit_index_bytes_.clear();
  cull_emit_index_count_ = 0;
  cull_emit_index_stride_ = 0;
  cull_emit_dropped_triangles_ = 0;

  const RegisterFile& regs = register_file_;
  auto vgt_draw_initiator = regs.Get<reg::VGT_DRAW_INITIATOR>();
  if (!vgt_draw_initiator.num_indices) {
    cull_bail_reason_ = CullBail::kNoIndices;
    return false;
  }
  const bool prim_is_strip =
      vgt_draw_initiator.prim_type == xenos::PrimitiveType::kTriangleStrip;
  if (vgt_draw_initiator.prim_type != xenos::PrimitiveType::kTriangleList &&
      !prim_is_strip) {
    cull_bail_reason_ = CullBail::kNotTriList;
    return false;
  }
  // Only guest-DMA indices: we copy the raw guest index entries for kept tris.
  if (vgt_draw_initiator.source_select != xenos::SourceSelect::kDMA) {
    cull_bail_reason_ = CullBail::kNotDMA;
    return false;
  }
  // Not reproducing tessellation.
  if (xenos::IsMajorModeExplicit(vgt_draw_initiator.major_mode,
                                 vgt_draw_initiator.prim_type) &&
      regs.Get<reg::VGT_OUTPUT_PATH_CNTL>().path_select ==
          xenos::VGTOutputPath::kTessellationEnable) {
    cull_bail_reason_ = CullBail::kTessellation;
    return false;
  }
  if (!vertex_shader.is_ucode_analyzed() ||
      !ShaderInterpreter::CanInterpretShader(vertex_shader)) {
    cull_bail_reason_ = CullBail::kNotInterpretable;
    return false;
  }
  if (regs.Get<reg::PA_CL_VTE_CNTL>().vtx_xy_fmt != 0) {
    // Pre-divided (screen/NDC) positions - the clip-plane test assumes clip
    // space; emit verbatim rather than misjudge.
    cull_bail_reason_ = CullBail::kVtxXyFmt;
    return false;
  }
  if (regs.Get<reg::PA_CL_CLIP_CNTL>().clip_disable != 0) {
    cull_bail_reason_ = CullBail::kClipDisable;
    return false;
  }
  auto pa_su_sc_mode_cntl = regs.Get<reg::PA_SU_SC_MODE_CNTL>();
  // Primitive restart: handled (NOT bailed). Reset-index slots break the strip into
  // sub-strips; triangles spanning a reset are dropped and winding parity re-bases
  // per sub-strip (see the emit loop below).
  const bool restart_enabled = pa_su_sc_mode_cntl.multi_prim_ib_ena != 0;
  const uint32_t reset_index =
      regs.Get<reg::VGT_MULTI_PRIM_IB_RESET_INDX>().reset_indx;

  auto vgt_dma_size = regs.Get<reg::VGT_DMA_SIZE>();
  const uint32_t stride =
      vgt_draw_initiator.index_size == xenos::IndexFormat::kInt16 ? 2u : 4u;
  cull_emit_index_stride_ = stride;
  xenos::Endian index_endian = vgt_dma_size.swap_mode;
  uint32_t index_buffer_base = regs[XE_GPU_REG_VGT_DMA_BASE];
  if (stride == 2u) {
    // Handle index endianness the same way as the PrimitiveProcessor / counter.
    if (index_endian == xenos::Endian::k8in32) {
      index_endian = xenos::Endian::k8in16;
    } else if (index_endian == xenos::Endian::k16in32) {
      index_endian = xenos::Endian::kNone;
    }
    index_buffer_base &= ~uint32_t(sizeof(uint16_t) - 1);
  } else {
    index_buffer_base &= ~uint32_t(sizeof(uint32_t) - 1);
  }
  const uint8_t* index_bytes =
      memory_.TranslatePhysical<const uint8_t*>(index_buffer_base);
  if (!index_bytes) {
    cull_bail_reason_ = CullBail::kNoIndexPtr;
    return false;
  }
  const uint16_t* index16 = reinterpret_cast<const uint16_t*>(index_bytes);
  const uint32_t* index32 = reinterpret_cast<const uint32_t*>(index_bytes);

  uint32_t index_offset = regs.Get<reg::VGT_INDX_OFFSET>().indx_offset;
  uint32_t min_index = regs.Get<reg::VGT_MIN_VTX_INDX>().min_indx;
  uint32_t max_index = regs.Get<reg::VGT_MAX_VTX_INDX>().max_indx;
  uint32_t num_indices = vgt_draw_initiator.num_indices;

  cull_vertices_scratch_.clear();
  cull_vertices_scratch_.resize(num_indices);

  // Fast affine replay (gpu_cull_fast_replay): recover the position matrix M once
  // and compute each vertex's clip via a 3-float decode + 4 dp4s, instead of the
  // full ShaderInterpreter per vertex (~orders of magnitude cheaper, exact since
  // position is affine). Falls back to the interpreter when the position input is
  // not a single k_32_32_32_FLOAT vfetch or M can't be recovered.
  FastAffineReplay fast_replay;
  const bool fast = cvars::gpu_cull_fast_replay &&
                    SetupFastAffineReplay(vertex_shader, fast_replay);
  const uint32_t* membase =
      fast ? reinterpret_cast<const uint32_t*>(memory_.physical_membase())
           : nullptr;

  shader_interpreter_.SetShader(vertex_shader);
  PositionExportSink position_export_sink;
  shader_interpreter_.SetExportSink(&position_export_sink);
  for (uint32_t i = 0; i < num_indices; ++i) {
    CullVertex& out = cull_vertices_scratch_[i];
    out.valid = false;
    out.is_reset = false;
    out.x = out.y = out.z = out.w = 0.0f;
    uint32_t vertex_index;
    if (i < vgt_dma_size.num_words) {
      vertex_index = stride == 2u ? uint32_t(index16[i]) : index32[i];
      vertex_index = xenos::GpuSwap(vertex_index, index_endian) & 0xFFFFFF;
    } else {
      vertex_index = 0;
    }
    if (restart_enabled && vertex_index == reset_index) {
      // Primitive-restart marker: not a real vertex, breaks the strip here.
      out.is_reset = true;
      continue;
    }
    vertex_index =
        std::min(max_index,
                 std::max(min_index, (vertex_index + index_offset) & 0xFFFFFF));
    if (fast) {
      float p[3];
      DecodeFastInput(fast_replay, membase, vertex_index, p);
      const double(&m)[4][4] = fast_replay.m;
      out.x = float(m[0][0] * p[0] + m[0][1] * p[1] + m[0][2] * p[2] + m[0][3]);
      out.y = float(m[1][0] * p[0] + m[1][1] * p[1] + m[1][2] * p[2] + m[1][3]);
      out.z = float(m[2][0] * p[0] + m[2][1] * p[1] + m[2][2] * p[2] + m[2][3]);
      out.w = float(m[3][0] * p[0] + m[3][1] * p[1] + m[3][2] * p[2] + m[3][3]);
      out.valid = true;
      continue;
    }
    position_export_sink.Reset();
    shader_interpreter_.temp_registers()[0] = float(vertex_index);
    shader_interpreter_.Execute();
    if (position_export_sink.vertex_kill().has_value() &&
        (position_export_sink.vertex_kill().value() & ~(UINT32_C(1) << 31))) {
      continue;
    }
    if (!position_export_sink.position_x().has_value() ||
        !position_export_sink.position_y().has_value() ||
        !position_export_sink.position_w().has_value()) {
      continue;
    }
    out.x = position_export_sink.position_x().value();
    out.y = position_export_sink.position_y().value();
    out.z = position_export_sink.position_z().has_value()
                ? position_export_sink.position_z().value()
                : 0.0f;
    out.w = position_export_sink.position_w().value();
    out.valid = true;
  }
  shader_interpreter_.SetExportSink(nullptr);

  const bool cull_front = pa_su_sc_mode_cntl.cull_front != 0;
  const bool cull_back = pa_su_sc_mode_cntl.cull_back != 0;
  const bool front_is_ccw = pa_su_sc_mode_cntl.face == 0;

  const uint32_t max_tris =
      prim_is_strip ? (num_indices >= 2u ? num_indices - 2u : 0u)
                    : num_indices / 3u;
  cull_emit_index_bytes_.reserve(size_t(max_tris) * 3u * stride);

  uint32_t dropped_triangles = 0;
  // Append slot `slot`'s RAW guest index bytes (pre-swap/pre-offset) to the list.
  auto append_raw = [&](uint32_t slot) {
    const uint8_t* src = index_bytes + size_t(slot) * stride;
    cull_emit_index_bytes_.insert(cull_emit_index_bytes_.end(), src,
                                  src + stride);
    ++cull_emit_index_count_;
  };
  // CONSERVATIVE: drop only triangles that are CLEARLY backface or fully outside
  // one frustum side; keep everything else (invalid/killed/near-degenerate/edge-on).
  // Cull decision uses the slot order + winding_reversed (identical to the counter);
  // emit order swaps the first two slots for odd strip triangles so the output LIST
  // triangle keeps the strip's actual winding (else the GPU would cull the wrong
  // face -> holes). Relative |det| margin keeps near-edge-on triangles even if the
  // CPU position differs slightly from the GPU's.
  auto emit_if_kept = [&](uint32_t ia, uint32_t ib, uint32_t ic,
                          bool winding_reversed) {
    const CullVertex& a = cull_vertices_scratch_[ia];
    const CullVertex& b = cull_vertices_scratch_[ib];
    const CullVertex& c = cull_vertices_scratch_[ic];
    bool cull = false;
    if (a.valid && b.valid && c.valid && a.w > 0.0f && b.w > 0.0f &&
        c.w > 0.0f) {
      cull = (a.x > a.w && b.x > b.w && c.x > c.w) ||
             (a.x < -a.w && b.x < -b.w && c.x < -c.w) ||
             (a.y > a.w && b.y > b.w && c.y > c.w) ||
             (a.y < -a.w && b.y < -b.w && c.y < -c.w);
      if (!cull && (cull_front || cull_back)) {
        if (cull_front && cull_back) {
          cull = true;
        } else {
          float t0 = b.y * c.w - c.y * b.w;
          float t1 = b.x * c.w - c.x * b.w;
          float t2 = b.x * c.y - c.x * b.y;
          float det = a.x * t0 - a.y * t1 + a.w * t2;
          float abs_a_x = a.x < 0.0f ? -a.x : a.x;
          float abs_a_y = a.y < 0.0f ? -a.y : a.y;
          float abs_a_w = a.w < 0.0f ? -a.w : a.w;
          float abs_t0 = t0 < 0.0f ? -t0 : t0;
          float abs_t1 = t1 < 0.0f ? -t1 : t1;
          float abs_t2 = t2 < 0.0f ? -t2 : t2;
          float abs_det = det < 0.0f ? -det : det;
          // Relative edge-on margin: keep triangles whose facing is ambiguous.
          float scale =
              abs_a_x * abs_t0 + abs_a_y * abs_t1 + abs_a_w * abs_t2;
          if (abs_det > (1.0f / 1024.0f) * scale) {
            bool tri_front =
                ((det > 0.0f) == front_is_ccw) != winding_reversed;
            cull = tri_front ? cull_front : cull_back;
          }
        }
      }
    }
    if (cull) {
      ++dropped_triangles;
      return;
    }
    if (winding_reversed) {
      append_raw(ib);
      append_raw(ia);
      append_raw(ic);
    } else {
      append_raw(ia);
      append_raw(ib);
      append_raw(ic);
    }
  };
  if (prim_is_strip) {
    // Walk maximal runs of non-reset slots (sub-strips). Within each, emit
    // triangles (j,j+1,j+2) with winding parity RE-BASED to the sub-strip start so
    // strip-restart geometry keeps correct winding; triangles spanning a reset are
    // simply never formed.
    uint32_t i = 0;
    while (i < num_indices) {
      if (cull_vertices_scratch_[i].is_reset) {
        ++i;
        continue;
      }
      uint32_t s = i;
      while (i < num_indices && !cull_vertices_scratch_[i].is_reset) {
        ++i;
      }
      const uint32_t e = i;  // sub-strip [s, e)
      for (uint32_t j = s; j + 2u < e; ++j) {
        emit_if_kept(j, j + 1u, j + 2u, ((j - s) & 1u) != 0u);
      }
    }
  } else {
    uint32_t triangle_count = num_indices / 3u;
    for (uint32_t t = 0; t < triangle_count; ++t) {
      const uint32_t a = t * 3u + 0u, b = t * 3u + 1u, c = t * 3u + 2u;
      // A list triangle containing a reset marker is not a real primitive.
      if (cull_vertices_scratch_[a].is_reset ||
          cull_vertices_scratch_[b].is_reset ||
          cull_vertices_scratch_[c].is_reset) {
        continue;
      }
      emit_if_kept(a, b, c, false);
    }
  }
  // Only worth the strip->list flattening + topology change if we actually
  // removed triangles; otherwise the caller should draw the originals verbatim.
  cull_emit_dropped_triangles_ = dropped_triangles;
  cull_bail_reason_ =
      dropped_triangles > 0 ? CullBail::kBuilt : CullBail::kZeroDropped;
  return dropped_triangles > 0;
}

}  // namespace gpu
}  // namespace xe
