#version 460
#extension GL_GOOGLE_include_directive : require
// Vulkan port of adaptive_quad.hs.hlsl: the edge factors come from the guest
// "index" buffer, one per control point, and the patch index is the primitive
// index.
#include "tessellation_vk.glsli"

layout(vertices = 1) out;

layout(location = 0) in float xe_in_edge_factor[];
layout(location = 0) out float xe_out_index[];

void main() {
  xe_out_index[gl_InvocationID] = XeGuestIndexToFloat(uint(gl_PrimitiveID));
  // Vulkan, like Direct3D 12, goes along the perimeter: outer level [0] is the
  // U0 edge, [1] V0, [2] U1, [3] V1. The Xbox 360 factors go along the
  // perimeter too, starting one edge later with the swizzle that the guest
  // shaders get in r0.yz.
  float edge_0 = xe_in_edge_factor[3];
  float edge_1 = xe_in_edge_factor[0];
  float edge_2 = xe_in_edge_factor[1];
  float edge_3 = xe_in_edge_factor[2];
  gl_TessLevelOuter[0] = edge_0;
  gl_TessLevelOuter[1] = edge_1;
  gl_TessLevelOuter[2] = edge_2;
  gl_TessLevelOuter[3] = edge_3;
  // On the Xbox 360, the inside factor is the minimum of the factors of the
  // edges along the axis: [0] is along U, [1] along V.
  gl_TessLevelInner[0] = min(edge_1, edge_3);
  gl_TessLevelInner[1] = min(edge_0, edge_2);
}
