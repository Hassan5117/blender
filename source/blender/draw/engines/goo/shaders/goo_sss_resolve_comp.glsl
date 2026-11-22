/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_effects_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_sss_resolve)

#include "draw_view_lib.glsl"
#include "eevee_utility_tx.bsl.hh"
#include "gpu_shader_math_constants_lib.glsl"
#include "gpu_shader_utildefines_lib.glsl"

void main()
{
  int2 texel = int2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(texel, extent))) {
    return;
  }

  float4 layer0 = texelFetch(sss_data_tx, int3(texel, 0), 0);
  if (layer0.a < 0.5f) {
    return;
  }

  float center_d = texelFetch(depth_tx, texel, 0).r;
  if (center_d >= 1.0f) {
    return;
  }

  float4 layer1 = texelFetch(sss_data_tx, int3(texel, 1), 0);
  float4 layer2 = texelFetch(sss_data_tx, int3(texel, 2), 0);
  float3 center_irrad = layer0.rgb;
  float3 center_albedo = layer1.rgb;
  float center_obj_id = layer1.a;
  float max_albedo = max(1e-6f, max(center_albedo.r, max(center_albedo.g, center_albedo.b)));
  float3 burley_A = center_albedo / max_albedo;
  float3 burley_s = 1.9f - burley_A + 3.5f * ((burley_A - 0.8f) * (burley_A - 0.8f));
  /* Principled BSDF & SSS nodes scale sss_radius by 4*PI for Random Walk; match EEVEE burley_setup. */
  float3 burley_l = (0.25f * 0.31830988618379067f) * max(layer2.rgb, float3(0.0f));
  float3 burley_d = max(burley_l / max(burley_s, float3(0.05f)), float3(1e-4f));
  float max_d = max(burley_d.r, max(burley_d.g, burley_d.b));

  float2 inv_ext = 1.0f / float2(extent);
  float2 center_uv = (float2(texel) + 0.5f) * inv_ext;
  float3 center_vP = drw_point_screen_to_view(float3(center_uv, center_d));

  float z_scale = drw_view_is_perspective() ? max(abs(center_vP.z), 1e-3f) : 1.0f;
  float proj_scale = abs(drw_view().winmat[0][0]) * 0.5f * float(extent.x);
  float px_per_meter = max(proj_scale / z_scale, 1e-4f);
  float r_max_px = clamp(6.0f * max_d * px_per_meter, 0.0f, 64.0f);

  if (r_max_px < 1.0f) {
    float4 cur = imageLoadFast(color_img, texel);
    imageStoreFast(color_img, texel, float4(cur.rgb + center_irrad * center_albedo, cur.a));
    return;
  }

  [[resource_table]] const UtilityTexture util_tx = resource_table_get(UtilityTexture);
  float2 noise = util_tx.fetch(float2(texel), float(UTIL_BLUE_NOISE_LAYER)).xy;
  float jitter_fac = saturate(goo_probe_buf.sss_params.y);
  float angle_offset = (noise.x * 6.2831853f * jitter_fac) + float(sample_index) * 2.39996323f;

  int sample_count = clamp(int(goo_probe_buf.sss_params.x + 0.5f) * 2 + 1, 5, 33);

  float r_min_px = 0.75f;
  float world_per_px = 1.0f / px_per_meter;
  float r_min_world = r_min_px * world_per_px;

  /* Exact analytic Christensen-Burley CDF over center pixel core [0, r_min_world]. */
  float3 w_core = max(
      1.0f - 0.25f * exp(-r_min_world / burley_d) - 0.75f * exp(-r_min_world / (3.0f * burley_d)),
      float3(0.0f));
  float3 accum_irrad = center_irrad * w_core;
  float3 weight_sum = w_core;

  float r_ratio = max(r_max_px / r_min_px, 1.001f);
  float log_r_ratio = log(r_ratio);
  float golden_angle = 2.39996323f;
  for (int i = 0; i < 33; i++) {
    if (i >= sample_count) {
      break;
    }
    float u = (float(i) + 0.25f + 0.5f * mix(0.5f, noise.y, jitter_fac)) / float(sample_count);
    /* Log-uniform radial spacing gives equal relative accuracy across disparate RGB channels. */
    float r_px = r_min_px * pow(r_ratio, u);
    float theta = float(i) * golden_angle + angle_offset;
    int2 tap_texel = texel + int2(round(float2(cos(theta), sin(theta)) * r_px));
    if (any(lessThan(tap_texel, int2(0))) || any(greaterThanEqual(tap_texel, extent))) {
      continue;
    }

    float4 tap0 = texelFetch(sss_data_tx, int3(tap_texel, 0), 0);
    if (tap0.a < 0.5f) {
      continue;
    }
    float4 tap1 = texelFetch(sss_data_tx, int3(tap_texel, 1), 0);
    if (abs(tap1.a - center_obj_id) > 0.5f) {
      continue;
    }

    float tap_d = texelFetch(depth_tx, tap_texel, 0).r;
    if (tap_d >= 1.0f) {
      continue;
    }
    float2 tap_uv = (float2(tap_texel) + 0.5f) * inv_ext;
    float3 tap_vP = drw_point_screen_to_view(float3(tap_uv, tap_d));
    float world_dist = length(tap_vP - center_vP);

    float dr_world = (r_px * world_per_px) * log_r_ratio / float(sample_count);
    float3 pdf_1d = (exp(-world_dist / burley_d) + exp(-world_dist / (3.0f * burley_d))) /
                    (4.0f * burley_d);
    float3 sample_w = pdf_1d * dr_world;

    accum_irrad += tap0.rgb * sample_w;
    weight_sum += sample_w;
  }

  float3 scattered = (accum_irrad / max(weight_sum, float3(1e-6f))) * center_albedo;
  float4 cur = imageLoadFast(color_img, texel);
  imageStoreFast(color_img, texel, float4(cur.rgb + scattered, cur.a));
}
