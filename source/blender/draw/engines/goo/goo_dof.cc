/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include <algorithm>
#include <cmath>

#include "BKE_camera.h"

#include "BLI_math_base.hh"
#include "BLI_rand.h"

#include "DNA_camera_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "GPU_state.hh"
#include "GPU_texture.hh"

#include "../eevee/eevee_defines.hh"

#include "goo_dof.hh"
#include "goo_shader.hh"

namespace blender::goo {

static float2 dof_polygon_sample(int sample_index,
                                 int total_samples,
                                 float blades,
                                 float rotation,
                                 float2 aniso)
{
  const unsigned int primes[2] = {5, 7};
  double offset[2] = {0.0, 0.0};
  double ht[2] = {0.0, 0.0};
  BLI_halton_2d(primes, offset, sample_index + 1, ht);

  const float u = float(ht[0]);
  const float r = std::sqrt(std::clamp(u, 0.0f, 1.0f));
  const float theta = float(ht[1]) * 2.0f * float(M_PI);

  float scale = 1.0f;
  if (blades >= 2.5f) {
    const float sector = 2.0f * float(M_PI) / blades;
    const float half_sector = sector * 0.5f;
    const float local_a = std::fmod(theta + half_sector, sector) - half_sector;
    scale = std::cos(half_sector) / std::max(std::cos(local_a), 1e-4f);
  }
  const float ang = theta + rotation + float(sample_index) / float(std::max(1, total_samples)) * 0.1f;
  return float2(std::cos(ang), std::sin(ang)) * (r * scale) * aniso;
}

void DofModule::free_textures()
{
  coc_tx_.free();
  prefilter_tx_.free();
  tile_a_tx_.free();
  tile_b_tx_.free();
  out_tx_.free();
  coc_ptr_ = nullptr;
  prefilter_ptr_ = nullptr;
  tile_a_ptr_ = nullptr;
  tile_b_ptr_ = nullptr;
  out_ptr_ = nullptr;
}

void DofModule::sync(const Scene *scene,
                     const Object *camera_ob,
                     int2 render_extent,
                     const float4x4 &winmat)
{
  enabled_ = false;
  if (camera_ob == nullptr || camera_ob->type != OB_CAMERA || camera_ob->data == nullptr) {
    free_textures();
    return;
  }

  const Camera *cam = reinterpret_cast<const Camera *>(camera_ob->data);
  if ((cam->dof.flag & CAM_DOF_ENABLED) == 0 || cam->type == CAM_PANO) {
    free_textures();
    return;
  }

  enabled_ = true;
  is_persp_ = (cam->type == CAM_PERSP);
  focus_dist_ = std::max(BKE_camera_object_dof_distance(camera_ob), 1e-4f);

  const float fstop = std::max(cam->dof.aperture_fstop, 1e-4f);
  const float flen_m = cam->lens * 1e-3f;
  if (is_persp_) {
    aperture_radius_ = (0.5f * flen_m / fstop) *
                       (focus_dist_ / std::max(focus_dist_ - flen_m, 1e-4f));
  }
  else {
    aperture_radius_ = (0.5f / fstop) * 0.04f;
  }

  proj_scale_ = 0.5f * float(std::max(1, render_extent.x)) * std::abs(winmat[0][0]);
  max_coc_ = (scene && scene->eevee.bokeh_max_size > 0.0f) ?
                 std::min(scene->eevee.bokeh_max_size, 128.0f) :
                 100.0f;
  jittered_ = (scene != nullptr) && ((scene->eevee.flag & SCE_EEVEE_DOF_JITTER) != 0);
  overblur_ = scene ? std::clamp(scene->eevee.bokeh_overblur / 100.0f, 0.0f, 1.0f) : 0.05f;
  blades_ = (cam->dof.aperture_blades >= 3) ? float(cam->dof.aperture_blades) : 0.0f;
  rotation_ = cam->dof.aperture_rotation;

  const float ratio = std::max(cam->dof.aperture_ratio, 1e-4f);
  aniso_ = (ratio >= 1.0f) ? float2(1.0f / ratio, 1.0f) : float2(1.0f, ratio);

  threshold_ = scene ? scene->eevee.bokeh_threshold : 1.0f;
  neighbor_max_ = scene ? scene->eevee.bokeh_neighbor_max : 10.0f;
  denoise_fac_ = scene ? std::clamp(scene->eevee.bokeh_denoise_fac, 0.0f, 1.0f) : 0.75f;
  hq_slight_ = (scene && (scene->eevee.flag & SCE_EEVEE_DOF_HQ_SLIGHT_FOCUS) != 0) ? 1.0f : 0.0f;

  ShaderModule &sh = ShaderModule::get();
  const GPUSamplerState nearest{GPU_SAMPLER_FILTERING_DEFAULT};
  const GPUSamplerState lin{GPU_SAMPLER_FILTERING_LINEAR};

  coc_ps_.init();
  coc_ps_.state_set(DRW_STATE_NO_DRAW);
  coc_ps_.shader_set(sh.dof_coc.get());
  coc_ps_.bind_ubo("goo_dof_buf", dof_buf_);
  coc_ps_.bind_texture("src_color_tx", &src_color_ptr_, nearest);
  coc_ps_.bind_texture("depth_tx", &src_depth_ptr_, nearest);
  coc_ps_.bind_image("out_coc_img", &coc_ptr_);
  coc_ps_.bind_image("out_color_img", &prefilter_ptr_);
  coc_ps_.bind_image("out_tile_img", &tile_a_ptr_);
  coc_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS |
                  GPU_BARRIER_FRAMEBUFFER);
  coc_ps_.dispatch(&coc_dispatch_);

