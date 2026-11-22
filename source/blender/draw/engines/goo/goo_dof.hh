/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Physical aperture Depth of Field (CoC, dilated tile bounds, polygonal bokeh gather, optional TAA jitter).
 */

#pragma once

#include "DRW_gpu_wrapper.hh"
#include "draw_manager.hh"
#include "draw_pass.hh"
#include "draw_view.hh"

#include "goo_shader_shared.hh"

namespace blender {
struct Object;
struct Scene;
}  // namespace blender

namespace blender::goo {

using namespace draw;

class DofModule {
 private:
  UniformBuffer<GooDofBlock> dof_buf_ = {"GooDofBlock"};

  Texture coc_tx_ = {"goo.dof_coc_tx"};
  Texture prefilter_tx_ = {"goo.dof_prefilter_tx"};
  Texture tile_a_tx_ = {"goo.dof_tile_a_tx"};
  Texture tile_b_tx_ = {"goo.dof_tile_b_tx"};
  Texture out_tx_ = {"goo.dof_out_tx"};

  gpu::Texture *coc_ptr_ = nullptr;
  gpu::Texture *prefilter_ptr_ = nullptr;
  gpu::Texture *tile_a_ptr_ = nullptr;
  gpu::Texture *tile_b_ptr_ = nullptr;
  gpu::Texture *out_ptr_ = nullptr;
  gpu::Texture *src_color_ptr_ = nullptr;
  gpu::Texture *src_depth_ptr_ = nullptr;

  int3 coc_dispatch_ = int3(1);
  int3 dilate_dispatch_ = int3(1);
  int3 gather_dispatch_ = int3(1);
  int2 tile_extent_ = int2(1);
  int2 dir_h_ = int2(1, 0);
  int2 dir_v_ = int2(0, 1);
  int radius_tiles_ = 1;
  int unf_sample_index_ = 0;

  PassSimple coc_ps_ = {"Goo.DofCoC"};
  PassSimple dilate_h_ps_ = {"Goo.DofDilateH"};
  PassSimple dilate_v_ps_ = {"Goo.DofDilateV"};
  PassSimple gather_ps_ = {"Goo.DofGather"};

  bool enabled_ = false;
  bool jittered_ = false;
  bool is_persp_ = true;
  float focus_dist_ = 10.0f;
  float aperture_radius_ = 0.0f;
  float proj_scale_ = 1.0f;
  float max_coc_ = 100.0f;
  float overblur_ = 0.05f;
  float blades_ = 0.0f;
  float rotation_ = 0.0f;
  float2 aniso_ = float2(1.0f);
  float threshold_ = 1.0f;
  float neighbor_max_ = 10.0f;
  float denoise_fac_ = 0.75f;
  float hq_slight_ = 0.0f;

 public:
  void sync(const Scene *scene,
            const Object *camera_ob,
            int2 render_extent,
            const float4x4 &winmat);

  bool enabled() const
  {
    return enabled_;
  }

  void jitter_camera(int sample_index,
                     int total_samples,
                     float4x4 &viewmat,
                     float4x4 &winmat) const;

  void render(Manager &manager,
              View &view,
              gpu::Texture *depth_tx,
              gpu::Texture *color_tx,
              int sample_index,
              int total_samples,
              int2 extent);
};

}  // namespace blender::goo
