#version 460
#extension GL_GOOGLE_include_directive : require
// Vulkan port of adaptive_triangle.hs.hlsl: the edge factors come from the
// guest "index" buffer, one per control point, and the patch index is the
// primitive index.
#include "tessellation_vk.glsli"

layout(vertices = 1) out;

layout(location = 0) in float xe_in_edge_factor[];
layout(location = 0) out float xe_out_index[];

void main() {
  xe_out_index[gl_InvocationID] = XeGuestIndexToFloat(uint(gl_PrimitiveID));
  // On the Xbox 360, factor [0] is for the v0->v1 edge, [1] for v1->v2, [2]
  // for v2->v0 (v0 is the U1V0W0 vertex). In Vulkan, like in Direct3D 12, outer
  // level [0] is for the U0 edge (v1->v2), [1] for the V0 edge (v2->v0), [2]
  // for the W0 edge (v0->v1). The translated guest shader gets the
  // tessellation coordinates as ZYX in r0 with the identity swizzle in r1.y,
  // and with this order 4D5307E6 water has no cracks (Direct3D 12 findings).
  float edge_0 = xe_in_edge_factor[1];
  float edge_1 = xe_in_edge_factor[2];
  float edge_2 = xe_in_edge_factor[0];
  gl_TessLevelOuter[0] = edge_0;
  gl_TessLevelOuter[1] = edge_1;
  gl_TessLevelOuter[2] = edge_2;
  gl_TessLevelInner[0] = min(min(edge_0, edge_1), edge_2);
}
