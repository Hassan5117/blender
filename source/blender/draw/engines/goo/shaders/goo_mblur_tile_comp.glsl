/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_effects_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_mblur_tile)

#include "draw_view_lib.glsl"

shared float4 sh_max_vel[256];

float2 goo_mblur_clamp_len(float2 v, float max_len)
{
  float len = length(v);
  return (len > max_len && len > 1e-5f) ? (v * (max_len / len)) : v;
}

float4 goo_mblur_pixel_vel_px(int2 texel, int2 extent, float max_blur_px)
{
  float2 uv = (float2(texel) + 0.5f) / float2(extent);
  float depth = texelFetch(depth_tx, texel, 0).r;
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
  float2 prev_px = goo_mblur_clamp_len(vel_uv.xy * float2(extent), max_blur_px);
  float2 next_px = goo_mblur_clamp_len(vel_uv.zw * float2(extent), max_blur_px);
  return float4(prev_px, next_px);
}

void main()
{
  int2 extent = goo_mblur_buf.extent_params.xy;
  int2 texel = int2(gl_GlobalInvocationID.xy);
  float max_blur_px = goo_mblur_buf.mblur_params.x;

  float4 vel_px = any(greaterThanEqual(texel, extent)) ?
                      float4(0.0f) :
                      goo_mblur_pixel_vel_px(texel, extent, max_blur_px);

  uint lid = gl_LocalInvocationIndex;
  sh_max_vel[lid] = vel_px;

  for (uint stride = 128u; stride > 0u; stride >>= 1u) {
    barrier();
    if (lid < stride) {
      float4 a = sh_max_vel[lid];
      float4 b = sh_max_vel[lid + stride];
      if (dot(b.xy, b.xy) > dot(a.xy, a.xy)) {
        a.xy = b.xy;
      }
      if (dot(b.zw, b.zw) > dot(a.zw, a.zw)) {
        a.zw = b.zw;
      }
      sh_max_vel[lid] = a;
    }
  }
  barrier();

  if (lid == 0u) {
    imageStoreFast(out_tile_img, int2(gl_WorkGroupID.xy), sh_max_vel[0]);
  }
}
