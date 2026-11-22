/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_effects_infos.hh"

VERTEX_SHADER_CREATE_INFO(goo_velocity_curves)

#include "draw_curves_lib.glsl"
#include "draw_model_lib.glsl"
#include "draw_object_infos_lib.glsl"
#include "draw_view_lib.glsl"

#if defined(GPU_NVIDIA) && defined(GPU_OPENGL)
#  define const
#endif

void main()
{
  drw_ResourceID_iface.resource_id = drw_resource_id_raw();
  uint rid = min(drw_resource_id(), uint(GOO_MAX_VELOCITY_OBJECTS - 1));
  GooVelocityObject vo = goo_vel_buf[rid];

  const curves::Point ls_pt = curves::point_get(uint(gl_VertexID));
  const curves::Point ws_pt = curves::object_to_world(ls_pt, drw_modelmat());
  const float3 V = drw_world_incident_vector(ws_pt.P);
  const float3 curr_ws = curves::shape_point_get(ws_pt, V).P;

  /* Transform curve shape offset back to object space approximately via drw_modelinv,
   * then re-transform with prev/next obmats. */
  float3 local_p = (drw_modelinv() * float4(curr_ws, 1.0f)).xyz;
  float3 prev_ws = (vo.prev_obmat * float4(local_p, 1.0f)).xyz;
  float3 next_ws = (vo.next_obmat * float4(local_p, 1.0f)).xyz;

  vel_interp.curr_clip = goo_mblur_buf.curr_persmat * float4(curr_ws, 1.0f);
  vel_interp.prev_clip = goo_mblur_buf.prev_persmat * float4(prev_ws, 1.0f);
  vel_interp.next_clip = goo_mblur_buf.next_persmat * float4(next_ws, 1.0f);
  gl_Position = vel_interp.curr_clip;
}
