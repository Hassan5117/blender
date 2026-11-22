/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_effects_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_mblur_gather)

#include "draw_view_lib.glsl"
#include "eevee_utility_tx.bsl.hh"

float2 goo_mblur_clamp_len(float2 v, float max_len)
{
  float len = length(v);
  return (len > max_len && len > 1e-5f) ? (v * (max_len / len)) : v;
}

float4 goo_mblur_get_vel_px(int2 texel, float2 uv, float depth, int2 extent, float max_blur_px)
{
  float4 vel_uv;
  if (depth < 1.0f) {
    vel_uv = texelFetch(velocity_tx, texel, 0);
  }
  else {
    float3 wP = drw_point_screen_to_world(float3(uv, 1.0f));
    float4 c_prev = goo_mblur_buf.prev_persmat * float4(wP, 1.0f);
    float4 c_next = goo_mblur_buf.next_persmat * float4(wP, 1.0f);
    float2 uv_prev = (c_prev.xy / max(abs(c_prev.w), 1e-6f)) * 0.5f + 0.5f;
    float2 uv_next = (c_next.xy / max(abs(c_next.w), 1e-6f)) * 0.5f + 0.5f;
    vel_uv = float4(uv_prev - uv, uv_next - uv);
  }
  return float4(goo_mblur_clamp_len(vel_uv.xy * float2(extent), max_blur_px),
                goo_mblur_clamp_len(vel_uv.zw * float2(extent), max_blur_px));
}

float goo_mblur_depth_compare(float za, float zb, float depth_scale)
{
  return saturate(1.0f - (za - zb) * depth_scale * 0.1f);
}

void main()
{
  int2 extent = goo_mblur_buf.extent_params.xy;
  int2 tile_ext = goo_mblur_buf.extent_params.zw;
  int2 texel = int2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(texel, extent))) {
    return;
  }

  float4 center_col = texelFetch(src_color_tx, texel, 0);
  float max_blur_px = goo_mblur_buf.mblur_params.x;
  float depth_scale = goo_mblur_buf.mblur_params.y;
  if (max_blur_px < 0.5f) {
    imageStoreFast(dst_color_img, texel, center_col);
    return;
  }

  int2 tile_coord = clamp(texel / 16, int2(0), tile_ext - int2(1));
  float4 tile_vel = texelFetch(tile_tx, tile_coord, 0);
  float tile_max_speed = max(length(tile_vel.xy), length(tile_vel.zw));
  if (tile_max_speed < 0.5f) {
    imageStoreFast(dst_color_img, texel, center_col);
    return;
  }

  float2 inv_ext = 1.0f / float2(extent);
  float2 center_uv = (float2(texel) + 0.5f) * inv_ext;
  float center_d = texelFetch(depth_tx, texel, 0).r;
  float center_z = (center_d >= 1.0f) ? 1.0e4f : -drw_depth_screen_to_view(center_d);
  float4 center_vel = goo_mblur_get_vel_px(texel, center_uv, center_d, extent, max_blur_px);
  float center_speed = max(length(center_vel.xy), length(center_vel.zw));

  [[resource_table]] const UtilityTexture util_tx = resource_table_get(UtilityTexture);
  float noise = util_tx.fetch(float2(texel), float(UTIL_BLUE_NOISE_LAYER)).x;
  float jitter = fract(noise + goo_mblur_buf.mblur_params.z) - 0.5f;

  const int HALF_STEPS = 12;
  float4 accum = center_col;
  float total_w = 1.0f;

  for (int dir = 0; dir < 2; dir++) {
    float2 c_vel = (dir == 0) ? center_vel.xy : center_vel.zw;
    float2 t_vel = (dir == 0) ? tile_vel.xy : tile_vel.zw;
    float t_len = length(t_vel);
    if (t_len < 0.5f) {
      continue;
    }

    for (int s = 1; s <= HALF_STEPS; s++) {
      /* Alternate between center velocity (coherent streak) and dilated tile velocity. */
      float2 dir_vel = ((s & 1) == 0 && length(c_vel) > 0.5f) ? c_vel : t_vel;
      float t = (float(s) + jitter * 0.5f) / float(HALF_STEPS);
      t = clamp(t, 0.02f, 1.0f);

      float2 tap_uv = clamp(center_uv + dir_vel * t * inv_ext, float2(0.0f), float2(1.0f));
      int2 tap_texel = clamp(int2(tap_uv * float2(extent)), int2(0), extent - int2(1));
      float tap_d = texelFetch(depth_tx, tap_texel, 0).r;
      float tap_z = (tap_d >= 1.0f) ? 1.0e4f : -drw_depth_screen_to_view(tap_d);

      float4 tap_vel_4 = goo_mblur_get_vel_px(tap_texel, tap_uv, tap_d, extent, max_blur_px);
      float2 tap_v = (dir == 0) ? tap_vel_4.xy : tap_vel_4.zw;
      float tap_speed = length(tap_v);
      float sample_dist_px = length(dir_vel) * t;

      float fg = goo_mblur_depth_compare(tap_z, center_z, depth_scale);
      float bg = goo_mblur_depth_compare(center_z, tap_z, depth_scale);

      /* Exposure-conserving blend: foreground/background coverage plus stationary self weight. */
      float w_fg = fg * saturate((tap_speed + 0.5f - sample_dist_px) / 2.0f);
      float w_bg = bg * saturate((center_speed + 0.5f - sample_dist_px) / 2.0f);
      float w_tap = max(w_fg, w_bg);
      float w_self = 1.0f - w_tap;

      if (w_tap > 1e-4f) {
        float4 tap_col = texelFetch(src_color_tx, tap_texel, 0);
        accum += tap_col * w_tap;
      }
      if (w_self > 1e-4f) {
        accum += center_col * w_self;
      }
      total_w += 1.0f;
    }
  }

  imageStoreFast(dst_color_img, texel, accum / total_w);
}
