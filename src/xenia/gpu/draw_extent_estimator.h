/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_DRAW_EXTENT_ESTIMATOR_H_
#define XENIA_GPU_DRAW_EXTENT_ESTIMATOR_H_

#include <cstdint>
#include <optional>
#include <vector>

#include "xenia/gpu/register_file.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/shader_interpreter.h"
#include "xenia/gpu/trace_writer.h"
#include "xenia/memory.h"

namespace xe {
namespace gpu {

class DrawExtentEstimator {
 public:
  DrawExtentEstimator(const RegisterFile& register_file, const Memory& memory,
                      TraceWriter* trace_writer)
      : register_file_(register_file),
        memory_(memory),
        trace_writer_(trace_writer),
        shader_interpreter_(register_file, memory) {
    shader_interpreter_.SetTraceWriter(trace_writer);
  }

  // The shader must have its ucode analyzed.
  uint32_t EstimateVertexMaxY(const Shader& vertex_shader);
  uint32_t EstimateMaxY(bool try_to_estimate_vertex_max_y,
                        const Shader& vertex_shader);

  // Front B cullable-triangle counter (gpu_trace_cullable_tris): replays the
  // guest VS positions on the CPU and counts how many triangles a CPU-side cull
  // WOULD drop before the GPU bins them - a READ-ONLY decision instrument (never
  // mutates geometry) that sizes the potential of a triangle cull against the
  // GPU binning bottleneck. Counts a triangle if it is either provably FULLY
  // OUTSIDE one side clip plane (conservative frustum) OR backface-culled per
  // PA_SU_SC_MODE_CNTL (approximate: single-precision homogeneous signed-area
  // determinant, no perspective divide). Z-plane culling is omitted to stay
  // conservative. Returns 0 when the shader can't be interpreted (texture-fetch
  // VS), the draw isn't a triangle list, or it uses pre-divided (vtx_xy_fmt)
  // positions.
  uint32_t CountCullableTriangles(const Shader& vertex_shader);

  // Lever 2 cull Step 2a: build a CONSERVATIVELY culled index list for the current
  // kGuestDMA triangle-list/strip draw - the emit counterpart of
  // CountCullableTriangles. Replays the guest VS positions, drops only triangles
  // that are CLEARLY backface (|det| beyond a relative margin) or fully outside one
  // frustum side, and writes the surviving triangles' RAW guest index entries (so
  // the GPU's own swap + VGT_INDX_OFFSET reproduce the same vertices) as a triangle
  // LIST (strips are converted; odd strip triangles have their first two indices
  // swapped to preserve winding). Conservative: KEEPS any triangle with an invalid/
  // killed/missing-position or near-degenerate vertex. Returns false (caller should
  // draw the indices verbatim) for any draw it can't safely cull: non-DMA, non-tri,
  // tessellated, primitive-restart, pre-divided (vtx_xy_fmt), clip_disable, or a
  // non-interpretable (texture-fetch) VS. The result is in culled_index_*().
  bool BuildCulledIndexList(const Shader& vertex_shader);

  // Step 2b validation (gpu_cull_replay_validate, read-only): recover the affine
  // position matrix M (clip = M*[input_xyz,1]) from ShaderInterpreter samples of
  // the current draw and report how well it reproduces the interpreter. Confirms
  // the fast matrix-recovery replay before it displaces the interpreter.
  enum class AffineValidateStatus : uint32_t {
    kSkipped = 0,      // not a position-affine-MVP / list-strip / DMA draw
    kUnsupported,      // not exactly one register leaf input, or solve singular
    kAffine,           // M reproduces the interpreter within tolerance
    kNonAffine,        // large residual - clip is NOT affine in the leaf input
  };
  AffineValidateStatus ValidateAffinePositionReplay(const Shader& vertex_shader);
  float affine_validate_max_error() const { return affine_validate_max_error_; }

  // Step 2b-ii fast replay: the recovered affine position map + the k_32_32_32_FLOAT
  // position-attribute decode parameters, so a vertex's clip position is
  // clip_k = sum_a m[k][a] * {decode(index).x, .y, .z, 1}[a] (4 dp4s + a 3-float
  // read) instead of the full ShaderInterpreter.
  struct FastAffineReplay {
    double m[4][4] = {};
    xenos::VertexFormat format = xenos::VertexFormat::kUndefined;
    uint32_t base_dwords = 0;    // fetch constant base address, in dwords
    uint32_t end_dwords = 0;     // base + fetch constant size (bounds, in dwords)
    uint32_t stride_dwords = 0;  // per-vertex stride, in dwords
    int32_t offset_dwords = 0;   // attribute offset within the vertex, in dwords
    xenos::Endian endian = xenos::Endian::kNone;
  };
  // Sets up the fast replay for the current draw: identifies the position slice's
  // single k_32_32_32_FLOAT vfetch leaf input and recovers M from interpreter
  // basis samples. Returns false (caller uses the interpreter path) if the input
  // isn't a single such vfetch or M can't be recovered.
  bool SetupFastAffineReplay(const Shader& vertex_shader, FastAffineReplay& out);

