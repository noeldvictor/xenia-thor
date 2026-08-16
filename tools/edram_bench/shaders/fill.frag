#version 450
// Fill shader with a RUNTIME ALU cost knob.
//
// The old version had a fixed 8-iteration loop and a comment saying the
// variable under test was the ATTACHMENT, not shader throughput. That was
// right for the sizing question and it is wrong for the one we have now:
// BD's two dominant passes are 65% of the GPU frame and NOBODY KNOWS whether
// they are ALU-bound or bandwidth-bound. Both scale linearly with pixel
// count, so the resolution curve cannot separate them.
//
// So the loop count is now a push constant. Hold pixels fixed and sweep it:
//   time scales with alu_iters  -> ALU-bound   -> GMEM cannot help, ever
//   time flat in alu_iters      -> bandwidth-bound -> GMEM is the right tool
//
// The loop must NOT be foldable, or ir3 will hoist it away and the sweep
// reads flat for the wrong reason. pc.t comes from the host per draw and the
// trip count is dynamic, so the compiler cannot unroll it to a constant.
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;
layout(push_constant) uniform PC {
  float t;
  uint alu_iters;
} pc;
void main() {
  vec3 c = vec3(vUV, pc.t);
  for (uint i = 0u; i < pc.alu_iters; ++i) {
    c = fract(c * 1.3 + 0.17);
  }
  // Alpha varies per fragment so an enabled blend cannot be optimized into a
  // plain write: the destination must actually be read back.
  oColor = vec4(c, 0.5 + 0.25 * c.x);
}
