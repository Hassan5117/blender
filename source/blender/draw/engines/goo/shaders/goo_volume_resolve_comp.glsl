/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_effects_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_volume_resolve)

#include "goo_volume_lib.glsl"

void main()
{
  int2 extent = imageSize(color_img);
  int2 texel = int2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(texel, extent))) {
    return;
  }

  float depth = texelFetch(depth_tx, texel, 0).r;
  float dist = (depth >= 1.0f) ? goo_vol_buf.depth_params.y : -drw_depth_screen_to_view(depth);
  if (dist <= goo_vol_buf.depth_params.x) {
    return;
  }

  float2 screen_uv = (float2(texel) + 0.5f) * goo_vol_buf.coord_params.zw;
  float2 froxel_uv = screen_uv * goo_vol_buf.coord_params.xy;
  float z_norm = goo_vol_dist_to_z(dist);
  float d_count = float(max(goo_vol_buf.froxel_size.z, 1));
  float inv_d = 1.0f / d_count;
  float z_slice = z_norm * d_count;

  float3 in_scat;
  float3 trans;
  if (z_slice < 1.0f) {
    float3 uvw0 = float3(froxel_uv, 0.5f * inv_d);
    float3 end_scat = textureLod(integrated_scatter_tx, uvw0, 0.0f).rgb;
    float3 end_trans = textureLod(integrated_transmit_tx, uvw0, 0.0f).rgb;
    float fac = saturate(z_slice);
    in_scat = end_scat * fac;
    trans = mix(float3(1.0f), end_trans, fac);
  }
  else {
    float z_tex = clamp((z_slice - 0.5f) * inv_d, 0.5f * inv_d, 1.0f - 0.5f * inv_d);
    float3 uvw = float3(froxel_uv, z_tex);
    in_scat = textureLod(integrated_scatter_tx, uvw, 0.0f).rgb;
    trans = textureLod(integrated_transmit_tx, uvw, 0.0f).rgb;
  }

  float4 cur = imageLoadFast(color_img, texel);
  float3 new_rgb = cur.rgb * trans + in_scat;
  float avg_t = saturate((trans.r + trans.g + trans.b) * (1.0f / 3.0f));
  float new_a = max(cur.a, 1.0f - avg_t);
  imageStoreFast(color_img, texel, float4(new_rgb, new_a));
}
