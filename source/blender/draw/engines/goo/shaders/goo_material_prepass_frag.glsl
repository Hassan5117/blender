/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_material_infos.hh"

FRAGMENT_SHADER_CREATE_INFO(goo_material_prepass_mesh)

#include "draw_view_lib.glsl"
#include "goo_object_lib.glsl"
#include "goo_nodetree_lib.glsl"

#include "goo_nodetree_frag_lib.glsl"

void main()
{
  g_data.P = interp.P;
  g_data.Ni = interp.N;
  g_data.N = safe_normalize(interp.N);
  g_data.Ng = safe_normalize(cross(gpu_dfdx(interp.P), gpu_dfdy(interp.P)));
  g_data.is_strand = false;
  g_data.hair_diameter = 0.0f;
  g_data.hair_strand_id = 0;
  g_data.ray_type = uchar(0); /* RAY_TYPE_CAMERA */
  g_data.ray_depth = 0.0f;
  g_data.ray_length = distance(g_data.P, drw_view_position());
  g_data.barycentric_coords = float2(0.0f);
  g_data.barycentric_dists = float3(0.0f);
  if (!gl_FrontFacing) {
    g_data.N = -g_data.N;
    g_data.Ni = -g_data.Ni;
  }
  if (dot(g_data.Ng, g_data.N) < 0.0f) {
    g_data.Ng = -g_data.Ng;
  }

  g_goo_frag_depth = gl_FragCoord.z;
  g_goo_frag_depth_set = false;

  nodetree_surface(0.5f);

  float alpha = saturate(1.0f - reduce_add(g_goo_transmittance) * (1.0f / 3.0f));
  if ((goo_material_flags & GOO_MAT_FLAG_ALPHA_CLIP) != 0) {
    if (alpha <= goo_alpha_threshold) {
      discard;
    }
  }
  else if ((goo_material_flags & GOO_MAT_FLAG_ALPHA_HASHED) != 0) {
    if (alpha < goo_hashed_alpha_threshold(g_data.P)) {
      discard;
    }
  }

#ifdef GOO_USE_SET_DEPTH
  if (g_goo_frag_depth_set) {
    gl_FragDepth = g_goo_frag_depth;
  }
#endif
}
