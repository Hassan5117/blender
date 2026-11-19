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

  /** Surface pass. One sub-pass per material color. */
  PassMain surface_ps_ = {"Goo.Surface"};
  Map<const Material *, PassMain::Sub *> material_subs_;

  float4 world_color_ = float4(0.05f, 0.05f, 0.05f, 1.0f);

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
  void draw_to(Manager &manager, View &view, gpu::FrameBuffer *fb);

 private:
  PassMain::Sub &material_sub_get(const Material *ma);
};

}  // namespace blender::goo
