/* SPDX-FileCopyrightText: 2017-2023 Blender Authors
 * SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Ground-Truth Ambient Occlusion (GTAO) horizon search and hemisphere integration.
 * Ported from Goo Engine 4.4 `ambient_occlusion_lib.glsl` to Blender 5.2 Metal-safe BSL.
 */

#pragma once

#include "draw_view_lib.glsl"
#include "eevee_utility_tx.bsl.hh"
#include "goo_defines.hh"
#include "gpu_shader_math_constants_lib.glsl"
#include "gpu_shader_math_safe_lib.glsl"
#include "gpu_shader_math_vector_reduce_lib.glsl"
#include "gpu_shader_math_vector_safe_lib.glsl"
#include "gpu_shader_utildefines_lib.glsl"

SHADER_LIBRARY_CREATE_INFO(UtilityTexture)
SHADER_LIBRARY_CREATE_INFO(draw_view)

#define GOO_NO_OCCLUSION_DATA float4(M_PI, -M_PI, M_PI, -M_PI)

float2 goo_gtao_noise(float2 frag_coord, float2 sample_rand)
{
  [[resource_table]] const UtilityTexture util_tx = resource_table_get(UtilityTexture);
  float2 noise = util_tx.fetch(frag_coord, float(UTIL_BLUE_NOISE_LAYER)).xy;
  return fract(noise * 6.1803402f + sample_rand);
}

float2 goo_gtao_dir(float jitter)
{
  float angle = jitter * M_PI_2;
  return float2(cos(angle), sin(angle));
}

float4 goo_gtao_pack_horizons(float4 horizons)
{
  return float4(1.0f) - horizons * float4(1.0f, -1.0f, 1.0f, -1.0f) * M_1_PI;
}

float4 goo_gtao_unpack_horizons(float4 packed_horizons)
{
  return (float4(1.0f) - packed_horizons) * float4(1.0f, -1.0f, 1.0f, -1.0f) * M_PI;
}

float goo_gtao_search_slice(float3 vI,
                            float3 vP,
                            float2 uv0,
                            float2 step_dir_uv,
                            float screen_radius_px,
                            float noise_y,
                            sampler2DDepth depth_tx,
                            float inverted,
                            float radius,
                            int sample_count)
{
  float h = (inverted != 0.0f) ? 1.0f : -1.0f;

  if (screen_radius_px < 1.5f) {
    return safe_acos(h);
  }

  float radial_span = max(screen_radius_px - float(sample_count), 0.0f);
  float safe_radius = max(radius, 1e-5f);
  float safe_inv_count = 1.0f / max(float(sample_count), 1.0f);

  for (int iter = 0; iter < 16; iter++) {
    if (iter >= sample_count) {
      break;
    }
    float frac = (float(iter) + noise_y) * safe_inv_count;
    float time_px = 1.0f + float(iter) + frac * frac * radial_span;
    if (time_px > screen_radius_px) {
      break;
    }

    float2 sample_uv = uv0 + step_dir_uv * time_px;
    if (any(lessThan(sample_uv, float2(0.0f))) || any(greaterThan(sample_uv, float2(1.0f)))) {
      break;
    }

    float depth = textureLod(depth_tx, sample_uv, 0.0f).r;
    if (depth >= 1.0f && inverted == 0.0f) {
      continue;
    }

    depth += (inverted != 0.0f) ? -1e-5f : 1e-5f;

    float3 s = drw_point_screen_to_view(float3(sample_uv, depth));
    float3 omega_s = s - vP;
    float len = length(omega_s);
    if (len <= 1e-5f) {
      continue;
    }

    float s_h = dot(vI, omega_s / len);
    float dist_ratio = saturate(abs(len) / safe_radius);
    float dist_fac = dist_ratio * dist_ratio;

    if (inverted != 0.0f) {
      h = mix(min(h, s_h), h, dist_fac);
    }
    else {
      h = mix(max(h, s_h), h, dist_fac);
    }
  }
  return safe_acos(h);
}

