/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Static shader storage. Shaders are shared between all engine instances and freed on exit.
 */

#pragma once

#include "GPU_shader.hh"

namespace blender::goo {

class ShaderModule {
 public:
  gpu::StaticShader surface_flat = {"goo_surface_flat"};

  static ShaderModule &get();
  static void module_free();
};

}  // namespace blender::goo
