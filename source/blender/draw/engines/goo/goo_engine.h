/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Goo Engine: EEVEE-Legacy based NPR engine, ported onto the modern draw manager.
 */

#pragma once

#include "DRW_render.hh"
#include "RE_engine.h"

namespace blender {

extern RenderEngineType DRW_engine_viewport_goo_type;

namespace goo {

struct Engine : public DrawEngine::Pointer {
  DrawEngine *create_instance() final;

  static void free_static();
};

}  // namespace goo
}  // namespace blender
