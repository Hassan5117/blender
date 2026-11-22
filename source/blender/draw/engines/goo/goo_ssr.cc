/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include <algorithm>
#include <cmath>

#include "BLI_math_base.h"

#include "GPU_state.hh"
#include "GPU_texture.hh"

#include "../eevee/eevee_defines.hh"

#include "goo_light.hh"
#include "goo_probe.hh"
#include "goo_shader.hh"
#include "goo_ssr.hh"

namespace blender::goo {

int SsrModule::safe_mip_count(int2 extent, int max_mips)
{
  const int min_dim = std::max(1, std::min(extent.x, extent.y));
  const int avail = int(std::floor(std::log2(double(min_dim)))) + 1;
  return std::clamp(avail, 1, max_mips);
}

void SsrModule::init_buffers(int2 render_extent,
                             bool need_gbuffer,
                             ProbeModule &probes,
                             LightModule &lights)
{
  const eGPUTextureUsage img_usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_SHADER_WRITE;
  if (dummy_ssr_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16, int2(1), img_usage)) {
    dummy_ssr_tx_.clear(float4(0.0f));
  }

  if (need_gbuffer) {
    ssr_data_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16, render_extent, img_usage);
    ssr_spec_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16, render_extent, img_usage);
    ssr_fallback_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16, render_extent, img_usage);
    ssr_out_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16,
                          render_extent,
                          img_usage | GPU_TEXTURE_USAGE_ATTACHMENT);

    const bool half_res = (probes.probe_block().ssr_flags & GOO_SSR_FLAG_HALFRES) != 0;
    const int2 trace_ext = half_res ? math::max(math::divide_ceil(render_extent, int2(2)), int2(1)) :
                                      render_extent;
    ssr_hit_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16, trace_ext, img_usage);

    const int hiz_mips = safe_mip_count(render_extent, GOO_HIZ_MIPS);
    if (hiz_depth_tx_.ensure_2d(
            gpu::TextureFormat::SFLOAT_32, render_extent, img_usage, nullptr, hiz_mips))
    {
      hiz_depth_tx_.ensure_mip_views();
      GPU_texture_mipmap_mode(hiz_depth_tx_, true, false);
    }
    else {
      hiz_depth_tx_.ensure_mip_views();
    }

    ssr_data_ptr_ = ssr_data_tx_;
    ssr_spec_ptr_ = ssr_spec_tx_;
    ssr_fallback_ptr_ = ssr_fallback_tx_;
  }
  else {
    ssr_data_tx_.free();
    ssr_spec_tx_.free();
    ssr_fallback_tx_.free();
    ssr_out_tx_.free();
    ssr_hit_tx_.free();
    hiz_depth_tx_.free();
    ssr_data_ptr_ = dummy_ssr_tx_;
    ssr_spec_ptr_ = dummy_ssr_tx_;
    ssr_fallback_ptr_ = dummy_ssr_tx_;
  }

  ShaderModule &sh = ShaderModule::get();
  const GPUSamplerState nearest_sampler{GPU_SAMPLER_FILTERING_DEFAULT};
  const GPUSamplerState lin_sampler{GPU_SAMPLER_FILTERING_LINEAR};
  const GPUSamplerState lin_mip_sampler{GPU_SAMPLER_FILTERING_LINEAR |
                                        GPU_SAMPLER_FILTERING_MIPMAP};

  color_copy_ps_.init();
  color_copy_ps_.state_set(DRW_STATE_NO_DRAW);
  color_copy_ps_.shader_set(sh.color_copy.get());
  color_copy_ps_.bind_texture("src_tx", &unf_src_tx_, nearest_sampler);
  color_copy_ps_.bind_image("dst_img", &unf_dst_img_);
  color_copy_ps_.push_constant("extent", &unf_extent_);
  color_copy_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS |
                         GPU_BARRIER_FRAMEBUFFER);
  color_copy_ps_.dispatch(&unf_copy_dispatch_);

  color_downsample_ps_.init();
  color_downsample_ps_.state_set(DRW_STATE_NO_DRAW);
  color_downsample_ps_.shader_set(sh.color_downsample.get());
  color_downsample_ps_.bind_texture("src_tx", &unf_src_tx_, lin_sampler);
  color_downsample_ps_.bind_image("dst_img", &unf_dst_img_);
  color_downsample_ps_.push_constant("src_extent", &unf_src_extent_);
  color_downsample_ps_.push_constant("dst_extent", &unf_dst_extent_);
  color_downsample_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
  color_downsample_ps_.dispatch(&unf_down_dispatch_);

  hiz_copy_ps_.init();
  hiz_copy_ps_.state_set(DRW_STATE_NO_DRAW);
  hiz_copy_ps_.shader_set(sh.hiz_copy.get());
  hiz_copy_ps_.bind_texture("src_depth_tx", &unf_depth_src_tx_, nearest_sampler);
  hiz_copy_ps_.bind_image("dst_hiz_img", &unf_dst_img_);
  hiz_copy_ps_.push_constant("extent", &unf_extent_);
  hiz_copy_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS |
                       GPU_BARRIER_FRAMEBUFFER);
  hiz_copy_ps_.dispatch(&unf_hiz_copy_dispatch_);

  hiz_downsample_ps_.init();
  hiz_downsample_ps_.state_set(DRW_STATE_NO_DRAW);
  hiz_downsample_ps_.shader_set(sh.hiz_downsample.get());
  hiz_downsample_ps_.bind_texture("src_hiz_tx", &unf_src_tx_, nearest_sampler);
  hiz_downsample_ps_.bind_image("dst_hiz_img", &unf_dst_img_);
  hiz_downsample_ps_.push_constant("src_extent", &unf_src_extent_);
  hiz_downsample_ps_.push_constant("dst_extent", &unf_dst_extent_);
  hiz_downsample_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
  hiz_downsample_ps_.dispatch(&unf_hiz_down_dispatch_);

  ssr_trace_ps_.init();
  ssr_trace_ps_.state_set(DRW_STATE_NO_DRAW);
  ssr_trace_ps_.shader_set(sh.ssr_trace.get());
  probes.bind_ubo(ssr_trace_ps_);
  ssr_trace_ps_.bind_ubo(GOO_LIGHT_UBO_SLOT, lights.light_buf());
  ssr_trace_ps_.bind_texture(RBUFS_UTILITY_TEX_SLOT, sh.utility_tx);
  ssr_trace_ps_.bind_texture("hiz_depth_tx", &unf_hiz_tx_, nearest_sampler);
  ssr_trace_ps_.bind_texture("ssr_data_tx", &ssr_data_ptr_, nearest_sampler);
  ssr_trace_ps_.bind_texture("ssr_spec_tx", &ssr_spec_ptr_, nearest_sampler);
  ssr_trace_ps_.bind_image("out_hit_img", &unf_hit_tx_);
  ssr_trace_ps_.push_constant("render_extent", &unf_extent_);
  ssr_trace_ps_.push_constant("trace_extent", &unf_trace_extent_);
  ssr_trace_ps_.push_constant("half_res", &unf_half_res_);
  ssr_trace_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
  ssr_trace_ps_.dispatch(&unf_trace_dispatch_);

  ssr_resolve_ps_.init();
  ssr_resolve_ps_.state_set(DRW_STATE_NO_DRAW);
  ssr_resolve_ps_.shader_set(sh.ssr_resolve.get());
  probes.bind_ubo(ssr_resolve_ps_);
  ssr_resolve_ps_.bind_texture("scene_color_tx", &unf_src_tx_, lin_mip_sampler);
  ssr_resolve_ps_.bind_texture("ssr_data_tx", &ssr_data_ptr_, nearest_sampler);
  ssr_resolve_ps_.bind_texture("ssr_spec_tx", &ssr_spec_ptr_, nearest_sampler);
  ssr_resolve_ps_.bind_texture("ssr_fallback_tx", &ssr_fallback_ptr_, nearest_sampler);
  ssr_resolve_ps_.bind_texture("ssr_hit_tx", &unf_hit_tx_, lin_sampler);
  ssr_resolve_ps_.bind_texture("hiz_depth_tx", &unf_hiz_tx_, nearest_sampler);
  ssr_resolve_ps_.bind_image("out_color_img", &unf_dst_img_);
  ssr_resolve_ps_.push_constant("render_extent", &unf_extent_);
  ssr_resolve_ps_.push_constant("trace_extent", &unf_trace_extent_);
  ssr_resolve_ps_.push_constant("half_res", &unf_half_res_);
  ssr_resolve_ps_.push_constant("color_mips", &unf_color_mips_);
  ssr_resolve_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
  ssr_resolve_ps_.dispatch(&unf_resolve_dispatch_);
}

