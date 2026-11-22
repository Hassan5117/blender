/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_effects_infos.hh"

VERTEX_SHADER_CREATE_INFO(goo_velocity_mesh)

#include "draw_model_lib.glsl"
#include "draw_object_infos_lib.glsl"
#include "draw_view_lib.glsl"

void main()
{
  drw_ResourceID_iface.resource_id = drw_resource_id_raw();
  uint rid = min(drw_resource_id(), uint(GOO_MAX_VELOCITY_OBJECTS - 1));
  GooVelocityObject vo = goo_vel_buf[rid];

  float3 curr_ws = drw_point_object_to_world(pos);
  float3 prev_ws = (vo.prev_obmat * float4(pos, 1.0f)).xyz;
  float3 next_ws = (vo.next_obmat * float4(pos, 1.0f)).xyz;

  vel_interp.curr_clip = drw_point_world_to_homogenous(curr_ws);
  vel_interp.prev_clip = goo_mblur_buf.prev_persmat * float4(prev_ws, 1.0f);
  vel_interp.next_clip = goo_mblur_buf.next_persmat * float4(next_ws, 1.0f);
  gl_Position = vel_interp.curr_clip;
}
