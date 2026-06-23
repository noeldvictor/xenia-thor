// Frame generation - motion-compensated warp (Vulkan GLSL, presenter-only).
// Synthesizes the in-between frame by FORWARD-extrapolating the newest history
// frame (N-1) by half the estimated global camera motion, instead of the 50%
// cross-fade (which ghosts). Reads the 1x1 motion sums from the estimate pass,
// solves the separable Lucas-Kanade translation (dx=-Sxt/Sxx, dy=-Syt/Syy =
// the N-2->N-1 image flow), and samples N-1 at c - 0.5*(dx,dy) (the feature now
// at output c came from there in N-1). Global translation = robust, no per-pixel
// aperture ghosting; camera/background motion is captured, fast local motion is
// not (accepted first pass, same class as the cross-fade limitation).
#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0) uniform texture2D src0;    // N-1 (newest history)
layout(set = 0, binding = 1) uniform texture2D motion;  // 1x1 (Sxx,Syy,Sxt,Syt)

layout(location = 0) out vec4 o_color;

void main() {
  vec4 s = texelFetch(motion, ivec2(0, 0), 0);
  // Guard against a degenerate (near-zero-gradient) estimate -> no warp.
  float dx = (s.x > 1e-3) ? clamp(-s.z / s.x, -48.0, 48.0) : 0.0;
  float dy = (s.y > 1e-3) ? clamp(-s.w / s.y, -48.0, 48.0) : 0.0;
  ivec2 ext = textureSize(src0, 0);
  ivec2 c = ivec2(gl_FragCoord.xy);
  ivec2 sc = clamp(c - ivec2(round(vec2(dx, dy) * 0.5)), ivec2(0, 0),
                   ext - ivec2(1, 1));
  o_color = vec4(texelFetch(src0, sc, 0).rgb, 1.0);
}
