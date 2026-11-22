/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_indirect_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_ssr_trace)

#include "draw_view_lib.glsl"
#include "eevee_utility_tx.bsl.hh"
#include "gpu_shader_math_base_lib.glsl"
#include "gpu_shader_math_constants_lib.glsl"
#include "gpu_shader_math_matrix_construct_lib.glsl"
#include "gpu_shader_math_safe_lib.glsl"
#include "gpu_shader_math_vector_safe_lib.glsl"
#include "gpu_shader_utildefines_lib.glsl"

void main()
{
  int2 trace_texel = int2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(trace_texel, trace_extent))) {
    return;
  }

  int2 src_texel = min((half_res != 0) ? (trace_texel * 2) : trace_texel,
                       render_extent - int2(1));
  if (half_res != 0) {
    float best_d = 2.0f;
    for (int dy = 0; dy < 2; dy++) {
      for (int dx = 0; dx < 2; dx++) {
        int2 p = min(trace_texel * 2 + int2(dx, dy), render_extent - int2(1));
        float w = texelFetch(ssr_data_tx, p, 0).w;
        float d = texelFetch(hiz_depth_tx, p, 0).r;
        if (w >= 0.0f && d < best_d) {
          best_d = d;
          src_texel = p;
        }
      }
    }
  }

  float4 ssr_data = texelFetch(ssr_data_tx, src_texel, 0);
  float4 ssr_spec = texelFetch(ssr_spec_tx, src_texel, 0);
  float roughness = ssr_data.w;
  float max_rough = goo_probe_buf.ssr_params.y;
  if (roughness < 0.0f || ssr_spec.w <= 1e-4f || roughness > max_rough + 0.05f) {
    imageStoreFast(out_hit_img, trace_texel, float4(0.0f));
    return;
  }

  float depth = texelFetch(hiz_depth_tx, src_texel, 0).r;
  if (depth >= 1.0f) {
    imageStoreFast(out_hit_img, trace_texel, float4(0.0f));
    return;
  }

  float2 uv = (float2(src_texel) + 0.5f) / float2(render_extent);
  float3 vP = drw_point_screen_to_view(float3(uv, depth));
  float3 P = drw_point_view_to_world(vP);
  float3 N = safe_normalize(ssr_data.xyz);
  float3 V = drw_world_incident_vector(P);
  float3 R = safe_normalize(reflect(-V, N));

  if (roughness > 0.02f) {
    [[resource_table]] const UtilityTexture util_tx = resource_table_get(UtilityTexture);
    float2 noise = util_tx.fetch(float2(trace_texel), float(UTIL_BLUE_NOISE_LAYER)).xy;
    noise = fract(noise * 6.1803402f + goo_light_buf.sample_params.yz);
    float jitter_a = min(roughness * roughness * 0.25f, 0.08f);
    float phi = 2.0f * M_PI * noise.x;
    float r_jit = sqrt(noise.y) * jitter_a;
    float3x3 basis = from_up_axis(R);
    float3 R_jit = safe_normalize(R + basis * float3(cos(phi) * r_jit, sin(phi) * r_jit, 0.0f));
    if (dot(R_jit, N) > 0.05f) {
      R = R_jit;
    }
  }

  float3 vR = safe_normalize(drw_normal_world_to_view(R));
  float v_near = drw_depth_screen_to_view(0.0f);
  float max_t = 35.0f;
  if (vR.z > 1e-4f) {
    max_t = min(max_t, max((v_near - vP.z) * 0.9f / vR.z, 0.0f));
  }
  if (max_t <= 0.05f) {
    imageStoreFast(out_hit_img, trace_texel, float4(0.0f));
    return;
  }

  float3 ss_start = float3(uv, depth);
  float3 ss_end = drw_point_view_to_screen(vP + vR * max_t);
  float3 ss_delta = ss_end - ss_start;

  /* Clip ray segment to [0, 1]^2 screen UV bounds so all march steps stay on screen. */
  float clip_t = 1.0f;
  if (ss_delta.x > 1e-6f) {
    clip_t = min(clip_t, (1.0f - ss_start.x) / ss_delta.x);
  }
  else if (ss_delta.x < -1e-6f) {
    clip_t = min(clip_t, -ss_start.x / ss_delta.x);
  }
  if (ss_delta.y > 1e-6f) {
    clip_t = min(clip_t, (1.0f - ss_start.y) / ss_delta.y);
  }
  else if (ss_delta.y < -1e-6f) {
    clip_t = min(clip_t, -ss_start.y / ss_delta.y);
  }
  clip_t = clamp(clip_t, 0.0f, 1.0f);
  ss_delta *= clip_t;

  float pix_dist = length(ss_delta.xy * float2(render_extent));
  if (pix_dist < 1.5f) {
    imageStoreFast(out_hit_img, trace_texel, float4(0.0f));
    return;
  }

  int max_steps = clamp(int(mix(24.0f, 96.0f, goo_probe_buf.ssr_params.x)), 16, 96);
  float thickness = max(goo_probe_buf.ssr_params.z, 0.02f);

  float t_min = max(2.5f / pix_dist, 0.5f / float(max_steps));
  float dt = (1.0f - t_min) / float(max_steps);
  float t_prev = t_min;
  float t_hit = 0.0f;
  bool found = false;

  float step_jitter = fract(sin(dot(float2(trace_texel), float2(12.9898f, 78.233f)) +
                                goo_light_buf.sample_params.y * 17.3f) *
                            43758.5453f);

  for (int i = 0; i < max_steps; i++) {
    float t = t_min + (float(i) + step_jitter) * dt;
    if (t > 1.0f) {
      break;
    }
    float3 cur_ss = ss_start + ss_delta * t;
    if (any(lessThan(cur_ss.xy, float2(0.0f))) || any(greaterThan(cur_ss.xy, float2(1.0f))) ||
        cur_ss.z <= 0.0f || cur_ss.z >= 1.0f)
    {
      break;
    }

    float surf_z = textureLod(hiz_depth_tx, cur_ss.xy, 0.0f).r;
    if (surf_z >= 1.0f) {
      t_prev = t;
      continue;
    }

    float ray_vz = drw_depth_screen_to_view(cur_ss.z);
    float surf_vz = drw_depth_screen_to_view(surf_z);
    float diff = surf_vz - ray_vz;
    if (diff > 0.002f && diff < thickness) {
      t_hit = t;
      found = true;
      break;
    }
    t_prev = t;
  }

  if (!found) {
    imageStoreFast(out_hit_img, trace_texel, float4(0.0f));
    return;
  }

  float lo = max(t_prev, t_min);
  float hi = t_hit;
  for (int b = 0; b < 5; b++) {
    float mid = 0.5f * (lo + hi);
    float3 mid_ss = ss_start + ss_delta * mid;
    float surf_z = textureLod(hiz_depth_tx, mid_ss.xy, 0.0f).r;
    float ray_vz = drw_depth_screen_to_view(mid_ss.z);
    float surf_vz = drw_depth_screen_to_view(surf_z);
    float diff = surf_vz - ray_vz;
    if (surf_z < 1.0f && diff > 0.002f && diff < thickness * 1.5f) {
      hi = mid;
    }
    else {
      lo = mid;
    }
  }

  float2 hit_uv = clamp(ss_start.xy + ss_delta.xy * hi, float2(0.0f), float2(1.0f));
  float border = min(min(hit_uv.x, 1.0f - hit_uv.x), min(hit_uv.y, 1.0f - hit_uv.y));
  float border_fade = max(goo_probe_buf.ssr_params.w, 1e-4f);
  float edge_w = saturate(border / border_fade);
  float facing_w = saturate(1.0f - max(vR.z, 0.0f) * 2.0f);
  float rough_w = 1.0f - smoothstep(max_rough * 0.75f, max(max_rough, 1e-4f), roughness);
  float conf = edge_w * facing_w * rough_w;

  imageStoreFast(out_hit_img, trace_texel, float4(hit_uv, saturate(hi), saturate(conf)));
}
