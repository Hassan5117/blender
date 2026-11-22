/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Froxel Volumetrics (World + bounded Object volumes, H-G lighting & shadows, Frostbite integration).
 */

#pragma once

#include <memory>

#include "BLI_vector.hh"

#include "DRW_gpu_wrapper.hh"
#include "DRW_render.hh"
#include "draw_manager.hh"
#include "draw_pass.hh"
#include "draw_view.hh"

#include "goo_defines.hh"
#include "goo_shader_shared.hh"

namespace blender {
struct Object;
struct Scene;
struct World;
}  // namespace blender

namespace blender::goo {

using namespace draw;

class LightModule;
class MaterialModule;
class ProbeModule;

class VolumeModule {
 private:
  struct VolumeObjectEntry {
    float4x4 obj_to_world = float4x4::identity();
    float4x4 world_to_obj = float4x4::identity();
    float3 bounds_min = float3(-1.0f);
    float3 bounds_max = float3(1.0f);
    float4 orco_mul = float4(0.5f);
    float4 orco_add = float4(0.5f);
    PassMain near_depth_ps = {"Goo.VolObjNear"};
    PassMain far_depth_ps = {"Goo.VolObjFar"};
    PassSimple eval_ps = {"Goo.VolObjEval"};
  };

  UniformBuffer<GooVolumeBlock> vol_buf_ = {"GooVolumeBlock"};

  Texture prop_scatter_tx_ = {"goo.vol_prop_scatter"};
  Texture prop_extinct_tx_ = {"goo.vol_prop_extinct"};
  Texture prop_emissive_tx_ = {"goo.vol_prop_emissive"};
  Texture scatter_lit_tx_ = {"goo.vol_scatter_lit"};
  Texture integrated_scatter_tx_ = {"goo.vol_integ_scatter"};
  Texture integrated_transmit_tx_ = {"goo.vol_integ_transmit"};

  Texture vol_obj_near_tx_ = {"goo.vol_obj_near"};
  Texture vol_obj_far_tx_ = {"goo.vol_obj_far"};
  Texture vol_dummy_fb_tx_ = {"goo.vol_dummy_fb"};
  Texture dummy_depth_tx_ = {"goo.vol_dummy_depth"};
  Texture dummy_3d_tx_ = {"goo.vol_dummy_3d"};

  gpu::Texture *prop_scatter_ptr_ = nullptr;
  gpu::Texture *prop_extinct_ptr_ = nullptr;
  gpu::Texture *prop_emissive_ptr_ = nullptr;
  gpu::Texture *scatter_lit_ptr_ = nullptr;
  gpu::Texture *integrated_scatter_ptr_ = nullptr;
  gpu::Texture *integrated_transmit_ptr_ = nullptr;
  gpu::Texture *near_depth_ptr_ = nullptr;
  gpu::Texture *far_depth_ptr_ = nullptr;
  gpu::Texture *resolve_depth_ptr_ = nullptr;
  gpu::Texture *resolve_color_ptr_ = nullptr;

  Framebuffer vol_eval_fb_ = {"goo.vol_eval_fb"};
  Framebuffer vol_obj_near_fb_ = {"goo.vol_obj_near_fb"};
  Framebuffer vol_obj_far_fb_ = {"goo.vol_obj_far_fb"};
  View froxel_view_ = {"goo.vol_froxel_view"};

  PassSimple clear_ps_ = {"Goo.VolClear"};
  PassSimple world_ps_ = {"Goo.VolWorld"};
  PassSimple scatter_ps_ = {"Goo.VolScatter"};
  PassSimple integrate_ps_ = {"Goo.VolIntegrate"};
  PassSimple resolve_ps_ = {"Goo.VolResolve"};

  Vector<std::unique_ptr<VolumeObjectEntry>> objects_;

  bool has_world_volume_ = false;
  int tile_size_ = 8;
  int volume_samples_ = 64;
  int shadow_steps_ = 16;
  int volume_flags_ = 0;
  float depth_distrib_ = 0.8f;
  float vol_start_ = 0.1f;
  float vol_end_ = 100.0f;
  float3 clamp_params_ = float3(0.0f);

  int3 froxel_size_ = int3(1);
  int3 froxel_dispatch_ = int3(1);
  int3 integrate_dispatch_ = int3(1);
  int3 resolve_dispatch_ = int3(1);
  int unf_sample_index_ = 0;

  void ensure_render_targets(int2 render_extent);
  void free_render_targets();

 public:
  void begin_sync(const Scene *scene,
                  World *world,
                  MaterialModule &materials,
                  ProbeModule &probes,
                  LightModule &lights,
                  int2 render_extent,
                  bool deferred_compile);

  void object_sync(Manager &manager,
                   ProbeModule &probes,
                   Object *ob,
                   ResourceHandleRange handle,
                   gpu::Batch *geom,
                   GPUMaterial *vol_gpumat,
                   bool deferred_compile);

  bool enabled() const
  {
    return has_world_volume_ || !objects_.is_empty();
  }

  void render(Manager &manager,
              View &view,
              gpu::Texture *depth_tx,
              gpu::Texture *color_tx,
              int sample_index,
              int2 render_extent);
};

}  // namespace blender::goo
