/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_effects_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_volume_scatter)

#include "draw_view_lib.glsl"
#include "eevee_utility_tx.bsl.hh"
#include "goo_probe_lib.glsl"
#include "goo_volume_lib.glsl"
#include "gpu_shader_math_constants_lib.glsl"

float goo_phase_hg(float cos_theta, float g)
{
  float g2 = g * g;
  float denom = max(1.0f + g2 - 2.0f * g * cos_theta, 1e-4f);
  return (1.0f - g2) / (4.0f * M_PI * pow(denom, 1.5f));
}

float goo_vol_shadow_map(GooLightData ld, float3 P)
{
  if (ld.spot_shadow.y < -0.5f) {
    return 1.0f;
  }
  int shadow_start = int(ld.spot_shadow.y + 0.5f);
  int shadow_count = int(ld.spot_shadow.z + 0.5f);
  int light_type = int(ld.position_type.w + 0.5f);

  int slice = -1;
  float4 clip = float4(0.0f);

  if (light_type == GOO_LIGHT_SUN) {
    for (int c = 0; c < shadow_count; c++) {
      GooShadowSlice sc = goo_shadow_buf.slices[shadow_start + c];
      float4 cl = sc.vp * float4(P, 1.0f);
      if (cl.w > 0.0f) {
        float3 co = (cl.xyz / cl.w) * 0.5f + 0.5f;
        if (co.x >= 0.01f && co.x <= 0.99f && co.y >= 0.01f && co.y <= 0.99f && co.z >= 0.0f &&
            co.z <= 1.0f)
        {
          slice = shadow_start + c;
          clip = cl;
          break;
        }
      }
    }
  }
  else if (light_type == GOO_LIGHT_SPOT) {
    slice = shadow_start;
    clip = goo_shadow_buf.slices[slice].vp * float4(P, 1.0f);
  }
  else if (light_type == GOO_LIGHT_POINT && shadow_count == 6) {
    float3 d = P - ld.position_type.xyz;
    float3 ad = abs(d);
    int face = 0;
    if (ad.x >= ad.y && ad.x >= ad.z) {
      face = (d.x >= 0.0f) ? 0 : 1;
    }
    else if (ad.y >= ad.z) {
      face = (d.y >= 0.0f) ? 2 : 3;
    }
    else {
      face = (d.z >= 0.0f) ? 4 : 5;
    }
    slice = shadow_start + face;
    clip = goo_shadow_buf.slices[slice].vp * float4(P, 1.0f);
  }

  if (slice < 0 || clip.w <= 1e-6f) {
    return 1.0f;
  }

  GooShadowSlice s = goo_shadow_buf.slices[slice];
  float3 coord = (clip.xyz / clip.w) * 0.5f + 0.5f;
  if (coord.x < 0.0f || coord.x > 1.0f || coord.y < 0.0f || coord.y > 1.0f || coord.z < 0.0f ||
      coord.z > 1.0f)
  {
    return 1.0f;
  }

  float stored_depth = textureLod(goo_shadow_tx, float3(coord.xy, float(slice)), 0.0f).r;
  return (coord.z - s.params.y <= stored_depth) ? 1.0f : 0.0f;
}

float3 goo_vol_self_shadow(float3 P, float3 L, float max_dist, float jitter)
{
  int steps = clamp(int(goo_vol_buf.inv_froxel_size.w + 0.5f), 1, 32);
  float march_dist = min(max_dist, max(goo_vol_buf.depth_params.y - goo_vol_buf.depth_params.x, 1.0f));
  float step_len = march_dist / float(steps);
  float3 optical_depth = float3(0.0f);

  for (int s = 0; s < 32; s++) {
    if (s >= steps) {
      break;
    }
    float t = (float(s) + 0.25f + 0.5f * jitter) * step_len;
    float3 sample_P = P + L * t;
    float3 v_s = drw_point_world_to_view(sample_P);
    float d_s = -v_s.z;
    if (d_s <= goo_vol_buf.depth_params.x || d_s >= goo_vol_buf.depth_params.y) {
      continue;
    }
    float2 ss = drw_point_view_to_screen(v_s).xy;
    if (ss.x < 0.0f || ss.x > 1.0f || ss.y < 0.0f || ss.y > 1.0f) {
      continue;
    }
    float z_s = goo_vol_dist_to_z(d_s);
    float3 ext_s = max(
        textureLod(prop_extinct_tx, float3(ss * goo_vol_buf.coord_params.xy, z_s), 0.0f).rgb,
        float3(0.0f));
    optical_depth += ext_s * step_len;
  }

  return exp(-optical_depth);
}

