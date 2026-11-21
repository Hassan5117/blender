/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_material_infos.hh"

VERTEX_SHADER_CREATE_INFO(goo_material_surface_mesh)

#include "draw_model_lib.glsl"
#include "draw_object_infos_lib.glsl"
#include "draw_view_lib.glsl"
#include "goo_attributes_mesh_lib.glsl"
#include "goo_object_lib.glsl"

#include "goo_nodetree_vert_lib.glsl"

void main()
{
  drw_ResourceID_iface.resource_id = drw_resource_id_raw();
  interp.P = drw_point_object_to_world(pos);
  interp.N = normalize(drw_normal_object_to_world(nor));

  ObjectInfos ob_infos = drw_object_infos();
  /* Original coordinates (ORCO). */
  float3 lP_orco = pos * ob_infos.orco_mul + ob_infos.orco_add;

  g_data.P = interp.P;
  g_data.N = interp.N;
  g_data.Ni = interp.N;
  g_data.Ng = interp.N;

  MeshVertex vert;
  vert.lP = pos;
  vert.world_to_object = to_float3x3(drw_modelinv());
  vert.orco_default = lP_orco;
  attrib_load(vert);

  interp.P += nodetree_displacement();

  gl_Position = drw_point_world_to_homogenous(interp.P);
#ifdef USE_WORLD_CLIP_PLANES
  gl_ClipDistance[0] = ((goo_probe_buf.capture_flags & GOO_CAPTURE_CLIP_PLANE) != 0) ?
                           dot(goo_probe_buf.capture_clip_plane, float4(interp.P, 1.0f)) :
                           1.0f;
#endif
}
