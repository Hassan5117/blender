/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#pragma once

#include "DNA_world_types.h"

#include "DRW_render.hh"

#include "draw_manager.hh"
#include "draw_pass.hh"
#include "draw_view.hh"

#include "goo_bloom.hh"
#include "goo_light.hh"
#include "goo_material.hh"

namespace blender::goo {

using namespace draw;

/**
 * Engine instance. One per viewport (or per F12 render).
 *
 * Phase-0 skeleton: draws mesh surfaces with a trivial lit shader so that the
 * engine plumbing (registration, sync, submission, render-to-image) is proven on 5.x
 * before porting the EEVEE-Legacy based Goo pipeline on top of it.
 */
class Instance : public DrawEngine {
 public:
  const DRWContext *draw_ctx = nullptr;

  /** Depth prepass (opaque geometry) for Curvature and ScreenspaceInfo. */
  PassMain prepass_ps_ = {"Goo.Prepass"};

  /** Surface pass. One sub-pass per material color. */
  PassMain surface_ps_ = {"Goo.Surface"};
  Map<const Material *, PassMain::Sub *> material_subs_;

  /** Second-pass surface draw for materials reading scene color/depth (`ScreenspaceInfo`). */
  PassMain refract_ps_ = {"Goo.Refract"};
  Map<GPUMaterial *, PassMain::Sub *> refract_subs_;
  bool has_refract_draws_ = false;

  /** Procedural World background pass. */
  PassSimple world_ps_ = {"Goo.World"};
  bool has_world_shader_ = false;
  float4 world_color_ = float4(0.05f, 0.05f, 0.05f, 1.0f);

  /** Node-tree materials (one sub-pass per GPUMaterial). */
  MaterialModule materials;
  Map<GPUMaterial *, PassMain::Sub *> gpumat_subs_;

  /** Scene lights + shadow atlas. */
  LightModule lights;
  bool is_image_render_ = false;

  /** Offscreen buffers for depth prepass, screenspace info copy, and post effects. */
  TextureFromPool color_tx_ = {"goo.color"};
  TextureFromPool prepass_depth_tx_ = {"goo.prepass_depth"};
  TextureFromPool scene_color_tx_ = {"goo.scene_color"};
  Texture dummy_color_tx_ = {"goo.dummy_color"};
  gpu::Texture *prepass_depth_ptr_ = nullptr;
  gpu::Texture *scene_color_ptr_ = nullptr;
  Framebuffer prepass_fb_ = {"goo.prepass_fb"};
  Framebuffer main_fb_ = {"goo.main"};
  Framebuffer output_fb_ = {"goo.output"};

  Bloom bloom;

  StringRefNull name_get() final
  {
    return "Goo Engine";
  }

  void init() final;
  void begin_sync() final;
  void object_sync(ObjectRef &ob_ref, Manager &manager) final;
  void end_sync() final;
  void draw(Manager &manager) final;

  /** Draw into the given color/depth textures (used by viewport and F12). */
  void draw_to(Manager &manager, View &view, gpu::Texture *color, gpu::Texture *depth);

 private:
  PassMain::Sub &material_sub_get(const Material *ma);
  PassMain::Sub &gpumat_sub_get(Manager &manager, GPUMaterial *gpumat);
  PassMain::Sub &refract_sub_get(Manager &manager, GPUMaterial *gpumat);
  void configure_material_sub(PassMain::Sub &sub,
                              Manager &manager,
                              GPUMaterial *gpumat,
                              bool is_refract);
};

}  // namespace blender::goo
