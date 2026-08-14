#version 450
// Uber-shader register-pressure probe.
//
// Models what the guest shader translator actually emits: a pixel shader whose
// behaviour is gated by a word of SYSTEM FLAGS. Our translator carries 61 such
// kSysFlag_ bits and tests them at RUNTIME from a uniform, so ir3 cannot fold
// them - every gated block stays live and its temporaries stay allocated.
//
// Compiled twice from this one source:
//   (default)     flags come from a push constant  -> runtime branches
//   -DSPEC_FLAGS  flags come from a specialization constant -> the compiler
//                 folds every test and dead-strips the untaken blocks
//
// The Adreno guide claims the second form cuts GPR count and so raises wave
// occupancy. This measures whether that is true on Turnip, before anyone
// rewrites the translator around it.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

layout(push_constant) uniform PC {
  float t;
  uint flags;
} pc;

#ifdef SPEC_FLAGS
layout(constant_id = 0) const uint kFlags = 0u;
#define FLAGS kFlags
#else
#define FLAGS pc.flags
#endif

// Each gated block keeps its own live value, so an unfoldable branch forces the
// allocator to hold all of them at once - which is the register pressure the
// uber-shader pattern is accused of creating.
#define GATE(i, expr)                 \
  if ((FLAGS & (1u << i)) != 0u) {    \
    acc##i = expr;                    \
  }

void main() {
  vec3 base = vec3(vUV, pc.t);
  vec3 acc0 = base, acc1 = base, acc2 = base, acc3 = base;
  vec3 acc4 = base, acc5 = base, acc6 = base, acc7 = base;
  vec3 acc8 = base, acc9 = base, acc10 = base, acc11 = base;
  vec3 acc12 = base, acc13 = base, acc14 = base, acc15 = base;

  GATE(0, fract(acc0 * 1.31 + 0.17))
  GATE(1, sqrt(abs(acc1)) + 0.11)
  GATE(2, acc2 * acc2 - 0.23)
  GATE(3, sin(acc3 * 3.1) * 0.5 + 0.5)
  GATE(4, fract(acc4 * 2.17 + 0.29))
  GATE(5, exp2(acc5 * 0.5) * 0.25)
  GATE(6, acc6 * 0.75 + base * 0.25)
  GATE(7, normalize(acc7 + 0.001))
  GATE(8, fract(acc8 * 1.77 + 0.37))
  GATE(9, cos(acc9 * 2.3) * 0.5 + 0.5)
  GATE(10, acc10 * base + 0.13)
  GATE(11, inversesqrt(abs(acc11) + 0.01))
  GATE(12, fract(acc12 * 1.19 + 0.41))
  GATE(13, log2(abs(acc13) + 1.0))
  GATE(14, acc14 - base * 0.5)
  GATE(15, clamp(acc15 * 1.5, 0.0, 1.0))

  vec3 c = acc0 + acc1 + acc2 + acc3 + acc4 + acc5 + acc6 + acc7 + acc8 +
           acc9 + acc10 + acc11 + acc12 + acc13 + acc14 + acc15;
  oColor = vec4(c * (1.0 / 16.0), 1.0);
}
