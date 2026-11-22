/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "goo_octahedral_lib.glsl"
#include "gpu_shader_math_base_lib.glsl"
#include "gpu_shader_math_matrix_construct_lib.glsl"
#include "gpu_shader_math_matrix_conversion_lib.glsl"
#include "gpu_shader_math_vector_reduce_lib.glsl"
#include "gpu_shader_utildefines_lib.glsl"

#ifdef GOO_INDIRECT_READ

float3 goo_sample_env_probe(int layer, float3 dir, float lod)
{
  float2 oct_uv = goo_oct_dir_to_uv(dir);
  float max_mip = max(goo_probe_buf.indirect_params.w - 1.0f, 0.0f);
  float lod_clamped = clamp(lod, 0.0f, max_mip);
  float lod0 = floor(lod_clamped);
  float lod1 = min(lod0 + 1.0f, max_mip);
  float frac = lod_clamped - lod0;

  float base_res = float(max(goo_probe_buf.env_oct_res, 8));
  float res0 = max(base_res / exp2(lod0), 2.0f);
  float res1 = max(base_res / exp2(lod1), 2.0f);

  float3 s0 = textureLod(goo_env_probe_tx, float3(goo_oct_sample_uv(oct_uv, res0), float(layer)), lod0)
                  .rgb;
  float3 s1 = textureLod(goo_env_probe_tx, float3(goo_oct_sample_uv(oct_uv, res1), float(layer)), lod1)
                  .rgb;
  return mix(s0, s1, frac);
}

float3 goo_eval_diffuse_indirect(float3 P, float3 N, float3 V)
{
  float3 indirect_scale = float3(max(goo_probe_buf.indirect_params.y, 0.0f));
  float3 world_irrad = (goo_probe_buf.lookdev_params.w > 0.5f) ?
                           goo_eval_sh_lambert(goo_volume_sh_buf[0], N) :
                           goo_light_buf.world_ambient.rgb;

  float3 vol_accum = float3(0.0f);
  float vol_weight = 0.0f;
  int vol_count = ((goo_probe_buf.capture_flags & GOO_CAPTURE_VOLUME) != 0) ?
                      0 :
                      min(goo_probe_buf.volume_probe_count, GOO_MAX_VOLUME_PROBES);

  for (int v = 0; v < vol_count; v++) {
    GooVolumeProbeData vp = goo_volume_probe_buf[v];
    float3 biased_P = P + N * vp.params.z + V * vp.params.w;
    float3 lP = (vp.world_to_volume * float4(biased_P, 1.0f)).xyz;
    float dist = reduce_max(abs(lP));
    if (dist >= 1.0f) {
      continue;
    }

    float falloff = clamp(vp.params.x, 1e-4f, 1.0f);
    float w = saturate((1.0f - dist) / falloff);
    w = w * w * (3.0f - 2.0f * w);
    if (w <= 0.0f) {
      continue;
    }

    int3 res = max(vp.res_offset.xyz, int3(1));
    int offset = vp.res_offset.w;
    float3 grid_f = saturate(lP * 0.5f + 0.5f) * float3(res - int3(1));
    int3 i0 = min(int3(floor(grid_f)), res - int3(1));
    int3 i1 = min(i0 + int3(1), res - int3(1));
    float3 f = grid_f - float3(i0);

    float3 irrad = float3(0.0f);
    for (int dz = 0; dz < 2; dz++) {
      int z = (dz == 0) ? i0.z : i1.z;
      float wz = (dz == 0) ? (1.0f - f.z) : f.z;
      for (int dy = 0; dy < 2; dy++) {
        int y = (dy == 0) ? i0.y : i1.y;
        float wy = (dy == 0) ? (1.0f - f.y) : f.y;
        for (int dx = 0; dx < 2; dx++) {
          int x = (dx == 0) ? i0.x : i1.x;
          float wx = (dx == 0) ? (1.0f - f.x) : f.x;
          int cell_idx = offset + (z * res.y + y) * res.x + x;
          float cell_w = wx * wy * wz;
          if (cell_w > 0.0f) {
            irrad += goo_eval_sh_lambert(goo_volume_sh_buf[cell_idx], N) * cell_w;
          }
        }
      }
    }

    vol_accum += irrad * max(vp.params.y, 0.0f) * w;
    vol_weight += w;
  }

  float3 result = (vol_weight > 1e-5f) ?
                      mix(world_irrad, vol_accum / max(vol_weight, 1.0f), saturate(vol_weight)) :
                      world_irrad;
  return result * indirect_scale;
}

float goo_line_unit_sphere_intersect(float3 o, float3 d)
{
  float a = dot(d, d);
  float b = dot(o, d);
  float c = dot(o, o) - 1.0f;
  float disc = max(b * b - a * c, 0.0f);
  float inv_a = 1.0f / max(a, 1e-6f);
  return (-b + sqrt(disc)) * inv_a;
}

float goo_line_unit_box_intersect(float3 o, float3 d)
{
  float3 inv_d = 1.0f / mix(d, float3(1e-6f), lessThan(abs(d), float3(1e-6f)));
  float3 t0 = (-float3(1.0f) - o) * inv_d;
  float3 t1 = (float3(1.0f) - o) * inv_d;
  float3 tmax = max(t0, t1);
  return max(min(tmax.x, min(tmax.y, tmax.z)), 0.0f);
}

