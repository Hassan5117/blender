/* SPDX-FileCopyrightText: 2017-2023 Blender Authors
 * SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_post_infos.hh"

#if defined(STEP_ACCUM)
FRAGMENT_SHADER_CREATE_INFO(goo_taa_accum)
#elif defined(STEP_BLEND)
FRAGMENT_SHADER_CREATE_INFO(goo_taa_blend)
#elif defined(STEP_REPROJECT)
FRAGMENT_SHADER_CREATE_INFO(goo_taa_reproject)
#include "draw_view_lib.glsl"
#endif

float4 goo_safe_color(float4 c)
{
  float r = (isnan(c.x) || isinf(c.x)) ? 0.0f : max(c.x, 0.0f);
  float g = (isnan(c.y) || isinf(c.y)) ? 0.0f : max(c.y, 0.0f);
  float b = (isnan(c.z) || isinf(c.z)) ? 0.0f : max(c.z, 0.0f);
  float a = (isnan(c.w) || isinf(c.w)) ? 0.0f : clamp(c.w, 0.0f, 1.0f);
  return float4(r, g, b, a);
}

#if defined(STEP_REPROJECT)
float3 goo_clip_to_aabb(float3 color, float3 minimum, float3 maximum)
{
  float3 center = 0.5f * (maximum + minimum);
  float3 extents = 0.5f * (maximum - minimum);
  float3 dist = color - center;
  float3 ts = abs(extents) / max(abs(dist), float3(0.0001f));
  float t = clamp(min(ts.x, min(ts.y, ts.z)), 0.0f, 1.0f);
  return center + dist * t;
}
#endif

void main()
{
#if defined(STEP_ACCUM)
  int2 max_texel = max(textureSize(sample_tx, 0) - int2(1), int2(0));
  int2 texel = clamp(int2(gl_FragCoord.xy), int2(0), max_texel);
  out_color = goo_safe_color(texelFetch(sample_tx, texel, 0)) * sample_weight;

#elif defined(STEP_BLEND)
  int2 max_texel = max(textureSize(sample_tx, 0) - int2(1), int2(0));
  int2 texel = clamp(int2(gl_FragCoord.xy), int2(0), max_texel);
  float4 cur = goo_safe_color(texelFetch(sample_tx, texel, 0));
  float4 hist = goo_safe_color(texelFetch(history_tx, texel, 0));
  out_color = goo_safe_color(mix(hist, cur, blend_alpha));

#elif defined(STEP_REPROJECT)
  int2 tex_size = max(textureSize(sample_tx, 0), int2(1));
  int2 max_texel = tex_size - int2(1);
  int2 texel = clamp(int2(gl_FragCoord.xy), int2(0), max_texel);
  float2 uv = (float2(texel) + 0.5f) / float2(tex_size);

  float depth = texelFetch(depth_tx, texel, 0).r;
  float3 world_pos = drw_point_screen_to_world(float3(uv, depth));
  float4 prev_clip = prev_persmat * float4(world_pos, 1.0f);
  float2 uv_history = (prev_clip.w > 1e-6f) ? ((prev_clip.xy / prev_clip.w) * 0.5f + 0.5f) :
                                              float2(-1.0f);

  float4 c02 = goo_safe_color(texelFetch(sample_tx, clamp(texel + int2(-1, 1), int2(0), max_texel), 0));
  float4 c12 = goo_safe_color(texelFetch(sample_tx, clamp(texel + int2(0, 1), int2(0), max_texel), 0));
  float4 c22 = goo_safe_color(texelFetch(sample_tx, clamp(texel + int2(1, 1), int2(0), max_texel), 0));
  float4 c01 = goo_safe_color(texelFetch(sample_tx, clamp(texel + int2(-1, 0), int2(0), max_texel), 0));
  float4 c11 = goo_safe_color(texelFetch(sample_tx, texel, 0));
  float4 c21 = goo_safe_color(texelFetch(sample_tx, clamp(texel + int2(1, 0), int2(0), max_texel), 0));
  float4 c00 = goo_safe_color(texelFetch(sample_tx, clamp(texel + int2(-1, -1), int2(0), max_texel), 0));
  float4 c10 = goo_safe_color(texelFetch(sample_tx, clamp(texel + int2(0, -1), int2(0), max_texel), 0));
  float4 c20 = goo_safe_color(texelFetch(sample_tx, clamp(texel + int2(1, -1), int2(0), max_texel), 0));

  float4 min_col = min(min(min(c02, c12), min(c22, c01)),
                       min(min(c11, c21), min(c00, min(c10, c20))));
  float4 max_col = max(max(max(c02, c12), max(c22, c01)),
                       max(max(c11, c21), max(c00, max(c10, c20))));

  float4 min_center = min(min(c12, c01), min(c11, min(c21, c10)));
  float4 max_center = max(max(c12, c01), max(c11, max(c21, c10)));
  min_col = (min_col + min_center) * 0.5f;
  max_col = (max_col + max_center) * 0.5f;

  float4 color_history = goo_safe_color(textureLod(history_tx, uv_history, 0.0f));
  color_history.rgb = goo_clip_to_aabb(color_history.rgb, min_col.rgb, max_col.rgb);

  float lum0 = dot(c11.rgb, float3(0.3333333f));
  float lum1 = dot(color_history.rgb, float3(0.3333333f));
  float diff = abs(lum0 - lum1) / max(lum0, max(lum1, 0.2f));
  float weight = clamp(1.0f - diff, 0.0f, 1.0f);
  float alpha = mix(0.04f, 0.12f, weight * weight);

  float4 blended = mix(color_history, c11, alpha);
  bool out_of_view = any(greaterThanEqual(abs(uv_history - 0.5f), float2(0.5f)));
  out_color = goo_safe_color(out_of_view ? c11 : blended);
  out_color.a = c11.a;
#endif
}