float4 goo_gtao_horizon_search(float3 vP,
                               float2 frag_coord,
                               float2 sample_rand,
                               sampler2DDepth depth_tx,
                               float radius,
                               float inverted,
                               int sample_count)
{
  if (radius <= 1e-5f) {
    return (inverted != 0.0f) ? float4(0.0f) : GOO_NO_OCCLUSION_DATA;
  }

  float2 noise = goo_gtao_noise(frag_coord, sample_rand);
  float2 dir = goo_gtao_dir(noise.x);
  float2 uv0 = drw_point_view_to_screen(vP).xy;
  float3 vI = drw_view_incident_vector(vP);
  float2 tex_size = float2(max(textureSize(depth_tx, 0), int2(1)));
  float max_screen_radius = 0.25f * max(tex_size.x, tex_size.y);

  float4 horizons = (inverted != 0.0f) ? float4(0.0f) : GOO_NO_OCCLUSION_DATA;

  for (int i = 0; i < 2; i++) {
    float2 uv1 = drw_point_view_to_screen(vP + float3(dir * radius, 0.0f)).xy;
    float2 delta_px = (uv1 - uv0) * tex_size;
    float px_len = length(delta_px);
    float screen_radius_px = min(px_len, max_screen_radius);
    float2 step_dir_uv = (px_len > 1e-5f) ? ((delta_px / px_len) / tex_size) : float2(0.0f);

    float h_pos = goo_gtao_search_slice(
        vI, vP, uv0, step_dir_uv, screen_radius_px, noise.y, depth_tx, inverted, radius, sample_count);
    float h_neg = goo_gtao_search_slice(vI,
                                        vP,
                                        uv0,
                                        -step_dir_uv,
                                        screen_radius_px,
                                        noise.y,
                                        depth_tx,
                                        inverted,
                                        radius,
                                        sample_count);

    if (i == 0) {
      horizons.x = h_pos;
      horizons.y = -h_neg;
    }
    else {
      horizons.z = h_pos;
      horizons.w = -h_neg;
    }

    dir = float2(-dir.y, dir.x);
  }

  return horizons;
}

float2 goo_gtao_clamp_horizons(float2 horizons, float angle_N, float inverted)
{
  float max_angle = M_PI_2 - 0.05f;
  if (inverted != 0.0f) {
    horizons.x = max(horizons.x, angle_N + max_angle);
    horizons.y = min(horizons.y, angle_N - max_angle);
  }
  else {
    horizons.x = min(horizons.x, angle_N + max_angle);
    horizons.y = max(horizons.y, angle_N - max_angle);
  }
  return horizons;
}

void goo_gtao_integrate(float4 horizons,
                        float2 frag_coord,
                        float2 sample_rand,
                        float3 V,
                        float3 N,
                        float3 Ng,
                        float inverted,
                        bool use_bent_normals,
                        float &visibility,
                        float3 &bent_normal)
{
  bool early_out = (inverted != 0.0f) ? (reduce_max(abs(horizons)) <= 1e-3f) :
                                        (reduce_min(abs(horizons)) >= (M_PI - 1e-3f));
  if (early_out) {
    visibility = saturate(dot(N, Ng) * 0.5f + 0.5f);
    bent_normal = use_bent_normals ? safe_normalize(N + Ng) : N;
    return;
  }

  float2 noise = goo_gtao_noise(frag_coord, sample_rand);
  float2 dir = goo_gtao_dir(noise.x);

  float vis_accum = 0.0f;
  float3 bent_accum = N * 0.001f;

  for (int i = 0; i < 2; i++) {
    float3 T = drw_normal_view_to_world(float3(dir, 0.0f));
    float3 B = safe_normalize(cross(V, T));
    T = safe_normalize(cross(B, V));

    float3 proj_N_raw = N - B * dot(N, B);
    float proj_N_len = length(proj_N_raw);
    float3 proj_N = (proj_N_len > 1e-6f) ? (proj_N_raw / proj_N_len) : N;
    float3 proj_Ng = safe_normalize(Ng - B * dot(Ng, B));

    float2 h = (i == 0) ? horizons.xy : horizons.zw;

    float N_sin = dot(proj_N, T);
    float Ng_sin = dot(proj_Ng, T);
    float N_cos = saturate(dot(proj_N, V));
    float Ng_cos = saturate(dot(proj_Ng, V));

    float angle_Ng = sign(Ng_sin) * safe_acos(Ng_cos);
    float angle_N = sign(N_sin) * safe_acos(N_cos);

    h = goo_gtao_clamp_horizons(h, angle_N, inverted);

    float bent_angle = (h.x + h.y) * 0.5f;
    bent_accum += proj_N_len * (T * sin(bent_angle) + V * 0.5f * cos(bent_angle));

    h = goo_gtao_clamp_horizons(h, angle_Ng, inverted);

    float2 inner = -cos(2.0f * h - angle_N) + float2(N_cos) + 2.0f * h * N_sin;
    float slice_vis = (inner.x + inner.y) * 0.25f;
    vis_accum += proj_N_len * slice_vis;

    dir = float2(-dir.y, dir.x);
  }

  visibility = saturate(vis_accum * 0.5f);

  if (!use_bent_normals) {
    bent_normal = N;
  }
  else {
    float v2 = visibility * visibility;
    float tmp = saturate(v2 * v2 * v2);
    bent_normal = safe_normalize(mix(bent_accum, N, tmp));
  }
}

float goo_gtao_multibounce(float visibility, float3 albedo)
{
  float lum = dot(albedo, float3(0.3333333f));
  float a = 2.0404f * lum - 0.3324f;
  float b = -4.7951f * lum + 0.6417f;
  float c = 2.7552f * lum + 0.6903f;
  float x = visibility;
  return max(x, ((x * a + b) * x + c) * x);
}
