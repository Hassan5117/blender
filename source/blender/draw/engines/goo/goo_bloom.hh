/* SPDX-FileCopyrightText: 2016 Blender Authors
 * SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Bloom, ported from Goo Engine 4.4 `eevee_bloom.cc` (EEVEE Legacy) onto the modern draw manager.
 *
 * Legacy -> modern mapping used here (the pattern the rest of the port will follow):
 *   DRW_pass_create + DRW_shgroup_create      -> PassSimple::init() + shader_set()
 *   DRW_shgroup_uniform_texture_ref(&tex)     -> bind_texture(name, &tex)   (dereferenced at submit)
 *   DRW_shgroup_uniform_vec2(ptr)             -> push_constant(name, ptr)   (dereferenced at submit)
 *   DRW_shgroup_call(grp, fullscreen_quad)    -> draw_procedural(GPU_PRIM_TRIS, 1, 3)
 *   DRW_texture_pool_query_2d_ex              -> TextureFromPool::acquire_2d() / release()
 *   GPU_framebuffer_ensure_config             -> draw::Framebuffer::ensure()
 *   GPU_framebuffer_bind + DRW_draw_pass      -> fb.bind() + manager.submit(pass, view)
 */

#pragma once

#include "BLI_math_vector_types.hh"

#include "DRW_gpu_wrapper.hh"
#include "draw_manager.hh"
#include "draw_pass.hh"

namespace blender {
struct Scene;
}

namespace blender::goo {

using namespace draw;

class Bloom {
 public:
  static constexpr int MAX_BLOOM_STEP = 16;

 private:
  bool enabled_ = false;

  /* Parameters (same meaning as `EEVEE_EffectsInfo` in Goo 4.4). */
  int iteration_len_ = 0;
  float sample_scale_ = 0.0f;
  float4 curve_threshold_ = float4(0.0f);
  float clamp_ = 0.0f;
  float3 color_ = float3(0.0f);

  int2 blit_size_ = int2(0);
  float2 source_texel_size_ = float2(0.0f);
  float2 blit_texel_size_ = float2(0.0f);
  int2 downsample_size_[MAX_BLOOM_STEP];
  float2 downsample_texel_size_[MAX_BLOOM_STEP];

  /* Per-step dynamic inputs, referenced by the passes and updated between submissions
   * (equivalent of Goo's `unf_source_buffer` / `unf_base_buffer` / `unf_source_texel_size`). */
  gpu::Texture *unf_source_tx_ = nullptr;
  gpu::Texture *unf_base_tx_ = nullptr;
  float2 unf_source_texel_size_ = float2(0.0f);

  TextureFromPool blit_tx_ = {"goo.bloom.blit"};
  TextureFromPool downsample_tx_[MAX_BLOOM_STEP];
  TextureFromPool upsample_tx_[MAX_BLOOM_STEP - 1];

  Framebuffer blit_fb_ = {"goo.bloom.blit"};
  Framebuffer down_fb_[MAX_BLOOM_STEP];
  Framebuffer accum_fb_[MAX_BLOOM_STEP - 1];

  PassSimple blit_ps_ = {"Bloom Blit"};
  PassSimple downsample_first_ps_ = {"Bloom Downsample First"};
  PassSimple downsample_ps_ = {"Bloom Downsample"};
  PassSimple upsample_ps_ = {"Bloom Upsample"};
  PassSimple resolve_ps_ = {"Bloom Resolve"};

 public:
  /** Read settings from the (evaluated) scene. Equivalent of `EEVEE_bloom_init`. */
  void init(const Scene *scene, int2 extent);
  /** Build passes. Equivalent of `EEVEE_bloom_cache_init`. */
  void sync();
  /**
   * Run the bloom chain on `source_tx` and write `source + bloom` into `target_fb`.
   * Equivalent of `EEVEE_bloom_draw`.
   */
  void render(Manager &manager, View &view, gpu::Texture *source_tx, gpu::FrameBuffer *target_fb);

  bool enabled() const
  {
    return enabled_;
  }

 private:
  void pass_setup(PassSimple &ps, gpu::Shader *sh, bool upsample, bool resolve, bool add_base);
};

}  // namespace blender::goo