  dilate_h_ps_.init();
  dilate_h_ps_.state_set(DRW_STATE_NO_DRAW);
  dilate_h_ps_.shader_set(sh.dof_dilate.get());
  dilate_h_ps_.bind_texture("src_tile_tx", &tile_a_ptr_, nearest);
  dilate_h_ps_.bind_image("dst_tile_img", &tile_b_ptr_);
  dilate_h_ps_.push_constant("tile_extent", &tile_extent_);
  dilate_h_ps_.push_constant("dir_step", &dir_h_);
  dilate_h_ps_.push_constant("radius_tiles", &radius_tiles_);
  dilate_h_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
  dilate_h_ps_.dispatch(&dilate_dispatch_);

  dilate_v_ps_.init();
  dilate_v_ps_.state_set(DRW_STATE_NO_DRAW);
  dilate_v_ps_.shader_set(sh.dof_dilate.get());
  dilate_v_ps_.bind_texture("src_tile_tx", &tile_b_ptr_, nearest);
  dilate_v_ps_.bind_image("dst_tile_img", &tile_a_ptr_);
  dilate_v_ps_.push_constant("tile_extent", &tile_extent_);
  dilate_v_ps_.push_constant("dir_step", &dir_v_);
  dilate_v_ps_.push_constant("radius_tiles", &radius_tiles_);
  dilate_v_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
  dilate_v_ps_.dispatch(&dilate_dispatch_);

  gather_ps_.init();
  gather_ps_.state_set(DRW_STATE_NO_DRAW);
  gather_ps_.shader_set(sh.dof_gather.get());
  gather_ps_.bind_ubo("goo_dof_buf", dof_buf_);
  gather_ps_.bind_texture(RBUFS_UTILITY_TEX_SLOT, sh.utility_tx);
  gather_ps_.bind_texture("src_color_tx", &prefilter_ptr_, lin);
  gather_ps_.bind_texture("coc_tx", &coc_ptr_, lin);
  gather_ps_.bind_texture("tile_tx", &tile_a_ptr_, nearest);
  gather_ps_.bind_texture("depth_tx", &src_depth_ptr_, nearest);
  gather_ps_.bind_image("dst_color_img", &out_ptr_);
  gather_ps_.push_constant("sample_index", &unf_sample_index_);
  gather_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
  gather_ps_.dispatch(&gather_dispatch_);
}

