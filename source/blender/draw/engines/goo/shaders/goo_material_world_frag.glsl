/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_material_infos.hh"

FRAGMENT_SHADER_CREATE_INFO(goo_material_world)

#include "draw_shader_shared.hh"
#include "draw_view_lib.glsl"
#include "goo_attributes_world_lib.glsl"
#include "goo_geom_types_lib.glsl"
#include "goo_nodetree_lib.glsl"
#include "goo_nodetree_frag_lib.glsl"

void main()
{
  g_data.N = normalize(drw_normal_view_to_world(drw_view_incident_vector(interp.P)));
  g_data.Ng = g_data.N;
  g_data.Ni = g_data.N;
  g_data.P = -g_data.N;
  g_data.is_strand = false;
  g_data.hair_diameter = 0.0f;
  g_data.hair_strand_id = 0;
  g_data.ray_type = ((goo_probe_buf.capture_flags & GOO_CAPTURE_WORLD) != 0) ? uchar(1) : uchar(0);
  g_data.ray_depth = 0.0f;
  g_data.ray_length = 0.0f;
  g_data.barycentric_coords = float2(0.0f);
  g_data.barycentric_dists = float3(0.0f);

  WorldPoint wp;
  wp.lP = g_data.P;
  attrib_load(wp);

  nodetree_surface(0.0f);

  float3 world_rad = g_goo_radiance;
  float world_alpha = 1.0f;
#ifdef GOO_INDIRECT_READ
  if ((goo_probe_buf.capture_flags & GOO_CAPTURE_WORLD) == 0 &&
      goo_probe_buf.lookdev_params.z > 0.5f)
  {
    if (goo_probe_buf.lookdev_params.y > 0.01f && goo_probe_buf.lookdev_params.w > 0.5f) {
      world_rad = goo_sample_env_probe(0, g_data.P, goo_probe_buf.lookdev_params.y);
    }
    world_alpha = saturate(goo_probe_buf.lookdev_params.x);
    world_rad *= world_alpha;
  }
#endif

#ifdef GOO_PASS_WRITE
  if (goo_pass_buf.env_id >= 0 && (goo_probe_buf.capture_flags & GOO_CAPTURE_NO_PASS_WRITE) == 0) {
    imageStoreFast(goo_rp_color_img,
                   int3(int2(gl_FragCoord.xy), goo_pass_buf.env_id),
                   float4(g_goo_radiance, 1.0f));
  }
#endif

  out_color = float4(world_rad, world_alpha);
}
