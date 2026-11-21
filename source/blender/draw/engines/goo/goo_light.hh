/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Scene lighting UBO + depth-only shadow atlas (Sun cascades, Spot, Point cube faces).
 */

#pragma once

#include "BLI_map.hh"
#include "BLI_vector.hh"

#include "DRW_gpu_wrapper.hh"
#include "DRW_render.hh"
#include "GPU_material.hh"
#include "draw_manager.hh"
#include "draw_pass.hh"
#include "draw_view.hh"

#include "goo_shader_shared.hh"

namespace blender {
struct Material;
struct Object;
struct Scene;
}

namespace blender::goo {

using namespace draw;

class LightModule {
 private:
  struct ShadowRenderSlice {
    int layer;
    float4x4 viewmat;
    float4x4 winmat;
  };

  UniformBuffer<GooLightBlock> light_buf_ = {"GooLightBlock"};
  UniformBuffer<GooShadowBlock> shadow_buf_ = {"GooShadowBlock"};

  Texture shadow_tx_ = {"goo.shadow_atlas"};
  Texture shadow_id_tx_ = {"goo.shadow_id_atlas"};
  Texture dummy_shadow_depth_tx_ = {"goo.dummy_shadow_depth"};
  Texture dummy_shadow_id_tx_ = {"goo.dummy_shadow_id"};
  Texture dummy_depth_tx_ = {"goo.light_dummy_depth"};
  Texture dummy_color_tx_ = {"goo.light_dummy_color"};
  Texture dummy_horizon_tx_ = {"goo.light_dummy_horizon"};

  Framebuffer shadow_fb_ = {"goo.shadow_fb"};
  View shadow_view_ = {"goo.shadow_view"};
  PassMain shadow_ps_ = {"Goo.Shadow"};
  PassMain::Sub *shadow_cull_sub_ = nullptr;
  PassMain::Sub *shadow_nocull_sub_ = nullptr;
  Map<GPUMaterial *, PassMain::Sub *> shadow_mat_subs_;

  bool use_shadows_ = true;
  int shadow_res_ = GOO_SHADOW_RES;
  Vector<ShadowRenderSlice> render_slices_;

  PassMain::Sub &shadow_mat_sub_get(Manager &manager,
                                    const Material *ma,
                                    GPUMaterial *shadow_gpumat,
                                    bool deferred);

 public:
  void begin_sync(const Scene *scene, float4 world_ambient);
  void caster_slot_sync(Manager &manager,
                        gpu::Batch *geom,
                        const Material *ma,
                        GPUMaterial *shadow_gpumat,
                        ResourceHandleRange handle,
                        bool deferred);
  void light_sync(Object *ob, const View &camera_view);
  void end_sync();

  /** Render all shadow slices into `shadow_tx_` and `shadow_id_tx_`. */
  void render_shadows(Manager &manager);

  /** Update per-sample TAA & film parameters inside the lighting UBO before rendering a sample. */
  void set_sample_params(int sample_index, float2 sample_rand, bool film_transparent);

  gpu::UniformBuf *light_buf()
  {
    return light_buf_;
  }
  gpu::UniformBuf *shadow_buf()
  {
    return shadow_buf_;
  }
  gpu::Texture **shadow_tx_ptr()
  {
    return &shadow_tx_;
  }
  gpu::Texture **shadow_id_tx_ptr()
  {
    return &shadow_id_tx_;
  }
  bool shadows_enabled() const
  {
    return use_shadows_;
  }
  bool gtao_enabled() const
  {
    return (int(light_buf_.gtao_params.w + 0.5f) & GOO_GTAO_FLAG_ENABLED) != 0 &&
           light_buf_.gtao_params.x > 0.0f;
  }
  float4 gtao_params() const
  {
    return light_buf_.gtao_params;
  }
};

}  // namespace blender::goo