float3 goo_eval_env_direction(float3 P, float3 dir, float roughness)
{
  float r_clamped = clamp(roughness, 0.0f, 1.0f);
  float max_mip = max(goo_probe_buf.indirect_params.w - 1.0f, 0.0f);
  float lod = sqrt(r_clamped) * max_mip;
  float indirect_scale = max(goo_probe_buf.indirect_params.y, 0.0f);

  float3 world_col = (goo_probe_buf.lookdev_params.w > 0.5f) ?
                         goo_sample_env_probe(0, dir, lod) :
                         (goo_light_buf.world_ambient.rgb * 0.5f);

  /* Sphere probes are sorted smallest-volume first in C++. Blend top matches cleanly. */
  float3 probe_accum = float3(0.0f);
  float probe_weight = 0.0f;
  int sphere_count = ((goo_probe_buf.capture_flags & (GOO_CAPTURE_SPHERE | GOO_CAPTURE_VOLUME)) !=
                      0) ?
                         0 :
                         min(goo_probe_buf.sphere_probe_count, GOO_MAX_SPHERE_PROBES);

  for (int i = 0; i < sphere_count; i++) {
    GooSphereProbeData sp = goo_sphere_probe_buf[i];
    float3 lP_inf = (sp.world_to_influence * float4(P, 1.0f)).xyz;
    float grad = (sp.params.y == GOO_PROBE_SHAPE_ELLIPSOID) ? length(lP_inf) :
                                                              reduce_max(abs(lP_inf));
    if (grad >= 1.0f) {
      continue;
    }

    float falloff = clamp(sp.location_falloff.w, 1e-4f, 1.0f);
    float w = saturate((1.0f - grad) / falloff);
    w = w * w * (3.0f - 2.0f * w);
    float rem = 1.0f - probe_weight;
    w = min(w, rem);
    if (w <= 1e-5f) {
      continue;
    }

    float3 par_o = (sp.world_to_parallax * float4(P, 1.0f)).xyz;
    float3 par_d = to_float3x3(sp.world_to_parallax) * dir;
    float dist = (sp.params.z == GOO_PROBE_SHAPE_ELLIPSOID) ?
                     goo_line_unit_sphere_intersect(par_o, par_d) :
                     goo_line_unit_box_intersect(par_o, par_d);
    float3 corr_dir = safe_normalize(P + dir * max(dist, 0.0f) - sp.location_falloff.xyz);
    float3 samp = goo_sample_env_probe(sp.params.x, corr_dir, lod) * max(sp.extra.x, 0.0f);

    probe_accum += samp * w;
    probe_weight += w;
    if (probe_weight >= 0.999f) {
      break;
    }
  }

  float3 combined = probe_accum + world_col * (1.0f - probe_weight);
  return combined * indirect_scale;
}

float3 goo_eval_specular_indirect(
    float3 P, float3 N, float3 V, float roughness, float &out_planar_conf)
{
  float3 R = reflect(-V, N);
  float3 env_col = goo_eval_env_direction(P, R, roughness);
  out_planar_conf = 0.0f;

  int planar_count = min(goo_probe_buf.planar_probe_count, GOO_MAX_PLANAR_PROBES);
  if (planar_count <= 0 ||
      (goo_probe_buf.capture_flags & (GOO_CAPTURE_PLANAR | GOO_CAPTURE_SPHERE | GOO_CAPTURE_VOLUME)) !=
          0)
  {
    return env_col;
  }

  float best_weight = 0.0f;
  float3 best_col = float3(0.0f);
  float indirect_scale = max(goo_probe_buf.indirect_params.y, 0.0f);

  for (int i = 0; i < planar_count; i++) {
    GooPlanarProbeData pp = goo_planar_probe_buf[i];
    float align = dot(N, pp.normal_falloff.xyz);
    if (align <= 0.1f) {
      continue;
    }

    float3 lP = (pp.world_to_plane * float4(P, 1.0f)).xyz;
    float abs_xy = max(abs(lP.x), abs(lP.y));
    float abs_z = abs(lP.z);
    if (abs_xy >= 1.0f || abs_z >= 1.0f) {
      continue;
    }

    float falloff = clamp(pp.normal_falloff.w, 1e-4f, 1.0f);
    float z_fade = saturate((1.0f - abs_z) / falloff);
    float xy_fade = saturate((1.0f - abs_xy) / max(falloff * 0.25f, 0.05f));
    float align_fade = smoothstep(0.1f, 0.6f, align);
    float box_w = z_fade * xy_fade * align_fade;
    if (box_w <= 0.0f) {
      continue;
    }

    float parallax_d = max(pp.params.y, 1.0f);
    float3 Q = P + R * parallax_d;
    float4 clip = pp.ref_vp * float4(Q, 1.0f);
    if (clip.w <= 1e-5f) {
      continue;
    }
    float2 uv = (clip.xy / clip.w) * 0.5f + 0.5f;
    if (any(lessThan(uv, float2(0.0f))) || any(greaterThan(uv, float2(1.0f)))) {
      continue;
    }

    float border_dist = min(min(uv.x, 1.0f - uv.x), min(uv.y, 1.0f - uv.y));
    float border_fade = smoothstep(0.0f, 0.03f, border_dist);
    float w = box_w * border_fade;

    float lod = sqrt(clamp(roughness, 0.0f, 1.0f)) * float(GOO_PLANAR_MIPS - 1);
    float4 samp = textureLod(goo_planar_tx, float3(uv, pp.params.x), lod);
    if (samp.a <= 1e-4f) {
      continue;
    }
    w *= saturate(samp.a);
    if (w > best_weight) {
      best_weight = w;
      best_col = (samp.rgb / max(samp.a, 1e-4f)) * max(pp.params.z, 0.0f) * indirect_scale;
    }
  }

  out_planar_conf = saturate(best_weight);
  return mix(env_col, best_col, out_planar_conf);
}

#endif