void DofModule::jitter_camera(int sample_index,
                              int total_samples,
                              float4x4 &viewmat,
                              float4x4 &winmat) const
{
  if (!enabled_ || !jittered_ || total_samples <= 1 || !is_persp_ || aperture_radius_ <= 0.0f) {
    return;
  }

  const float2 disk = dof_polygon_sample(sample_index, total_samples, blades_, rotation_, aniso_);
  const float2 off = disk * aperture_radius_;

  viewmat[3][0] -= off.x;
  viewmat[3][1] -= off.y;
  winmat[2][0] -= (off.x / focus_dist_) * winmat[0][0];
  winmat[2][1] -= (off.y / focus_dist_) * winmat[1][1];
}

void DofModule::render(Manager &manager,
                       View &view,
                       gpu::Texture *depth_tx,
                       gpu::Texture *color_tx,
                       int sample_index,
                       int total_samples,
                       int2 extent)
{
  if (!enabled_ || depth_tx == nullptr || color_tx == nullptr) {
    return;
  }

  const float fx_radius = (jittered_ && total_samples > 1) ?
                              aperture_radius_ *
                                  std::max(overblur_, 1.25f / std::sqrt(float(total_samples))) :
                              aperture_radius_;
  if (fx_radius <= 1e-6f) {
    free_textures();
    return;
  }

  const int2 safe_ext = math::max(extent, int2(1));
  const int2 tile_ext = math::max(math::divide_ceil(safe_ext, int2(16)), int2(1));

  const eGPUTextureUsage rw_usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_SHADER_WRITE;
  coc_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_32, safe_ext, rw_usage);
  prefilter_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16, safe_ext, rw_usage);
  tile_a_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16, tile_ext, rw_usage);
  tile_b_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16, tile_ext, rw_usage);
  out_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16, safe_ext, rw_usage);

  coc_ptr_ = coc_tx_;
  prefilter_ptr_ = prefilter_tx_;
  tile_a_ptr_ = tile_a_tx_;
  tile_b_ptr_ = tile_b_tx_;
  out_ptr_ = out_tx_;
  src_color_ptr_ = color_tx;
  src_depth_ptr_ = depth_tx;

  dof_buf_.coc_params = float4(fx_radius, focus_dist_, max_coc_, proj_scale_);
  dof_buf_.bokeh_params = float4(blades_, rotation_, threshold_, neighbor_max_);
  dof_buf_.aniso_params = float4(aniso_.x, aniso_.y, 1.0f / aniso_.x, 1.0f / aniso_.y);
  dof_buf_.filter_params = float4(float(safe_ext.x), float(safe_ext.y), denoise_fac_, hq_slight_);
  dof_buf_.push_update();

  coc_dispatch_ = int3(tile_ext, 1);
  dilate_dispatch_ = int3(math::divide_ceil(tile_ext, int2(8)), 1);
  gather_dispatch_ = int3(math::divide_ceil(safe_ext, int2(8)), 1);
  tile_extent_ = tile_ext;
  dir_h_ = int2(1, 0);
  dir_v_ = int2(0, 1);
  radius_tiles_ = std::clamp(int(std::ceil(max_coc_ / 16.0f)), 1, 8);
  unf_sample_index_ = sample_index;

  manager.submit(coc_ps_, view);
  manager.submit(dilate_h_ps_, view);
  manager.submit(dilate_v_ps_, view);
  manager.submit(gather_ps_, view);
  GPU_memory_barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS | GPU_BARRIER_TEXTURE_FETCH);
  GPU_texture_copy(color_tx, out_tx_);
}

}  // namespace blender::goo
