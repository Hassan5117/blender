/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_shadow_infos.hh"

VERTEX_SHADER_CREATE_INFO(goo_shadow_id_curves)

#include "draw_curves_lib.glsl"
#include "draw_model_lib.glsl"
#include "draw_view_lib.glsl"

#if defined(GPU_NVIDIA) && defined(GPU_OPENGL)
#  define const
#endif

void main()
{
  drw_ResourceID_iface.resource_id = drw_resource_id_raw();
  const curves::Point ls_pt = curves::point_get(uint(gl_VertexID));
  const curves::Point ws_pt = curves::object_to_world(ls_pt, drw_modelmat());
  const float3 V = drw_world_incident_vector(ws_pt.P);
  /* Bias along view ray to match EEVEE curves shadow bias and avoid self-shadowing. */
  const float3 world_pos = curves::shape_point_get(ws_pt, V).P - V * ws_pt.radius;
  gl_Position = drw_point_world_to_homogenous(world_pos);
}
