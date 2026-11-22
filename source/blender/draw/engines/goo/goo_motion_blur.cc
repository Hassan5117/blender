/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include <algorithm>
#include <cmath>

#include "BLI_math_base.hh"

#include "DEG_depsgraph_query.hh"

#include "DNA_layer_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "GPU_framebuffer.hh"
#include "GPU_state.hh"
#include "GPU_texture.hh"

#include "draw_common.hh"

#include "../eevee/eevee_defines.hh"

#include "goo_motion_blur.hh"
#include "goo_shader.hh"

namespace blender::goo {

void MotionBlurModule::begin_sync(const Scene *scene, const ViewLayer *vl, bool is_image_render)
{
  enabled_ = is_image_render && has_step_data_ && (scene != nullptr) &&
             ((scene->r.mode & R_MBLUR) != 0) &&
             (vl == nullptr || (vl->layflag & SCE_LAY_MOTION_BLUR) != 0) &&
             (scene->r.motion_blur_shutter > 1e-5f);
  if (!enabled_) {
    return;
  }

  max_blur_px_ = float(std::max(1, scene->eevee.motion_blur_max));
  depth_scale_ = std::max(scene->eevee.motion_blur_depth_scale, 0.01f);

  for (int i = 0; i < GOO_MAX_VELOCITY_OBJECTS; i++) {
    vel_buf_[i].prev_obmat = float4x4::identity();
    vel_buf_[i].next_obmat = float4x4::identity();
  }

  ShaderModule &sh = ShaderModule::get();
  const GPUSamplerState nearest{GPU_SAMPLER_FILTERING_DEFAULT};

  velocity_ps_.init();
  velocity_ps_.state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_LESS_EQUAL);

  vel_mesh_cull_sub_ = &velocity_ps_.sub("VelMeshCull");
  vel_mesh_cull_sub_->state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_LESS_EQUAL |
                                DRW_STATE_CULL_BACK);
  vel_mesh_cull_sub_->shader_set(sh.velocity_mesh.get());
  vel_mesh_cull_sub_->bind_ubo("goo_mblur_buf", mblur_buf_);
  vel_mesh_cull_sub_->bind_ssbo("goo_vel_buf", vel_buf_);

  vel_mesh_nocull_sub_ = &velocity_ps_.sub("VelMeshNoCull");
  vel_mesh_nocull_sub_->state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_LESS_EQUAL);
  vel_mesh_nocull_sub_->shader_set(sh.velocity_mesh.get());
  vel_mesh_nocull_sub_->bind_ubo("goo_mblur_buf", mblur_buf_);
  vel_mesh_nocull_sub_->bind_ssbo("goo_vel_buf", vel_buf_);

  vel_curves_sub_ = &velocity_ps_.sub("VelCurves");
  vel_curves_sub_->state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_LESS_EQUAL);
  vel_curves_sub_->shader_set(sh.velocity_curves.get());
  vel_curves_sub_->bind_ubo("goo_mblur_buf", mblur_buf_);
  vel_curves_sub_->bind_ssbo("goo_vel_buf", vel_buf_);

  tile_ps_.init();
  tile_ps_.state_set(DRW_STATE_NO_DRAW);
  tile_ps_.shader_set(sh.mblur_tile.get());
  tile_ps_.bind_ubo("goo_mblur_buf", mblur_buf_);
  tile_ps_.bind_texture("velocity_tx", &velocity_ptr_, nearest);
  tile_ps_.bind_texture("depth_tx", &src_depth_ptr_, nearest);
  tile_ps_.bind_image("out_tile_img", &tile_ptr_);
  tile_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS |
                   GPU_BARRIER_FRAMEBUFFER);
  tile_ps_.dispatch(&tile_dispatch_);

  dilate_ps_.init();
  dilate_ps_.state_set(DRW_STATE_NO_DRAW);
  dilate_ps_.shader_set(sh.mblur_dilate.get());
  dilate_ps_.bind_texture("src_tile_tx", &tile_ptr_, nearest);
  dilate_ps_.bind_image("dst_tile_img", &tile_dilated_ptr_);
  dilate_ps_.push_constant("tile_extent", &tile_extent_);
  dilate_ps_.push_constant("radius_tiles", &radius_tiles_);
  dilate_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
  dilate_ps_.dispatch(&dilate_dispatch_);

  gather_ps_.init();
  gather_ps_.state_set(DRW_STATE_NO_DRAW);
  gather_ps_.shader_set(sh.mblur_gather.get());
  gather_ps_.bind_ubo("goo_mblur_buf", mblur_buf_);
  gather_ps_.bind_texture(RBUFS_UTILITY_TEX_SLOT, sh.utility_tx);
  gather_ps_.bind_texture("src_color_tx", &src_color_ptr_, nearest);
  gather_ps_.bind_texture("velocity_tx", &velocity_ptr_, nearest);
  gather_ps_.bind_texture("depth_tx", &src_depth_ptr_, nearest);
  gather_ps_.bind_texture("tile_tx", &tile_dilated_ptr_, nearest);
  gather_ps_.bind_image("dst_color_img", &out_ptr_);
  gather_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
  gather_ps_.dispatch(&gather_dispatch_);
}

