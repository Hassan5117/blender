/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Object & camera velocity buffer pass and McGuire tile-max gather Motion Blur.
 */

#pragma once

#include "BLI_map.hh"

#include "DRW_gpu_wrapper.hh"
#include "DRW_render.hh"
#include "draw_manager.hh"
#include "draw_pass.hh"
#include "draw_view.hh"

#include "goo_defines.hh"
#include "goo_shader_shared.hh"

namespace blender {
struct ModifierData;
struct Object;
struct ParticleSystem;
struct Scene;
struct ViewLayer;
}  // namespace blender

namespace blender::goo {

using namespace draw;

class MotionBlurModule {
 public:
  struct StepData {
    float4x4 viewmat = float4x4::identity();
    float4x4 winmat = float4x4::identity();
    Map<ObjectKey, float4x4> object_mats;
    Map<const Object *, float4x4> orig_object_mats;
  };

 private:
  UniformBuffer<GooMBlurBlock> mblur_buf_ = {"GooMBlurBlock"};
  StorageArrayBuffer<GooVelocityObject, GOO_MAX_VELOCITY_OBJECTS> vel_buf_ = {
      "GooVelocityObjectBuf"};

  PassMain velocity_ps_ = {"Goo.Velocity"};
  PassMain::Sub *vel_mesh_cull_sub_ = nullptr;
  PassMain::Sub *vel_mesh_nocull_sub_ = nullptr;
  PassMain::Sub *vel_curves_sub_ = nullptr;

  PassSimple tile_ps_ = {"Goo.MBlurTile"};
  PassSimple dilate_ps_ = {"Goo.MBlurDilate"};
  PassSimple gather_ps_ = {"Goo.MBlurGather"};

  Framebuffer velocity_fb_ = {"goo.velocity_fb"};
  Texture velocity_tx_ = {"goo.velocity_tx"};
  Texture tile_tx_ = {"goo.mblur_tile_tx"};
  Texture tile_dilated_tx_ = {"goo.mblur_tile_dilated_tx"};
  Texture out_tx_ = {"goo.mblur_out_tx"};

  gpu::Texture *velocity_ptr_ = nullptr;
  gpu::Texture *tile_ptr_ = nullptr;
  gpu::Texture *tile_dilated_ptr_ = nullptr;
  gpu::Texture *src_color_ptr_ = nullptr;
  gpu::Texture *src_depth_ptr_ = nullptr;
  gpu::Texture *out_ptr_ = nullptr;

  int2 tile_extent_ = int2(1);
  int radius_tiles_ = 1;
  int3 tile_dispatch_ = int3(1);
  int3 dilate_dispatch_ = int3(1);
  int3 gather_dispatch_ = int3(1);

  StepData prev_step_;
  StepData next_step_;
  bool has_step_data_ = false;
  bool enabled_ = false;
  float max_blur_px_ = 32.0f;
  float depth_scale_ = 100.0f;

  void lookup_step_mats(const ObjectRef &ob_ref,
                        float4x4 &r_prev,
                        float4x4 &r_next) const;

 public:
  void clear_step_data()
  {
    prev_step_.object_mats.clear();
    prev_step_.orig_object_mats.clear();
    next_step_.object_mats.clear();
    next_step_.orig_object_mats.clear();
    has_step_data_ = false;
  }

  void set_step_data(StepData &&prev, StepData &&next)
  {
    prev_step_ = std::move(prev);
    next_step_ = std::move(next);
    has_step_data_ = true;
  }

  const float4x4 &prev_viewmat() const
  {
    return prev_step_.viewmat;
  }
  const float4x4 &prev_winmat() const
  {
    return prev_step_.winmat;
  }
  const float4x4 &next_viewmat() const
  {
    return next_step_.viewmat;
  }
  const float4x4 &next_winmat() const
  {
    return next_step_.winmat;
  }

  bool enabled() const
  {
    return enabled_;
  }

  void begin_sync(const Scene *scene, const ViewLayer *vl, bool is_image_render);

  void sync_object(const ObjectRef &ob_ref,
                   ResourceHandleRange handle,
                   gpu::Batch *geom,
                   bool cull_back);

  void sync_curves(const ObjectRef &ob_ref,
                   const Scene *scene,
                   ParticleSystem *psys,
                   ModifierData *md,
                   ResourceHandleRange handle);

  void end_sync();

  void render(Manager &manager,
              View &sample_view,
              const float4x4 &prev_persmat,
              const float4x4 &next_persmat,
              gpu::Texture *depth_tx,
              gpu::Texture *color_tx,
              int sample_index,
              int2 extent);
};

}  // namespace blender::goo
