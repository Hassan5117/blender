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
  g_data.ray_type = uchar(0); /* RAY_TYPE_CAMERA */
  g_data.ray_depth = 0.0f;
  g_data.ray_length = 0.0f;
  g_data.barycentric_coords = float2(0.0f);
  g_data.barycentric_dists = float3(0.0f);

  WorldPoint wp;
  wp.lP = g_data.P;
  attrib_load(wp);

  nodetree_surface(0.0f);

  out_color = float4(g_goo_radiance, 1.0f);
}
