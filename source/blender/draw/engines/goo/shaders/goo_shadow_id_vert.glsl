/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_shadow_infos.hh"

VERTEX_SHADER_CREATE_INFO(goo_shadow_id_mesh)

#include "draw_model_lib.glsl"
#include "draw_view_lib.glsl"

void main()
{
  drw_ResourceID_iface.resource_id = drw_resource_id_raw();
  float3 world_pos = drw_point_object_to_world(pos);
  gl_Position = drw_point_world_to_homogenous(world_pos);
}
