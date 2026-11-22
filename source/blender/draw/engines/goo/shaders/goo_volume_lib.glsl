/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Shared froxel depth distribution and view-ray reconstruction helpers for Goo Volumetrics. */

#pragma once

#include "draw_view_lib.glsl"

float goo_vol_z_to_dist(float z_norm)
{
  float z = saturate(z_norm);
  float near = goo_vol_buf.depth_params.x;
  float far = goo_vol_buf.depth_params.y;
  float distrib = saturate(goo_vol_buf.depth_params.z);
  bool is_persp = goo_vol_buf.depth_params.w > 0.5f;
  if (!is_persp || distrib <= 1e-4f || near <= 1e-5f) {
    return mix(near, far, z);
  }
  float shift = near * ((1.0f - distrib) / distrib) * 4.0f;
  float a = near + shift;
  float c = far + shift;
  return a * pow(max(c / a, 1.0001f), z) - shift;
}

float goo_vol_dist_to_z(float dist)
{
  float near = goo_vol_buf.depth_params.x;
  float far = goo_vol_buf.depth_params.y;
  float distrib = saturate(goo_vol_buf.depth_params.z);
  bool is_persp = goo_vol_buf.depth_params.w > 0.5f;
  if (!is_persp || distrib <= 1e-4f || near <= 1e-5f) {
    return saturate((dist - near) / max(far - near, 1e-6f));
  }
  float shift = near * ((1.0f - distrib) / distrib) * 4.0f;
  float a = near + shift;
  float c = far + shift;
  float d = clamp(dist, near, far);
  return saturate(log((d + shift) / a) / max(log(c / a), 1e-6f));
}

float3 goo_vol_ray_view_pos(float2 screen_uv, float dist)
{
  float3 v_near = drw_point_screen_to_view(float3(screen_uv, 0.0f));
  float3 v_far = drw_point_screen_to_view(float3(screen_uv, 1.0f));
  float denom = v_far.z - v_near.z;
  float t = (abs(denom) > 1e-6f) ? ((-dist - v_near.z) / denom) : 0.0f;
  return mix(v_near, v_far, t);
}
