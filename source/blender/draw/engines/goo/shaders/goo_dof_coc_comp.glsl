/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_effects_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_dof_coc)

#include "draw_view_lib.glsl"

shared float sh_fg_coc[256];
shared float sh_bg_coc[256];

void main()
{
  int2 extent = int2(goo_dof_buf.filter_params.xy + 0.5f);
  int2 texel = int2(gl_GlobalInvocationID.xy);
  bool valid = !any(greaterThanEqual(texel, extent));

  float fg_max = 0.0f;
  float bg_max = 0.0f;

  if (valid) {
    float depth = texelFetch(depth_tx, texel, 0).r;
    float vz = (depth >= 1.0f) ? -1.0e5f : drw_depth_screen_to_view(depth);
    float dist = max(-vz, 1e-4f);

    float fx_radius = goo_dof_buf.coc_params.x;
    float focus_dist = max(goo_dof_buf.coc_params.y, 1e-4f);
    float max_coc = abs(goo_dof_buf.coc_params.z);
    float proj_scale = goo_dof_buf.coc_params.w;

    float signed_coc = 0.0f;
    if (drw_view_is_perspective()) {
      signed_coc = fx_radius * proj_scale * (1.0f / focus_dist - 1.0f / dist);
    }
    else {
      signed_coc = fx_radius * proj_scale * (dist - focus_dist);
    }
    signed_coc = clamp(signed_coc, -max_coc, max_coc);

    fg_max = max(-signed_coc, 0.0f);
    bg_max = max(signed_coc, 0.0f);

    float4 col = texelFetch(src_color_tx, texel, 0);
    float denoise_fac = saturate(goo_dof_buf.filter_params.z);
    if (denoise_fac > 0.001f && abs(signed_coc) > 1.0f) {
      float3 nmax = float3(0.0f);
      float3 nmin = float3(1.0e9f);
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          if (dx == 0 && dy == 0) {
            continue;
          }
          int2 nt = clamp(texel + int2(dx, dy), int2(0), extent - int2(1));
          float3 nc = texelFetch(src_color_tx, nt, 0).rgb;
          nmax = max(nmax, nc);
          nmin = min(nmin, nc);
        }
      }
      float max_l = max(nmax.r, max(nmax.g, nmax.b));
      float cur_l = max(col.r, max(col.g, col.b));
      if (cur_l > max(max_l, 1e-4f) * 1.5f) {
        col.rgb = mix(col.rgb, clamp(col.rgb, nmin, nmax), denoise_fac);
      }
    }

    imageStoreFast(out_coc_img, texel, float4(signed_coc));
    imageStoreFast(out_color_img, texel, col);
  }

  uint lid = gl_LocalInvocationIndex;
  sh_fg_coc[lid] = fg_max;
  sh_bg_coc[lid] = bg_max;
  for (uint stride = 128u; stride > 0u; stride >>= 1u) {
    barrier();
    if (lid < stride) {
      sh_fg_coc[lid] = max(sh_fg_coc[lid], sh_fg_coc[lid + stride]);
      sh_bg_coc[lid] = max(sh_bg_coc[lid], sh_bg_coc[lid + stride]);
    }
  }
  barrier();

  if (lid == 0u) {
    imageStoreFast(out_tile_img, int2(gl_WorkGroupID.xy), float4(sh_fg_coc[0], sh_bg_coc[0], 0.0f, 0.0f));
  }
}