void SsrModule::clear_gbuffer(bool ssr_active)
{
  if (!ssr_active) {
    return;
  }
  ssr_data_tx_.clear(float4(0.0f, 0.0f, 0.0f, -1.0f));
  ssr_spec_tx_.clear(float4(0.0f));
  ssr_fallback_tx_.clear(float4(0.0f));
}

void SsrModule::build_color_pyramid(Manager &manager,
                                    gpu::Texture *src_tx,
                                    Texture &dst_pyramid_tx,
                                    int2 extent)
{
  if (src_tx == nullptr || dst_pyramid_tx.gpu_texture() == nullptr) {
    return;
  }
  dst_pyramid_tx.ensure_mip_views();
  const int mips = dst_pyramid_tx.mip_count();

  unf_src_tx_ = src_tx;
  unf_dst_img_ = dst_pyramid_tx.mip_view(0);
  unf_extent_ = extent;
  unf_copy_dispatch_ = int3(math::divide_ceil(extent, int2(8)), 1);
  manager.submit(color_copy_ps_);

  int2 cur_ext = extent;
  for (int m = 1; m < mips; m++) {
    const int2 dst_ext = math::max(cur_ext / 2, int2(1));
    unf_src_tx_ = dst_pyramid_tx.mip_view(m - 1);
    unf_dst_img_ = dst_pyramid_tx.mip_view(m);
    unf_src_extent_ = cur_ext;
    unf_dst_extent_ = dst_ext;
    unf_down_dispatch_ = int3(math::divide_ceil(dst_ext, int2(8)), 1);
    manager.submit(color_downsample_ps_);
    cur_ext = dst_ext;
  }
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
}

