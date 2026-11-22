/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "gpu_shader_utildefines_lib.glsl"

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
  float vis = 1.0f - holdout;
  float alpha = saturate(1.0f - reduce_add(g_goo_transmittance) * (1.0f / 3.0f)) * vis;
  float3 final_radiance = g_goo_radiance * vis;

#ifdef GOO_SSS_WRITE
  if ((goo_probe_buf.capture_flags & GOO_CAPTURE_NO_SSR) == 0 &&
      (goo_material_flags & (GOO_MAT_FLAG_NO_PASS_WRITE | GOO_MAT_FLAG_IS_REFRACT_PASS)) == 0 &&
      goo_probe_buf.sss_params.z > 0.5f)
  {
    int2 sss_texel = int2(gl_FragCoord.xy);
    if (g_goo_sss_weight > 1e-6f && vis > 1e-4f) {
      final_radiance = max(final_radiance - g_goo_sss_direct_radiance * vis, float3(0.0f));
      float inv_w = 1.0f / max(g_goo_sss_weight, 1e-5f);
      float3 sss_alb = clamp(g_goo_sss_albedo * inv_w, float3(0.0f), float3(10.0f));
      float3 sss_rad = max(g_goo_sss_radius * inv_w, float3(0.0f));
      float obj_id = float((drw_resource_id() & 1023u) + 1u);
      imageStoreFast(
          goo_sss_data_img, int3(sss_texel, 0), float4(g_goo_sss_irrad * vis, 1.0f));
      imageStoreFast(goo_sss_data_img, int3(sss_texel, 1), float4(sss_alb, obj_id));
      imageStoreFast(goo_sss_data_img, int3(sss_texel, 2), float4(sss_rad, 1.0f));
    }
    else {
      imageStoreFast(goo_sss_data_img, int3(sss_texel, 0), float4(0.0f));
    }
  }
#endif

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

#ifdef GOO_SSR_WRITE
  if ((goo_probe_buf.ssr_flags & GOO_SSR_FLAG_ENABLED) != 0 &&
      (goo_probe_buf.capture_flags & GOO_CAPTURE_NO_SSR) == 0 &&
      (goo_material_flags & (GOO_MAT_FLAG_NO_PASS_WRITE | GOO_MAT_FLAG_IS_REFRACT_PASS)) == 0)
  {
    int2 texel = int2(gl_FragCoord.xy);
    float vis = 1.0f - holdout;
    if (g_goo_ssr_weight > 1e-6f && g_goo_ssr_mask > 1e-4f && vis > 1e-4f) {
      imageStoreFast(
          goo_ssr_data_img, texel, float4(g_goo_ssr_normal, saturate(g_goo_ssr_roughness)));
      imageStoreFast(
          goo_ssr_spec_img, texel, float4(g_goo_ssr_throughput * vis, saturate(g_goo_ssr_mask)));
      imageStoreFast(goo_ssr_fallback_img, texel, float4(g_goo_ssr_fallback * vis, 1.0f));
    }
    else {
      imageStoreFast(goo_ssr_data_img, texel, float4(0.0f, 0.0f, 0.0f, -1.0f));
      imageStoreFast(goo_ssr_spec_img, texel, float4(0.0f));
      imageStoreFast(goo_ssr_fallback_img, texel, float4(0.0f));
    }
  }
#endif

#ifdef GOO_USE_SET_DEPTH
  if (g_goo_frag_depth_set) {
    gl_FragDepth = g_goo_frag_depth;
  }
#endif

  return color_out;
}
