/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/* Shared surface shading + alpha handling + render pass / Cryptomatte writes for meshes and curves. */

float4 goo_surface_frag_exec()
{
  fragment_displacement();

  g_goo_frag_depth = gl_FragCoord.z;
  g_goo_frag_depth_set = false;

  goo_pass_accum_reset();
  nodetree_surface(0.5f);

  eObjectInfoFlag ob_flag = object_infos_get().flag;
  if ((uint(ob_flag) & uint(OBJECT_HOLDOUT)) != 0u) {
    g_holdout = saturate(1.0f - reduce_add(g_goo_transmittance) * (1.0f / 3.0f));
    g_goo_radiance = float3(0.0f);
  }

  float holdout = saturate(g_holdout);
  float alpha = saturate(1.0f - reduce_add(g_goo_transmittance) * (1.0f / 3.0f)) *
                (1.0f - holdout);
  float3 final_radiance = g_goo_radiance * (1.0f - holdout);

  float4 color_out;
  if ((goo_material_flags & GOO_MAT_FLAG_ALPHA_CLIP) != 0) {
    if (alpha <= goo_alpha_threshold) {
      discard;
    }
    color_out = float4(final_radiance / max(alpha, 1e-4f), 1.0f);
  }
  else if ((goo_material_flags & GOO_MAT_FLAG_ALPHA_HASHED) != 0) {
    if (alpha < goo_hashed_alpha_threshold(g_data.P)) {
      discard;
    }
    color_out = float4(final_radiance / max(alpha, 1e-4f), 1.0f);
  }
  else if ((goo_material_flags & GOO_MAT_FLAG_ALPHA_BLEND) != 0) {
    color_out = float4(final_radiance, alpha);
  }
  else {
    color_out = float4(final_radiance, 1.0f - holdout);
  }

  goo_store_surface_passes(int2(gl_FragCoord.xy), holdout, drw_resource_id());

#ifdef GOO_USE_SET_DEPTH
  if (g_goo_frag_depth_set) {
    gl_FragDepth = g_goo_frag_depth;
  }
#endif

  return color_out;
}