void SsrModule::build_hiz_pyramid(Manager &manager, gpu::Texture *prepass_depth_tx, int2 extent)
{
  if (prepass_depth_tx == nullptr || hiz_depth_tx_.gpu_texture() == nullptr) {
    return;
  }
  hiz_depth_tx_.ensure_mip_views();
  const int mips = hiz_depth_tx_.mip_count();

  unf_depth_src_tx_ = prepass_depth_tx;
  unf_dst_img_ = hiz_depth_tx_.mip_view(0);
  unf_extent_ = extent;
  unf_hiz_copy_dispatch_ = int3(math::divide_ceil(extent, int2(8)), 1);
  manager.submit(hiz_copy_ps_);

  int2 cur_ext = extent;
  for (int m = 1; m < mips; m++) {
    const int2 dst_ext = math::max(cur_ext / 2, int2(1));
    unf_src_tx_ = hiz_depth_tx_.mip_view(m - 1);
    unf_dst_img_ = hiz_depth_tx_.mip_view(m);
    unf_src_extent_ = cur_ext;
    unf_dst_extent_ = dst_ext;
    unf_hiz_down_dispatch_ = int3(math::divide_ceil(dst_ext, int2(8)), 1);
    manager.submit(hiz_downsample_ps_);
    cur_ext = dst_ext;
  }
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
}

void SsrModule::trace_and_resolve(Manager &manager,
                                  View &sample_view,
                                  gpu::Texture *prepass_depth_tx,
                                  Texture &scene_color_pyramid_tx,
                                  gpu::Texture *dst_color_tx,
                                  ProbeModule &probes,
                                  int2 extent)
{
  build_hiz_pyramid(manager, prepass_depth_tx, extent);

  const bool half_res = (probes.probe_block().ssr_flags & GOO_SSR_FLAG_HALFRES) != 0;
  const int2 trace_ext = half_res ? math::max(math::divide_ceil(extent, int2(2)), int2(1)) :
                                    extent;

  unf_hiz_tx_ = hiz_depth_tx_;
  unf_hit_tx_ = ssr_hit_tx_;
  unf_extent_ = extent;
  unf_trace_extent_ = trace_ext;
  unf_half_res_ = half_res ? 1 : 0;
  unf_trace_dispatch_ = int3(math::divide_ceil(trace_ext, int2(8)), 1);
  manager.submit(ssr_trace_ps_, sample_view);

  unf_src_tx_ = scene_color_pyramid_tx;
  unf_dst_img_ = ssr_out_tx_;
  unf_color_mips_ = std::max(1, scene_color_pyramid_tx.mip_count());
  unf_resolve_dispatch_ = int3(math::divide_ceil(extent, int2(8)), 1);
  manager.submit(ssr_resolve_ps_);

  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS |
                     GPU_BARRIER_TEXTURE_UPDATE);
  GPU_texture_copy(dst_color_tx, ssr_out_tx_);
}

}  // namespace blender::goo