void MotionBlurModule::lookup_step_mats(const ObjectRef &ob_ref,
                                        float4x4 &r_prev,
                                        float4x4 &r_next) const
{
  const float4x4 fallback = ob_ref.object_to_world();
  const ObjectKey key(ob_ref);
  if (const float4x4 *m = prev_step_.object_mats.lookup_ptr(key)) {
    r_prev = *m;
  }
  else if (const float4x4 *mo = prev_step_.orig_object_mats.lookup_ptr(
               DEG_get_original(ob_ref.object)))
  {
    r_prev = *mo;
  }
  else {
    r_prev = fallback;
  }

  if (const float4x4 *m = next_step_.object_mats.lookup_ptr(key)) {
    r_next = *m;
  }
  else if (const float4x4 *mo = next_step_.orig_object_mats.lookup_ptr(
               DEG_get_original(ob_ref.object)))
  {
    r_next = *mo;
  }
  else {
    r_next = fallback;
  }
}

void MotionBlurModule::sync_object(const ObjectRef &ob_ref,
                                   ResourceHandleRange handle,
                                   gpu::Batch *geom,
                                   bool cull_back)
{
  if (!enabled_ || geom == nullptr || !handle.is_valid()) {
    return;
  }

  float4x4 prev_mat = float4x4::identity();
  float4x4 next_mat = float4x4::identity();
  if (!ob_ref.is_range()) {
    lookup_step_mats(ob_ref, prev_mat, next_mat);
  }
  else {
    const Object *orig = DEG_get_original(ob_ref.object);
    prev_mat = prev_step_.orig_object_mats.lookup_default(orig, ob_ref.object->object_to_world());
    next_mat = next_step_.orig_object_mats.lookup_default(orig, ob_ref.object->object_to_world());
  }

  const int64_t count = handle.id_range().size();
  for (int64_t i = 0; i < count; i++) {
    const uint rid = handle.sub_handle(int(i)).index();
    if (rid < uint(GOO_MAX_VELOCITY_OBJECTS)) {
      vel_buf_[rid].prev_obmat = prev_mat;
      vel_buf_[rid].next_obmat = next_mat;
    }
  }

  (cull_back ? vel_mesh_cull_sub_ : vel_mesh_nocull_sub_)->draw(geom, handle);
}

