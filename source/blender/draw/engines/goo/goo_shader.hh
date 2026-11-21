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

  /* Bloom (ported from Goo 4.4 `eevee_legacy_bloom_*`). */
  gpu::StaticShader bloom_blit_hq = {"goo_bloom_blit_hq"};
  gpu::StaticShader bloom_downsample = {"goo_bloom_downsample"};
  gpu::StaticShader bloom_downsample_hq = {"goo_bloom_downsample_hq"};
  gpu::StaticShader bloom_upsample_hq = {"goo_bloom_upsample_hq"};
  gpu::StaticShader bloom_resolve_hq = {"goo_bloom_resolve_hq"};

  /* GTAO, TAA, and Film Resolve/Overscan. */
  gpu::StaticShader effect_gtao = {"goo_effect_gtao"};
  gpu::StaticShader taa_accum = {"goo_taa_accum"};
  gpu::StaticShader taa_blend = {"goo_taa_blend"};
  gpu::StaticShader taa_reproject = {"goo_taa_reproject"};
  gpu::StaticShader film_resolve = {"goo_film_resolve"};
  gpu::StaticShader film_crop_depth = {"goo_film_crop_depth"};

  UtilityTexture utility_tx;

  static ShaderModule &get();
  static void module_free();
};

}  // namespace blender::goo
