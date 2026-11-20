/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Scene lighting UBO + depth-only shadow atlas (Sun cascades, Spot, Point cube faces).
 */

#pragma once

#include "BLI_vector.hh"

#include "DRW_gpu_wrapper.hh"
#include "DRW_render.hh"
#include "draw_manager.hh"
#include "draw_pass.hh"
#include "draw_view.hh"

#include "goo_shader_shared.hh"

namespace blender {
struct Object;
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
  Framebuffer shadow_fb_ = {"goo.shadow_fb"};
  View shadow_view_ = {"goo.shadow_view"};
  PassMain shadow_ps_ = {"Goo.Shadow"};

  Vector<ShadowRenderSlice> render_slices_;

 public:
  void begin_sync(float4 world_ambient);
  void caster_sync(Object *ob, ResourceHandleRange handle);
  void light_sync(Object *ob, const View &camera_view);
  void end_sync();

  /** Render all shadow slices into `shadow_tx_`. Must run before binding `main_fb_`. */
  void render_shadows(Manager &manager);

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
};

}  // namespace blender::goo
