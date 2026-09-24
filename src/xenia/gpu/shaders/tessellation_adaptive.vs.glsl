#version 460
#extension GL_GOOGLE_include_directive : require
// Vulkan port of tessellation_adaptive.vs.hlsl. The Xbox 360's GPU accepts the
// float32 tessellation factors for edges through a special kind of an index
// buffer. There appear to be no special values for culled patches on the Xbox
// 360 (4D5307E6 loses distant patches if only factors above 0 are drawn).
#include "tessellation_vk.glsli"

layout(location = 0) out float xe_out_edge_factor;

void main() {
  xe_out_edge_factor = clamp(
      uintBitsToFloat(
          XeEndianSwap32(uint(gl_VertexIndex), xe_vertex_index_endian)) +
          1.0,
      xe_tessellation_factor_range.x, xe_tessellation_factor_range.y);
}
