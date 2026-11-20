/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_material_infos.hh"

VERTEX_SHADER_CREATE_INFO(goo_material_world)

#include "draw_view_lib.glsl"

void main()
{
  /* Full-screen triangle at the far plane. */
  int v = gl_VertexID % 3;
  float x = float((v & 1) << 2) - 1.0f;
  float y = float((v & 2) << 1) - 1.0f;
  gl_Position = float4(x, y, 1.0f, 1.0f);

  interp.P = drw_point_ndc_to_view(float3(x, y, 1.0f));
  interp.N = float3(1.0f);
}