void main()
{
  int3 coord = int3(gl_GlobalInvocationID.xyz);
  int3 froxel_size = goo_vol_buf.froxel_size.xyz;
  if (any(greaterThanEqual(coord, froxel_size))) {
    return;
  }

  float4 scat_prop = texelFetch(prop_scatter_tx, coord, 0);
  float4 emit_prop = texelFetch(prop_emissive_tx, coord, 0);
  float3 sigma_s = max(scat_prop.rgb, float3(0.0f));
  float3 emit = max(emit_prop.rgb, float3(0.0f));

  if (all(lessThanEqual(sigma_s, float3(1e-6f))) && all(lessThanEqual(emit, float3(1e-6f)))) {
    imageStoreFast(out_scatter_lit_img, coord, float4(0.0f));
    return;
  }

  float4 ext_prop = texelFetch(prop_extinct_tx, coord, 0);
  float g = (ext_prop.a > 1e-6f) ? clamp(scat_prop.a / ext_prop.a, -0.95f, 0.95f) : 0.0f;

  [[resource_table]] const UtilityTexture util_tx = resource_table_get(UtilityTexture);
  float2 noise = util_tx.fetch(float2(coord.xy), float(UTIL_BLUE_NOISE_LAYER)).xy;
  float z_t = fract(noise.y + float(sample_index) * 0.61803399f + float(coord.z) * 0.38196601f);

  float inv_d = goo_vol_buf.inv_froxel_size.z;
  float dist0 = goo_vol_z_to_dist(float(coord.z) * inv_d);
  float dist1 = goo_vol_z_to_dist(float(coord.z + 1) * inv_d);
  float sample_dist = mix(dist0, dist1, z_t);

  float2 coord_scale = max(goo_vol_buf.coord_params.xy, float2(1e-6f));
  float2 screen_uv = ((float2(coord.xy) + 0.5f) * goo_vol_buf.inv_froxel_size.xy) / coord_scale;
  float3 vP = goo_vol_ray_view_pos(screen_uv, sample_dist);
  float3 P = drw_point_view_to_world(vP);
  float3 V = drw_world_incident_vector(P);

  float3 direct = float3(0.0f);
  int flags = goo_vol_buf.froxel_size.w;
  if ((flags & GOO_VOL_FLAG_LIGHTS) != 0 && any(greaterThan(sigma_s, float3(1e-6f)))) {
    bool do_vol_shadows = (flags & GOO_VOL_FLAG_SHADOWS) != 0;
    float light_clamp = goo_vol_buf.clamp_params.x;

    for (int i = 0; i < goo_light_buf.light_count; i++) {
      GooLightData ld = goo_light_buf.lights[i];
      int light_type = int(ld.position_type.w + 0.5f);
      float3 L;
      float att = 1.0f;
      float light_dist = 25.0f;

      if (light_type == GOO_LIGHT_SUN) {
        L = normalize(ld.position_type.xyz);
      }
      else {
        float3 to_light = ld.position_type.xyz - P;
        float dist_sqr = dot(to_light, to_light);
        light_dist = sqrt(max(dist_sqr, 1e-6f));
        float radius = max(0.05f, ld.color_energy.a);
        float denom = max(dist_sqr, radius * radius);
        L = to_light / light_dist;
        att = 1.0f / denom;
        if (light_type == GOO_LIGHT_SPOT) {
          float cos_angle = dot(-L, ld.direction_spot_cos.xyz);
          float spot_att = clamp(
              (cos_angle - ld.direction_spot_cos.w) * ld.spot_shadow.x, 0.0f, 1.0f);
          att *= spot_att * spot_att;
        }
        else if (light_type == GOO_LIGHT_AREA) {
          att *= max(0.0f, dot(-L, ld.direction_spot_cos.xyz));
        }
      }

      if (att <= 1e-6f) {
        continue;
      }

      float vis = goo_vol_shadow_map(ld, P);
      if (vis <= 1e-5f) {
        continue;
      }

      float3 vol_trans = do_vol_shadows ? goo_vol_self_shadow(P, L, light_dist, noise.x) :
                                          float3(1.0f);
      float phase = goo_phase_hg(dot(-L, V), g);
      float3 contrib = ld.color_energy.rgb * ld.spot_shadow.w * att * vis * vol_trans * phase;
      if (light_clamp > 0.0f) {
        float lum = max(contrib.r, max(contrib.g, contrib.b));
        if (lum > light_clamp) {
          contrib *= (light_clamp / lum);
        }
      }
      direct += contrib;
    }

    float direct_clamp = goo_vol_buf.clamp_params.y;
    if (direct_clamp > 0.0f) {
      float dlum = max(direct.r, max(direct.g, direct.b));
      if (dlum > direct_clamp) {
        direct *= (direct_clamp / dlum);
      }
    }
  }

  float3 indirect = float3(0.0f);
  if (any(greaterThan(sigma_s, float3(1e-6f)))) {
    indirect = goo_eval_diffuse_indirect(P, -V, V);
    float ind_clamp = goo_vol_buf.clamp_params.z;
    if (ind_clamp > 0.0f) {
      float ilum = max(indirect.r, max(indirect.g, indirect.b));
      if (ilum > ind_clamp) {
        indirect *= (ind_clamp / ilum);
      }
    }
  }

  float3 total_lit = max(sigma_s * (direct + indirect) + emit, float3(0.0f));
  imageStoreFast(out_scatter_lit_img, coord, float4(total_lit, 1.0f));
}
