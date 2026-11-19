/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Static shader storage. Shaders are shared between all engine instances and freed on exit.
 */

#pragma once

#include "GPU_shader.hh"

namespace blender::goo {

class ShaderModule {
 public:
  gpu::StaticShader surface_flat = {"goo_surface_flat"};

  /* Bloom (ported from Goo 4.4 `eevee_legacy_bloom_*`). */
  gpu::StaticShader bloom_blit_hq = {"goo_bloom_blit_hq"};
  gpu::StaticShader bloom_downsample = {"goo_bloom_downsample"};
  gpu::StaticShader bloom_downsample_hq = {"goo_bloom_downsample_hq"};
  gpu::StaticShader bloom_upsample_hq = {"goo_bloom_upsample_hq"};
  gpu::StaticShader bloom_resolve_hq = {"goo_bloom_resolve_hq"};

  static ShaderModule &get();
  static void module_free();
};

}  // namespace blender::goo
