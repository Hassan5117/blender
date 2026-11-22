/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Static shader storage. Shaders are shared between all engine instances and freed on exit.
 */

#pragma once

#include "DRW_gpu_wrapper.hh"
#include "GPU_shader.hh"

namespace blender::goo {

class UtilityTexture : public draw::Texture {
 public:
  UtilityTexture();
  ~UtilityTexture() = default;
};

class ShaderModule {
 public:
  gpu::StaticShader surface_flat = {"goo_surface_flat"};
  gpu::StaticShader shadow_mesh = {"goo_shadow_mesh"};
  gpu::StaticShader shadow_id_mesh = {"goo_shadow_id_mesh"};
  gpu::StaticShader shadow_curves = {"goo_shadow_curves"};
  gpu::StaticShader shadow_id_curves = {"goo_shadow_id_curves"};

  /* Bloom (ported from Goo 4.4 `eevee_legacy_bloom_*`). */
  gpu::StaticShader bloom_blit_hq = {"goo_bloom_blit_hq"};
  gpu::StaticShader bloom_downsample = {"goo_bloom_downsample"};
  gpu::StaticShader bloom_downsample_hq = {"goo_bloom_downsample_hq"};
  gpu::StaticShader bloom_upsample_hq = {"goo_bloom_upsample_hq"};
  gpu::StaticShader bloom_resolve_hq = {"goo_bloom_resolve_hq"};

  /* GTAO, TAA, Film Resolve/Overscan, and Render Passes / Cryptomatte. */
  gpu::StaticShader effect_gtao = {"goo_effect_gtao"};
  gpu::StaticShader taa_accum = {"goo_taa_accum"};
  gpu::StaticShader taa_blend = {"goo_taa_blend"};
  gpu::StaticShader taa_reproject = {"goo_taa_reproject"};
  gpu::StaticShader film_resolve = {"goo_film_resolve"};
  gpu::StaticShader film_crop_depth = {"goo_film_crop_depth"};
  gpu::StaticShader pass_accum = {"goo_pass_accum"};
  gpu::StaticShader cryptomatte_sort = {"goo_cryptomatte_sort"};
  gpu::StaticShader pass_display = {"goo_pass_display"};

  /* Indirect IBL, Light Probes, Color/Hi-Z Pyramids, and SSR. */
  gpu::StaticShader env_remap = {"goo_env_remap"};
  gpu::StaticShader env_convolve = {"goo_env_convolve"};
  gpu::StaticShader cube_to_sh = {"goo_cube_to_sh"};
  gpu::StaticShader color_copy = {"goo_color_copy"};
  gpu::StaticShader color_downsample = {"goo_color_downsample"};
  gpu::StaticShader color_downsample_array = {"goo_color_downsample_array"};
  gpu::StaticShader hiz_copy = {"goo_hiz_copy"};
  gpu::StaticShader hiz_downsample = {"goo_hiz_downsample"};
  gpu::StaticShader ssr_trace = {"goo_ssr_trace"};
  gpu::StaticShader ssr_resolve = {"goo_ssr_resolve"};

  /* Milestone 11: SSS, Depth of Field, Motion Blur, and Volumetrics. */
  gpu::StaticShader sss_resolve = {"goo_sss_resolve"};
  gpu::StaticShader dof_coc = {"goo_dof_coc"};
  gpu::StaticShader dof_dilate = {"goo_dof_dilate"};
  gpu::StaticShader dof_gather = {"goo_dof_gather"};
  gpu::StaticShader velocity_mesh = {"goo_velocity_mesh"};
  gpu::StaticShader velocity_curves = {"goo_velocity_curves"};
  gpu::StaticShader mblur_tile = {"goo_mblur_tile"};
  gpu::StaticShader mblur_dilate = {"goo_mblur_dilate"};
  gpu::StaticShader mblur_gather = {"goo_mblur_gather"};
  gpu::StaticShader volume_clear = {"goo_volume_clear"};
  gpu::StaticShader volume_scatter = {"goo_volume_scatter"};
  gpu::StaticShader volume_integrate = {"goo_volume_integrate"};
  gpu::StaticShader volume_resolve = {"goo_volume_resolve"};

  UtilityTexture utility_tx;

  static ShaderModule &get();
  static void module_free();
};

}  // namespace blender::goo
