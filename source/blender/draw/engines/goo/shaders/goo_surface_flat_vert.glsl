/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_surface_infos.hh"

VERTEX_SHADER_CREATE_INFO(goo_surface_flat)

#include "draw_model_lib.glsl"
#include "draw_view_lib.glsl"

void main()
{
  float3 world_pos = drw_point_object_to_world(pos);
  normal_ws = drw_normal_object_to_world(nor);
  position_ws = world_pos;
  gl_Position = drw_point_world_to_homogenous(world_pos);
}
