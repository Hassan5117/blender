/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_material_infos.hh"

FRAGMENT_SHADER_CREATE_INFO(goo_material_surface_curves)

#include "draw_view_lib.glsl"
#include "goo_object_lib.glsl"
#include "goo_nodetree_lib.glsl"

#include "goo_nodetree_frag_lib.glsl"
#include "goo_material_surface_lib.glsl"

void goo_init_globals_curves()
{
  g_data.P = interp.P;
  float cos_theta = clamp(curve_interp.time_width / max(curve_interp.radius, 1e-6f), -1.0f, 1.0f);
  float sin_theta = sin_from_cos(cos_theta);
  g_data.N = safe_normalize(interp.N * sin_theta + curve_interp.binormal * cos_theta);
  g_data.Ni = g_data.N;
  g_data.Ng = g_data.N;

  float3 V = drw_world_incident_vector(g_data.P);
  g_data.curve_T = -curve_interp.tangent;
  g_data.curve_B = cross(V, g_data.curve_T);
  g_data.curve_N = safe_normalize(cross(g_data.curve_T, g_data.curve_B));

  g_data.is_strand = true;
  g_data.hair_diameter = curve_interp.radius * 2.0f;
  g_data.hair_strand_id = curve_interp_flat.strand_id;
  g_data.ray_type = uchar(0); /* RAY_TYPE_CAMERA */
  g_data.ray_depth = 0.0f;
  g_data.ray_length = distance(g_data.P, drw_view_position());
  g_data.barycentric_coords = float2(0.0f);
  g_data.barycentric_dists = float3(0.0f);
#ifdef USE_BARYCENTRICS
  g_data.barycentric_coords.y = fract(curve_interp.point_id);
  g_data.barycentric_coords.x = 1.0f - g_data.barycentric_coords.y;
#endif
}

void main()
{
  goo_init_globals_curves();
  out_color = goo_surface_frag_exec();
}
