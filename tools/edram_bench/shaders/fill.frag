#version 450
// A few ALU ops so the fragment cost is non-zero but does not dominate - the
// variable under test is the ATTACHMENT, not shader throughput.
layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;
layout(push_constant) uniform PC { float t; } pc;
void main() {
  vec3 c = vec3(vUV, pc.t);
  for (int i = 0; i < 8; ++i) {
    c = fract(c * 1.3 + 0.17);
  }
  oColor = vec4(c, 1.0);
}
