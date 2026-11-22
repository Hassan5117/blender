/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include "BLI_math_base.hh"

#include "GPU_state.hh"
#include "GPU_texture.hh"

#include "../eevee/eevee_defines.hh"

#include "goo_probe.hh"
#include "goo_shader.hh"
#include "goo_sss.hh"

namespace blender::goo {

void SssModule::begin_sync(ProbeModule &probes)
{
  const eGPUTextureUsage usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_SHADER_WRITE;
  if (dummy_sss_tx_.ensure_2d_array(
          gpu::TextureFormat::SFLOAT_16_16_16_16, int2(1), 3, usage))
  {
    dummy_sss_tx_.clear(float4(0.0f));
  }

  has_sss_materials_ = false;
  sss_write_ptr_ = dummy_sss_tx_;
  resolve_sss_ptr_ = dummy_sss_tx_;

  ShaderModule &sh = ShaderModule::get();
  const GPUSamplerState nearest{GPU_SAMPLER_FILTERING_DEFAULT};

  resolve_ps_.init();
  resolve_ps_.state_set(DRW_STATE_NO_DRAW);
  resolve_ps_.shader_set(sh.sss_resolve.get());
  probes.bind_ubo(resolve_ps_);
  resolve_ps_.bind_texture(RBUFS_UTILITY_TEX_SLOT, sh.utility_tx);
  resolve_ps_.bind_texture("sss_data_tx", &resolve_sss_ptr_, nearest);
  resolve_ps_.bind_texture("depth_tx", &resolve_depth_ptr_, nearest);
  resolve_ps_.bind_image("color_img", &resolve_color_ptr_);
  resolve_ps_.push_constant("extent", &unf_extent_);
  resolve_ps_.push_constant("sample_index", &unf_sample_index_);
  resolve_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS |
                      GPU_BARRIER_FRAMEBUFFER);
  resolve_ps_.dispatch(&dispatch_size_);
}

void SssModule::end_sync(int2 render_extent)
{
  if (has_sss_materials_) {
    const int2 extent = math::max(render_extent, int2(1));
    const eGPUTextureUsage usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_SHADER_WRITE;
    sss_data_tx_.ensure_2d_array(gpu::TextureFormat::SFLOAT_16_16_16_16, extent, 3, usage);
    sss_write_ptr_ = sss_data_tx_;
    resolve_sss_ptr_ = sss_data_tx_;
  }
  else {
    sss_write_ptr_ = dummy_sss_tx_;
    resolve_sss_ptr_ = dummy_sss_tx_;
  }
}

void SssModule::clear_gbuffer()
{
  if (!has_sss_materials_ || !sss_data_tx_.is_valid()) {
    return;
  }
  sss_data_tx_.clear(float4(0.0f));
}

void SssModule::resolve(Manager &manager,
                        View &view,
                        gpu::Texture *depth_tx,
                        gpu::Texture *color_tx,
                        int sample_index,
                        int2 extent)
{
  if (!has_sss_materials_ || depth_tx == nullptr || color_tx == nullptr) {
    return;
  }

  resolve_sss_ptr_ = sss_data_tx_;
  resolve_depth_ptr_ = depth_tx;
  resolve_color_ptr_ = color_tx;
  unf_extent_ = extent;
  unf_sample_index_ = sample_index;
  dispatch_size_ = int3(math::divide_ceil(extent, int2(8)), 1);

  manager.submit(resolve_ps_, view);
  GPU_memory_barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS | GPU_BARRIER_TEXTURE_FETCH);
}

}  // namespace blender::goo
