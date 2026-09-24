#version 460
#extension GL_GOOGLE_include_directive : require
// Vulkan port of tessellation_indexed.vs.hlsl: a control point (or a patch)
// index from the guest index buffer, swapped and offset like the guest vertex
// shader would do it, for the tessellation control shader.
#include "tessellation_vk.glsli"

layout(location = 0) out float xe_out_index;

void main() {
  xe_out_index = XeGuestIndexToFloat(
      XeEndianSwap32(uint(gl_VertexIndex), xe_vertex_index_endian));
}
