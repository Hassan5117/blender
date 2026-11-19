/* SPDX-FileCopyrightText: 2016 Blender Authors
 * SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include <algorithm>
#include <cmath>

#include "DNA_scene_types.h"

#include "GPU_framebuffer.hh"
#include "GPU_texture.hh"

#include "goo_bloom.hh"
#include "goo_shader.hh"

namespace blender::goo {

static const bool use_antiflicker = true;
static const bool use_highres = true;

/* Same format as Goo 4.4 (GPU_R11F_G11F_B10F). */
static constexpr gpu::TextureFormat bloom_format = gpu::TextureFormat::UFLOAT_11_11_10;
static constexpr eGPUTextureUsage bloom_usage = GPU_TEXTURE_USAGE_SHADER_READ |
                                                GPU_TEXTURE_USAGE_ATTACHMENT;

void Bloom::init(const Scene *scene, int2 extent)
{
  enabled_ = scene && (scene->eevee.flag & SCE_EEVEE_BLOOM_ENABLED) && extent.x > 0 &&
             extent.y > 0;
  if (!enabled_) {
    return;
  }

  /* Blit buffer. */
  source_texel_size_ = 1.0f / float2(extent);
  blit_size_ = extent;
  blit_texel_size_ = 1.0f / float2(blit_size_);

  /* Parameters. */
  const float threshold = scene->eevee.bloom_threshold;
  const float knee = scene->eevee.bloom_knee;
  const float intensity = scene->eevee.bloom_intensity;
  const float radius = scene->eevee.bloom_radius;
  clamp_ = scene->eevee.bloom_clamp;

  /* Determine the iteration count. */
  const float min_dim = float(std::min(blit_size_.x, blit_size_.y));
  const float max_iter = (radius - 8.0f) + std::log(min_dim) / std::log(2.0f);
  const int max_iter_int = int(max_iter);
  iteration_len_ = std::clamp(max_iter_int, 1, MAX_BLOOM_STEP);

  sample_scale_ = 0.5f + max_iter - float(max_iter_int);
  curve_threshold_ = float4(threshold - knee, knee * 2.0f, 0.25f / std::max(1e-5f, knee), threshold);
  color_ = float3(scene->eevee.bloom_color) * intensity;

  /* Downsample sizes. */
  int2 texsize = blit_size_;
  for (int i = 0; i < iteration_len_; i++) {
    texsize = math::max(texsize / 2, int2(2));
    downsample_size_[i] = texsize;
    downsample_texel_size_[i] = 1.0f / float2(texsize);
  }
}

void Bloom::pass_setup(PassSimple &ps, gpu::Shader *sh, bool upsample, bool resolve, bool add_base)
{
  ps.init();
  ps.state_set(DRW_STATE_WRITE_COLOR);
  ps.shader_set(sh);
  ps.bind_texture("sourceBuffer", &unf_source_tx_);
  ps.push_constant("sourceBufferTexelSize", &unf_source_texel_size_);
  /* Bind defaults for uniforms unused by some steps (keeps Metal argument tables valid). */
  ps.push_constant("curveThreshold", curve_threshold_);
  ps.push_constant("clampIntensity", clamp_);
  ps.push_constant("sampleScale", sample_scale_);
  ps.push_constant("bloomColor", color_);
  ps.push_constant("bloomAddBase", add_base);
  if (upsample || resolve) {
    ps.bind_texture("baseBuffer", &unf_base_tx_);
  }
  else {
    /* Never sampled in blit/downsample, but must be bound. */
    ps.bind_texture("baseBuffer", &unf_source_tx_);
  }
  ps.draw_procedural(GPU_PRIM_TRIS, 1, 3);
}

void Bloom::sync()
{
  if (!enabled_) {
    return;
  }
  ShaderModule &sh = ShaderModule::get();
  /* Bloom algorithm (unchanged from Goo 4.4 / EEVEE Legacy):
   * blit (bright extract) -> downsample chain -> upsample+accumulate chain -> resolve onto source.
   */
  pass_setup(downsample_first_ps_,
             use_antiflicker ? sh.bloom_downsample_hq.get() : sh.bloom_downsample.get(),
             false,
             false,
             false);
  pass_setup(downsample_ps_, sh.bloom_downsample.get(), false, false, false);
  pass_setup(upsample_ps_, sh.bloom_upsample_hq.get(), true, false, false);
  pass_setup(blit_ps_, sh.bloom_blit_hq.get(), false, false, false);
  pass_setup(resolve_ps_, sh.bloom_resolve_hq.get(), true, true, true);
  UNUSED_VARS(use_highres);
}

void Bloom::render(Manager &manager,
                   View &view,
                   gpu::Texture *source_tx,
                   gpu::FrameBuffer *target_fb)
{
  if (!enabled_) {
    return;
  }

  /* Acquire transient textures for this frame. */
  blit_tx_.acquire_2d(blit_size_, bloom_format, bloom_usage);
  blit_fb_.ensure(GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(blit_tx_));
  for (int i = 0; i < iteration_len_; i++) {
    downsample_tx_[i].acquire_2d(downsample_size_[i], bloom_format, bloom_usage);
    down_fb_[i].ensure(GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(downsample_tx_[i]));
  }
  for (int i = 0; i < iteration_len_ - 1; i++) {
    upsample_tx_[i].acquire_2d(downsample_size_[i], bloom_format, bloom_usage);
    accum_fb_[i].ensure(GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(upsample_tx_[i]));
  }

  /* Extract bright pixels. */
  unf_source_texel_size_ = source_texel_size_;
  unf_source_tx_ = source_tx;
  blit_fb_.bind();
  manager.submit(blit_ps_, view);

  /* Downsample. */
  unf_source_texel_size_ = blit_texel_size_;
  unf_source_tx_ = blit_tx_;
  down_fb_[0].bind();
  manager.submit(downsample_first_ps_, view);

  gpu::Texture *last = downsample_tx_[0];
  for (int i = 1; i < iteration_len_; i++) {
    unf_source_texel_size_ = downsample_texel_size_[i - 1];
    unf_source_tx_ = last;
    down_fb_[i].bind();
    manager.submit(downsample_ps_, view);
    last = downsample_tx_[i];
  }

  /* Upsample and accumulate. */
  for (int i = iteration_len_ - 2; i >= 0; i--) {
    unf_source_texel_size_ = downsample_texel_size_[i];
    unf_source_tx_ = last;
    unf_base_tx_ = downsample_tx_[i];
    accum_fb_[i].bind();
    manager.submit(upsample_ps_, view);
    last = upsample_tx_[i];
  }

  /* Resolve. */
  unf_source_texel_size_ = downsample_texel_size_[0];
  unf_source_tx_ = last;
  unf_base_tx_ = source_tx;
  GPU_framebuffer_bind(target_fb);
  manager.submit(resolve_ps_, view);

  /* Release transient textures back to the pool. */
  blit_tx_.release();
  for (int i = 0; i < iteration_len_; i++) {
    downsample_tx_[i].release();
  }
  for (int i = 0; i < iteration_len_ - 1; i++) {
    upsample_tx_[i].release();
  }
}

}  // namespace blender::goo
