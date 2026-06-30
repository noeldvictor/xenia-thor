#version 460
#extension GL_EXT_samplerless_texture_functions : require

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// BD-30 anti-aliasing lever (gpu_present_fxaa): a present-time FXAA pass that
// restores MSAA-like edge smoothing after gpu_force_max_msaa_samples drops the
// guest MSAA 2x->1x (which halves per-sample foliage ROP for the fps win but
// aliases bright thin geometry into over-bright glints). FXAA re-averages those
// aliased edges, directly taming the over-bright at a fixed ~0.5ms/720p cost
// (vs MSAA's scene-scaling per-sample ROP). Compact Lottes FXAA with a flat-
// region early-out so texture detail / HUD text are preserved.
//
// Bindings + push constants MATCH guest_output_bilinear so this effect reuses
// the bilinear pipeline layout (source texture set0/b0, sampler set0/b1,
// push-const output_offset @16 / output_size_inv @24, shared rect vertex shader).
//
// NOTE: hand-compiled with the NDK glslc (the desktop Vulkan SDK / xb
// buildshaders toolchain is not provisioned in this build env) into
// bytecode/vulkan_spirv/guest_output_fxaa_ps.h. Re-run scratch/thor-debug/
// build_fxaa_spv.sh after editing.

layout(push_constant) uniform XePushConstants {
  layout(offset = 16) ivec2 xe_fxaa_output_offset;
  layout(offset = 24) vec2 xe_fxaa_output_size_inv;
};

layout(set = 0, binding = 0) uniform texture2D xe_fxaa_source;
layout(set = 0, binding = 1) uniform sampler xe_fxaa_sampler;

layout(location = 0) out vec4 xe_fxaa_color;

// Engage only above this absolute luma contrast (skip near-flat noise).
const float kEdgeThresholdMin = 0.0312;
// ...and above this fraction of the local max luma (adaptive; preserves texture).
const float kEdgeThreshold = 0.125;
const float kSpanMax = 8.0;
const float kReduceMul = 1.0 / 8.0;
const float kReduceMin = 1.0 / 128.0;

// Highlight compression: the MSAA 2x->1x clamp makes bright thin geometry alias
// to full brightness, which BD's bloom then amplifies into over-bright glints /
// blow-out. Present-time FXAA runs AFTER bloom, so a soft luma roll-off above a
// knee here is the right place to pull those blown-out highlights back toward
// range (edge-AA alone can't - the bloomed glow is a soft region, not an edge).
// Normal brightness (< knee) passes through unchanged.
const float kHighlightKnee = 0.70;
const float kHighlightStrength = 2.5;

float XeLuma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// GLSL forbids storing a combined sampler2D in a local, so build it inline.
#define XE_SRC sampler2D(xe_fxaa_source, xe_fxaa_sampler)

void main() {
  vec2 uv = (vec2(ivec2(gl_FragCoord.xy) - xe_fxaa_output_offset) + 0.5) *
            xe_fxaa_output_size_inv;
  vec2 texel = xe_fxaa_output_size_inv;

  vec3 rgbM = texture(XE_SRC, uv).rgb;
  vec3 rgbNW = textureLodOffset(XE_SRC, uv, 0.0, ivec2(-1, -1)).rgb;
  vec3 rgbNE = textureLodOffset(XE_SRC, uv, 0.0, ivec2(1, -1)).rgb;
  vec3 rgbSW = textureLodOffset(XE_SRC, uv, 0.0, ivec2(-1, 1)).rgb;
  vec3 rgbSE = textureLodOffset(XE_SRC, uv, 0.0, ivec2(1, 1)).rgb;

  float lumaM = XeLuma(rgbM);
  float lumaNW = XeLuma(rgbNW);
  float lumaNE = XeLuma(rgbNE);
  float lumaSW = XeLuma(rgbSW);
  float lumaSE = XeLuma(rgbSE);

  float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
  float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
  float lumaRange = lumaMax - lumaMin;

  vec3 result;
  if (lumaRange < max(kEdgeThresholdMin, lumaMax * kEdgeThreshold)) {
    // Flat region -> no edge AA (keeps textures/HUD crisp). Still highlight-
    // compressed below, since the bloomed over-bright glow is a flat region.
    result = rgbM;
  } else {
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce =
        max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * kReduceMul, kReduceMin);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-kSpanMax), vec2(kSpanMax)) * texel;

    vec3 rgbA = 0.5 * (texture(XE_SRC, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
                       texture(XE_SRC, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(XE_SRC, uv + dir * -0.5).rgb +
                                     texture(XE_SRC, uv + dir * 0.5).rgb);

    float lumaB = XeLuma(rgbB);
    // If the second tap overshoots the local luma range, fall back to the safe
    // (less-blurred) average.
    result = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
  }

  // Soft-compress blown-out highlights back toward range (the bloomed over-
  // bright the MSAA 2x->1x clamp introduced). Luma below the knee is untouched.
  float outLuma = XeLuma(result);
  if (outLuma > kHighlightKnee) {
    float over = outLuma - kHighlightKnee;
    float compressed =
        kHighlightKnee + over / (1.0 + over * kHighlightStrength);
    result *= compressed / max(outLuma, 1.0e-4);
  }

  xe_fxaa_color = vec4(result, 1.0);
}
