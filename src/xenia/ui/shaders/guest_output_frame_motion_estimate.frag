// Frame generation - global motion estimate (Vulkan GLSL, presenter-only; not
// xesl because it is Vulkan-exclusive frame-gen). Renders to a 1x1 RGBA32F
// target: a single fragment loops a GRIDxGRID sample lattice over the two latest
// history frames and accumulates the separable Lucas-Kanade normal-equation sums
// (Sxx=ZIx^2, Syy=ZIy^2, Sxt=ZIx*It, Syt=ZIy*It) of the global 2D translation
// between N-2 (src1) and N-1 (src0). The warp FS divides these for (dx,dy).
// Compile: glslc --target-env=vulkan1.1 -O -mfmt=c this -o the bytecode header.
#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D src0;  // N-1 (newest history)
layout(set = 0, binding = 1) uniform texture2D src1;  // N-2 (older history)

layout(location = 0) out vec4 o_sums;  // (Sxx, Syy, Sxt, Syt)

const int GRID = 32;

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main() {
  ivec2 ext = textureSize(src0, 0);
  float Sxx = 0.0;
  float Syy = 0.0;
  float Sxt = 0.0;
  float Syt = 0.0;
  for (int gy = 1; gy <= GRID; ++gy) {
    for (int gx = 1; gx <= GRID; ++gx) {
      ivec2 c = ivec2(gx * ext.x / (GRID + 1), gy * ext.y / (GRID + 1));
      c = clamp(c, ivec2(1, 1), ext - ivec2(2, 2));
      float l = luma(texelFetch(src0, c, 0).rgb);
      float lx1 = luma(texelFetch(src0, c + ivec2(1, 0), 0).rgb);
      float ly1 = luma(texelFetch(src0, c + ivec2(0, 1), 0).rgb);
      float l2 = luma(texelFetch(src1, c, 0).rgb);
      float Ix = lx1 - l;       // spatial gradient x (forward diff of N-1)
      float Iy = ly1 - l;       // spatial gradient y
      float It = l - l2;        // temporal diff N-1 - N-2
      Sxx += Ix * Ix;
      Syy += Iy * Iy;
      Sxt += Ix * It;
      Syt += Iy * It;
    }
  }
  o_sums = vec4(Sxx, Syy, Sxt, Syt);
}
