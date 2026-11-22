/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_material_infos.hh"

FRAGMENT_SHADER_CREATE_INFO(goo_material_volume_world)

#include "draw_shader_shared.hh"
#include "draw_view_lib.glsl"
#include "eevee_utility_tx.bsl.hh"
#include "goo_attributes_volume_lib.glsl"
#include "goo_geom_types_lib.glsl"
#include "goo_vol_nodetree_frag_lib.glsl"
#include "goo_volume_lib.glsl"
#include "goo_volume_nodetree_lib.glsl"

void main()
{
  int2 texel = int2(gl_FragCoord.xy);
  int3 froxel_size = goo_vol_buf.froxel_size.xyz;
  if (any(lessThan(texel, int2(0))) || any(greaterThanEqual(texel, froxel_size.xy))) {
    out_dummy = float4(0.0f);
    return;
  }

  float dist_entry = goo_vol_buf.depth_params.x;
  float dist_exit = goo_vol_buf.depth_params.y;

#ifdef MAT_GEOM_VOLUME_OBJECT
  float d_near = texelFetch(vol_obj_near_tx, texel, 0).r;
  float d_far = texelFetch(vol_obj_far_tx, texel, 0).r;
  if ((d_near >= 1.0f && d_far <= 0.0f) || (d_near < 1.0f && d_far > 0.0f && d_near > d_far)) {
    out_dummy = float4(0.0f);
    return;
  }
  dist_entry = (d_near < 1.0f) ? -drw_depth_screen_to_view(d_near) : goo_vol_buf.depth_params.x;
  dist_exit = (d_far > 0.0f) ? -drw_depth_screen_to_view(d_far) : goo_vol_buf.depth_params.y;
  dist_entry = max(dist_entry, goo_vol_buf.depth_params.x);
  dist_exit = min(dist_exit, goo_vol_buf.depth_params.y);
  if (dist_exit <= dist_entry) {
    out_dummy = float4(0.0f);
    return;
  }
#endif

  [[resource_table]] const UtilityTexture util_tx = resource_table_get(UtilityTexture);
  float3 noise = util_tx.fetch(float2(texel), float(UTIL_BLUE_NOISE_LAYER)).xyz;
  float z_jitter = fract(noise.z + float(sample_index) * 0.61803399f);

  float2 inv_xy = goo_vol_buf.inv_froxel_size.xy;
  float inv_d = goo_vol_buf.inv_froxel_size.z;
  float2 coord_scale = max(goo_vol_buf.coord_params.xy, float2(1e-6f));
  float2 uv_screen = ((float2(texel) + 0.5f) * inv_xy) / coord_scale;

  /* Zero unused globals once before the froxel slice loop. */
  g_data.curve_T = float3(0.0f);
  g_data.curve_B = float3(0.0f);
  g_data.curve_N = float3(0.0f);
  g_data.barycentric_coords = float2(0.0f);
  g_data.barycentric_dists = float3(0.0f);
  g_data.hair_diameter = 0.0f;
  g_data.hair_strand_id = 0;
  g_data.ray_depth = 0.0f;
  g_data.ray_length = 0.0f;
  g_data.ray_type = uchar(0);
  g_data.is_strand = false;

  for (int z = 0; z < froxel_size.z; z++) {
    float dist0 = goo_vol_z_to_dist(float(z) * inv_d);
    float dist1 = goo_vol_z_to_dist(float(z + 1) * inv_d);

    float ov_start = dist0;
    float ov_end = dist1;
    float overlap = 1.0f;
#ifdef MAT_GEOM_VOLUME_OBJECT
    ov_start = max(dist0, dist_entry);
    ov_end = min(dist1, dist_exit);
    overlap = max(ov_end - ov_start, 0.0f) / max(dist1 - dist0, 1e-6f);
    if (overlap <= 1e-5f) {
      continue;
    }
#endif

    float z_t = fract(z_jitter + float(z) * 0.38196601f);
    float sample_dist = mix(ov_start, ov_end, z_t);
    float3 vP = goo_vol_ray_view_pos(uv_screen, sample_dist);
    float3 wP = drw_point_view_to_world(vP);

    g_data.P = wP;
    float3 V = drw_world_incident_vector(wP);
    g_data.N = V;
    g_data.Ng = V;
    g_data.Ni = V;

    VolumePoint vp;
#ifdef MAT_GEOM_VOLUME_OBJECT
    vp.lP = (goo_vol_buf.world_to_obj * float4(wP, 1.0f)).xyz;
    vp.orco_default = vp.lP * goo_vol_buf.orco_mul.xyz + goo_vol_buf.orco_add.xyz;
#else
    vp.lP = wP;
    vp.orco_default = wP;
#endif

    attrib_load(vp);
    nodetree_volume();

    float3 scat = g_goo_vol_scattering * overlap;
    float3 abs_c = g_goo_vol_absorption * overlap;
    float3 emit = g_goo_vol_emission * overlap;
    float aniso_acc = g_goo_vol_anisotropy * overlap;
    float aniso_w = g_goo_vol_aniso_weight * overlap;

    if (any(greaterThan(scat, float3(0.0f))) || any(greaterThan(abs_c, float3(0.0f))) ||
        any(greaterThan(emit, float3(0.0f))))
    {
      int3 coord = int3(texel, z);
      float4 prev_s = imageLoadFast(vol_scatter_img, coord);
      float4 prev_e = imageLoadFast(vol_extinct_img, coord);
      float4 prev_m = imageLoadFast(vol_emissive_img, coord);

      imageStoreFast(vol_scatter_img, coord, prev_s + float4(scat, aniso_acc));
      imageStoreFast(vol_extinct_img, coord, prev_e + float4(scat + abs_c, aniso_w));
      imageStoreFast(vol_emissive_img, coord, float4(prev_m.rgb + emit, 1.0f));
    }
  }

  out_dummy = float4(0.0f);
}
