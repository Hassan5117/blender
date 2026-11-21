/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_shadow_infos.hh"

VERTEX_SHADER_CREATE_INFO(goo_shadow_curves)

#include "draw_curves_lib.glsl"
#include "draw_model_lib.glsl"
#include "draw_view_lib.glsl"

#if defined(GPU_NVIDIA) && defined(GPU_OPENGL)
#  define const
#endif

void main()
{
  const curves::Point ls_pt = curves::point_get(uint(gl_VertexID));
  const curves::Point ws_pt = curves::object_to_world(ls_pt, drw_modelmat());
  const float3 V = drw_world_incident_vector(ws_pt.P);
  const float3 world_pos = curves::shape_point_get(ws_pt, V).P;
  gl_Position = drw_point_world_to_homogenous(world_pos);
#ifdef USE_WORLD_CLIP_PLANES
  gl_ClipDistance[0] = ((goo_probe_buf.capture_flags & GOO_CAPTURE_CLIP_PLANE) != 0) ?
                           dot(goo_probe_buf.capture_clip_plane, float4(world_pos, 1.0f)) :
                           1.0f;
#endif
}
