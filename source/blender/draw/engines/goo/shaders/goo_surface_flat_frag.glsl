/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_surface_infos.hh"

FRAGMENT_SHADER_CREATE_INFO(goo_surface_flat)

#include "draw_view_lib.glsl"

void main()
{
  float3 N = normalize(normal_ws);
  /* Headlight: light comes from the camera. */
  float3 V = drw_world_incident_vector(position_ws);
  float lambert = max(0.0f, dot(N, V));
  /* Simple 2-band toon ramp as a nod to Goo's NPR look. */
  float band = (lambert > 0.5f) ? 1.0f : 0.45f;
  out_color = float4(base_color.rgb * band + world_color.rgb * 0.2f, 1.0f);
}
