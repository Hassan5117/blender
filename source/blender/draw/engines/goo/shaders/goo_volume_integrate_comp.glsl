/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_effects_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_volume_integrate)

#include "goo_volume_lib.glsl"

void main()
{
  int2 texel = int2(gl_GlobalInvocationID.xy);
  int3 froxel_size = goo_vol_buf.froxel_size.xyz;
  if (any(greaterThanEqual(texel, froxel_size.xy))) {
    return;
  }

  float2 coord_scale = max(goo_vol_buf.coord_params.xy, float2(1e-6f));
  float2 screen_uv = ((float2(texel) + 0.5f) * goo_vol_buf.inv_froxel_size.xy) / coord_scale;
  float inv_d = goo_vol_buf.inv_froxel_size.z;

  float3 accum_scatter = float3(0.0f);
  float3 accum_transmit = float3(1.0f);

  float dist0 = goo_vol_z_to_dist(0.0f);
  float3 vP0 = goo_vol_ray_view_pos(screen_uv, dist0);

  for (int z = 0; z < froxel_size.z; z++) {
    float dist1 = goo_vol_z_to_dist(float(z + 1) * inv_d);
    float3 vP1 = goo_vol_ray_view_pos(screen_uv, dist1);
    float ds = length(vP1 - vP0);
    vP0 = vP1;

    int3 coord = int3(texel, z);
    float3 sigma_t = max(texelFetch(prop_extinct_tx, coord, 0).rgb, float3(0.0f));
    float3 in_scat = max(texelFetch(scatter_lit_tx, coord, 0).rgb, float3(0.0f));

    float3 slice_transmit = exp(-sigma_t * ds);
    float3 tau = sigma_t * ds;
    float3 step_integ = in_scat * ds;
    for (int c = 0; c < 3; c++) {
      if (tau[c] > 1e-4f) {
        step_integ[c] = in_scat[c] * (1.0f - slice_transmit[c]) / sigma_t[c];
      }
    }

    accum_scatter += accum_transmit * step_integ;
    accum_transmit *= slice_transmit;

    imageStoreFast(out_integrated_scatter_img, coord, float4(accum_scatter, 1.0f));
    imageStoreFast(out_integrated_transmit_img, coord, float4(accum_transmit, 1.0f));
  }
}
