/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_indirect_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_ssr_resolve)

#include "gpu_shader_math_base_lib.glsl"
#include "gpu_shader_utildefines_lib.glsl"

void main()
{
  int2 texel = int2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(texel, render_extent))) {
    return;
  }

  float4 base_col = texelFetch(scene_color_tx, texel, 0);
  float4 ssr_data = texelFetch(ssr_data_tx, texel, 0);
  float4 ssr_spec = texelFetch(ssr_spec_tx, texel, 0);
  float roughness = ssr_data.w;

  if (roughness < 0.0f || ssr_spec.w <= 1e-4f) {
    imageStoreFast(out_color_img, texel, base_col);
    return;
  }

  float4 hit;
  if (half_res == 0) {
    hit = texelFetch(ssr_hit_tx, clamp(texel, int2(0), trace_extent - int2(1)), 0);
  }
  else {
    float center_d = texelFetch(hiz_depth_tx, texel, 0).r;
    float2 trace_f = (float2(texel) + 0.5f) * 0.5f - 0.5f;
    int2 t0 = int2(floor(trace_f));
    float2 weighted_uv = float2(0.0f);
    float weighted_dist = 0.0f;
    float weighted_conf = 0.0f;
    float sum_w = 0.0f;
    float sum_hit_w = 0.0f;

    for (int dy = 0; dy < 2; dy++) {
      for (int dx = 0; dx < 2; dx++) {
        int2 tc = clamp(t0 + int2(dx, dy), int2(0), trace_extent - int2(1));
        int2 rc = clamp(tc * 2, int2(0), render_extent - int2(1));
        float d_nbr = texelFetch(hiz_depth_tx, rc, 0).r;
        float w = 1.0f / (1e-4f + abs(center_d - d_nbr) * 200.0f);
        float4 h = texelFetch(ssr_hit_tx, tc, 0);
        if (h.w > 0.0f) {
          weighted_uv += h.xy * (w * h.w);
          weighted_dist += h.z * (w * h.w);
          sum_hit_w += w * h.w;
        }
        weighted_conf += h.w * w;
        sum_w += w;
      }
    }

    hit = (sum_hit_w > 1e-5f) ?
              float4(weighted_uv / sum_hit_w, weighted_dist / sum_hit_w, weighted_conf / sum_w) :
              float4(0.0f);
  }

  float conf = saturate(hit.w) * saturate(ssr_spec.w);
  if (conf <= 1e-4f) {
    imageStoreFast(out_color_img, texel, base_col);
    return;
  }

  float4 ssr_fallback = texelFetch(ssr_fallback_tx, texel, 0);
  float lod = clamp(
      sqrt(roughness) * float(max(color_mips - 1, 1)) * 0.85f, 0.0f, float(max(color_mips - 1, 0)));
  float3 ssr_rad = max(textureLod(scene_color_tx, hit.xy, lod).rgb, float3(0.0f));

  float firefly = goo_probe_buf.indirect_params.x;
  if (firefly > 0.0f) {
    float lum = max(ssr_rad.r, max(ssr_rad.g, ssr_rad.b));
    if (lum > firefly) {
      ssr_rad *= (firefly / lum);
    }
  }

  float3 final_rgb = max(base_col.rgb - ssr_fallback.rgb * conf, float3(0.0f)) +
                     ssr_rad * ssr_spec.rgb * conf;
  imageStoreFast(out_color_img, texel, float4(final_rgb, base_col.a));
}