  // Diagnostics for the last BuildCulledIndexList call: whether the fast affine
  // replay engaged, and the leaf position-attribute vfetch format SetupFastAffine-
  // Replay found (kUndefined if no single register leaf/vfetch). Lets the command
  // processor histogram why draws fall back to the slow interpreter.
  bool last_used_fast_replay() const { return last_build_used_fast_; }
  xenos::VertexFormat last_leaf_format() const { return setup_leaf_format_; }
  // Why SetupFastAffineReplay bailed (pinpoints the fast-path fallback cause).
  enum class FastSetupFail : uint32_t {
    kOk = 0,
    kNoLeaf,         // position slice reads no register input (const-only)
    kMultiLeaf,      // >1 register leaf input
    kNoVfetchMatch,  // single leaf, but no vertex_bindings vfetch writes it
    kBadFormat,      // vfetch found, but its format isn't fast-decodable
    kRecoveryFail,   // M couldn't be recovered / failed the residual self-check
    kCount,
  };
  FastSetupFail last_setup_fail() const { return fast_setup_fail_; }
  const uint8_t* culled_index_data() const {
    return cull_emit_index_bytes_.data();
  }
  size_t culled_index_byte_size() const { return cull_emit_index_bytes_.size(); }
  uint32_t culled_index_count() const { return cull_emit_index_count_; }
  uint32_t culled_index_stride() const { return cull_emit_index_stride_; }
  uint32_t culled_dropped_triangles() const {
    return cull_emit_dropped_triangles_;
  }
  // Why BuildCulledIndexList returned false (diagnostic: names the systematic bail
  // so the cull's reach can be widened). kBuilt = it produced a culled list.
  enum class CullBail : uint32_t {
    kBuilt = 0,
    kNoIndices,
    kNotTriList,
    kNotDMA,
    kTessellation,
    kNotInterpretable,  // !CanInterpretShader (whole-shader texture fetch)
    kVtxXyFmt,          // pre-divided positions
    kClipDisable,
    kRestart,           // primitive restart (multi_prim_ib_ena)
    kNoIndexPtr,
    kZeroDropped,       // ran the cull but dropped no triangles
    kCount,
  };
  CullBail culled_bail_reason() const { return cull_bail_reason_; }

 private:
  class PositionYExportSink : public ShaderInterpreter::ExportSink {
   public:
    void Export(ucode::ExportRegister export_register, const float* value,
                uint32_t value_mask) override;

    void Reset() {
      position_y_.reset();
      position_w_.reset();
      point_size_.reset();
      vertex_kill_.reset();
    }

    const std::optional<float>& position_y() const { return position_y_; }
    const std::optional<float>& position_w() const { return position_w_; }
    const std::optional<float>& point_size() const { return point_size_; }
    const std::optional<uint32_t>& vertex_kill() const { return vertex_kill_; }

   private:
    std::optional<float> position_y_;
    std::optional<float> position_w_;
    std::optional<float> point_size_;
    std::optional<uint32_t> vertex_kill_;
  };

  // Full clip-space position sink for the cullable-triangle counter (captures
  // x, y, z, w from the position export, plus the kill flag).
  class PositionExportSink : public ShaderInterpreter::ExportSink {
   public:
    void Export(ucode::ExportRegister export_register, const float* value,
                uint32_t value_mask) override;

    void Reset() {
      position_x_.reset();
      position_y_.reset();
      position_z_.reset();
      position_w_.reset();
      vertex_kill_.reset();
    }

    const std::optional<float>& position_x() const { return position_x_; }
    const std::optional<float>& position_y() const { return position_y_; }
    const std::optional<float>& position_z() const { return position_z_; }
    const std::optional<float>& position_w() const { return position_w_; }
    const std::optional<uint32_t>& vertex_kill() const { return vertex_kill_; }

   private:
    std::optional<float> position_x_;
    std::optional<float> position_y_;
    std::optional<float> position_z_;
    std::optional<float> position_w_;
    std::optional<uint32_t> vertex_kill_;
  };

  // Reused scratch for the cullable-triangle counter (avoids per-draw alloc).
  // One entry per index slot in the draw; invalid entries (killed / reset-index
  // / no position exported) break triangle formation in C3.
  struct CullVertex {
    bool valid;
    bool is_reset;  // primitive-restart marker slot (breaks the strip here)
    float x;
    float y;
    float z;
    float w;
  };
  std::vector<CullVertex> cull_vertices_scratch_;
  // Whether the last counted draw used pre-divided (vtx_xy_fmt) positions - read
  // by C3 to decide whether to apply the perspective divide.
  bool cull_vtx_xy_fmt_ = false;
  // Step 2a output: the raw guest index bytes of the kept triangles (triangle
  // LIST), filled by BuildCulledIndexList and read by the command processor.
  std::vector<uint8_t> cull_emit_index_bytes_;
  uint32_t cull_emit_index_count_ = 0;
  uint32_t cull_emit_index_stride_ = 0;
  uint32_t cull_emit_dropped_triangles_ = 0;
  CullBail cull_bail_reason_ = CullBail::kBuilt;
  float affine_validate_max_error_ = 0.0f;
  bool last_build_used_fast_ = false;
  xenos::VertexFormat setup_leaf_format_ = xenos::VertexFormat::kUndefined;
  FastSetupFail fast_setup_fail_ = FastSetupFail::kOk;

  const RegisterFile& register_file_;
  const Memory& memory_;
  TraceWriter* trace_writer_;

  ShaderInterpreter shader_interpreter_;
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_DRAW_EXTENT_ESTIMATOR_H_