void MotionBlurModule::sync_curves(const ObjectRef &ob_ref,
                                   const Scene *scene,
                                   ParticleSystem *psys,
                                   ModifierData *md,
                                   ResourceHandleRange handle)
{
  if (!enabled_ || !handle.is_valid() || vel_curves_sub_ == nullptr) {
    return;
  }

  float4x4 prev_mat = float4x4::identity();
  float4x4 next_mat = float4x4::identity();
  if (!ob_ref.is_range()) {
    lookup_step_mats(ob_ref, prev_mat, next_mat);
  }
  else {
    const Object *orig = DEG_get_original(ob_ref.object);
    prev_mat = prev_step_.orig_object_mats.lookup_default(orig, ob_ref.object->object_to_world());
    next_mat = next_step_.orig_object_mats.lookup_default(orig, ob_ref.object->object_to_world());
  }

  const int64_t count = handle.id_range().size();
  for (int64_t i = 0; i < count; i++) {
    const uint rid = handle.sub_handle(int(i)).index();
    if (rid < uint(GOO_MAX_VELOCITY_OBJECTS)) {
      vel_buf_[rid].prev_obmat = prev_mat;
      vel_buf_[rid].next_obmat = next_mat;
    }
  }

  PassMain::Sub &sub = vel_curves_sub_->sub("VelCurvesItem");
  gpu::Batch *geom = nullptr;
  if (psys != nullptr) {
    geom = hair_sub_pass_setup(sub, scene, ob_ref, psys, md, nullptr);
  }
  else {
    const char *unused_err = nullptr;
    geom = curves_sub_pass_setup(sub, scene, ob_ref.object, unused_err, nullptr);
  }
  if (geom != nullptr) {
    sub.draw(geom, handle);
  }
}

void MotionBlurModule::end_sync()
{
  if (!enabled_) {
    return;
  }
  vel_buf_.push_update();
}

void MotionBlurModule::render(Manager &manager,
                              View &sample_view,
                              const float4x4 &prev_persmat,
                              const float4x4 &next_persmat,
                              gpu::Texture *depth_tx,
                              gpu::Texture *color_tx,
                              int sample_index,
                              int2 extent)
{
  if (!enabled_ || depth_tx == nullptr || color_tx == nullptr) {
    return;
  }

  const int2 safe_ext = math::max(extent, int2(1));
  const int2 tile_ext = math::max(math::divide_ceil(safe_ext, int2(16)), int2(1));

  const eGPUTextureUsage vel_usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT;
  const eGPUTextureUsage rw_usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_SHADER_WRITE;

  velocity_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16, safe_ext, vel_usage);
  tile_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16, tile_ext, rw_usage);
  tile_dilated_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16, tile_ext, rw_usage);
  out_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16, safe_ext, rw_usage);

  velocity_ptr_ = velocity_tx_;
  tile_ptr_ = tile_tx_;
  tile_dilated_ptr_ = tile_dilated_tx_;
  src_color_ptr_ = color_tx;
  src_depth_ptr_ = depth_tx;
  out_ptr_ = out_tx_;

  mblur_buf_.prev_persmat = prev_persmat;
  mblur_buf_.curr_persmat = sample_view.winmat() * sample_view.viewmat();
  mblur_buf_.next_persmat = next_persmat;
  const float jitter_seed = std::fmod(float(sample_index) * 0.61803399f, 1.0f);
  mblur_buf_.mblur_params = float4(max_blur_px_, depth_scale_, jitter_seed, 1.0f);
  mblur_buf_.extent_params = int4(safe_ext.x, safe_ext.y, tile_ext.x, tile_ext.y);
  mblur_buf_.push_update();

  tile_extent_ = tile_ext;
  radius_tiles_ = std::clamp(int(std::ceil(max_blur_px_ / 16.0f)), 1, 6);
  tile_dispatch_ = int3(tile_ext, 1);
  dilate_dispatch_ = int3(math::divide_ceil(tile_ext, int2(8)), 1);
  gather_dispatch_ = int3(math::divide_ceil(safe_ext, int2(8)), 1);

  velocity_fb_.ensure(GPU_ATTACHMENT_TEXTURE(depth_tx), GPU_ATTACHMENT_TEXTURE(velocity_tx_));
  velocity_fb_.bind();
  GPU_framebuffer_clear_color(velocity_fb_, double4(0.0));
  manager.submit(velocity_ps_, sample_view);

  manager.submit(tile_ps_, sample_view);
  manager.submit(dilate_ps_, sample_view);
  manager.submit(gather_ps_, sample_view);
  GPU_memory_barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS | GPU_BARRIER_TEXTURE_FETCH);
  GPU_texture_copy(color_tx, out_tx_);
}

}  // namespace blender::goo
