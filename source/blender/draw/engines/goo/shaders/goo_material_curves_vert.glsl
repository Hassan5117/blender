/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_material_infos.hh"

VERTEX_SHADER_CREATE_INFO(goo_material_surface_curves)

#include "draw_curves_lib.glsl"
#include "draw_model_lib.glsl"
#include "draw_object_infos_lib.glsl"
#include "draw_view_lib.glsl"
#include "goo_attributes_curves_lib.glsl"
#include "goo_object_lib.glsl"

#include "goo_nodetree_vert_lib.glsl"

#if defined(GPU_NVIDIA) && defined(GPU_OPENGL)
#  define const
#endif

void main()
{
  drw_ResourceID_iface.resource_id = drw_resource_id_raw();

  const curves::Point ls_pt = curves::point_get(uint(gl_VertexID));
  const curves::Point ws_pt = curves::object_to_world(ls_pt, drw_modelmat());
  const float3 V = drw_world_incident_vector(ws_pt.P);
  const curves::ShapePoint pt = curves::shape_point_get(ws_pt, V);

  interp.P = pt.P;
  interp.N = pt.curve_N;

  curve_interp.binormal = pt.curve_B;
  curve_interp.tangent = pt.curve_T;
  curve_interp.radius = ws_pt.radius;
  curve_interp.time_width = ws_pt.azimuthal_offset * ws_pt.radius;
  curve_interp.point_id = float(ws_pt.point_id);
  curve_interp_flat.strand_id = ws_pt.curve_id;

  ObjectInfos ob_infos = drw_object_infos();
  float3 lP_root = curves::get_curve_root_pos(ws_pt.point_id, ws_pt.curve_segment);
  float3 lP_orco = lP_root * ob_infos.orco_mul + ob_infos.orco_add;

  g_data.P = interp.P;
  g_data.N = interp.N;
  g_data.Ni = interp.N;
  g_data.Ng = interp.N;
  g_data.is_strand = true;
  g_data.hair_diameter = ws_pt.radius * 2.0f;
  g_data.hair_strand_id = ws_pt.curve_id;
  g_data.curve_T = -pt.curve_T;
  g_data.curve_B = pt.curve_B;
  g_data.curve_N = pt.curve_N;

  CurvesPoint cvert;
  cvert.curve_id = ws_pt.curve_id;
  cvert.point_id = ws_pt.point_id;
  cvert.curve_segment = ws_pt.curve_segment;
  cvert.orco_default = lP_orco;
  attrib_load(cvert);

  interp.P += nodetree_displacement();

#ifdef MAT_SHADOW
  interp.P -= V * ws_pt.radius;
#endif

  gl_Position = drw_point_world_to_homogenous(interp.P);
#ifdef USE_WORLD_CLIP_PLANES
  gl_ClipDistance[0] = ((goo_probe_buf.capture_flags & GOO_CAPTURE_CLIP_PLANE) != 0) ?
                           dot(goo_probe_buf.capture_clip_plane, float4(interp.P, 1.0f)) :
                           1.0f;
#endif
}
