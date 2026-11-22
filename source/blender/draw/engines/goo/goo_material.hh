/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Node-tree materials for the Goo engine.
 *
 * Replaces Goo 4.4's `EEVEE_material_get` / `eevee_material_post_eval` (which went through the
 * legacy string based `DRW_shader_library` + `GPU_MAT_EEVEE_LEGACY`). On 5.x, materials are built by
 * `GPU_material_from_nodetree` with a codegen callback that amends the generated create-info with
 * Goo's own surface shader and closure library.
 */

#pragma once

#include "BLI_map.hh"

#include "GPU_material.hh"

namespace blender {
struct Material;
struct World;
}  // namespace blender

namespace blender::goo {

class MaterialModule {
 private:
  World *default_world_ = nullptr;

 public:
  ~MaterialModule();

  /**
   * Return a compiled material for `ma`, or nullptr if it is still compiling or failed
   * (caller falls back to the flat default shader).
   */
  GPUMaterial *surface_get(Material *ma, bool deferred_compilation);

  /**
   * Return a compiled depth-prepass material for `ma` (`CLIP` / `HASHED` alpha cutouts), or
   * nullptr if it is still compiling or failed.
   */
  GPUMaterial *prepass_get(Material *ma, bool deferred_compilation);

  /**
   * Return a compiled shadow caster material for `ma` (`CLIP` / `HASHED` alpha cutouts), or
   * nullptr if it is still compiling or failed.
   */
  GPUMaterial *shadow_get(Material *ma, bool deferred_compilation);

  /** Curves / hair surface, prepass, and shadow material variants. */
  GPUMaterial *curves_surface_get(Material *ma, bool deferred_compilation);
  GPUMaterial *curves_prepass_get(Material *ma, bool deferred_compilation);
  GPUMaterial *curves_shadow_get(Material *ma, bool deferred_compilation);

  /**
   * Return a compiled world background material for `wo` (or a default world if `wo` is null),
   * or nullptr if it is still compiling or failed.
   */
  GPUMaterial *world_get(World *wo, bool deferred_compilation);

  /**
   * Return a compiled world volume shader if `wo` has a volume output, or nullptr otherwise.
   */
  GPUMaterial *world_volume_get(World *wo, bool deferred_compilation);

  /**
   * Return a compiled object volume shader if `ma` has a volume output, or nullptr otherwise.
   */
  GPUMaterial *object_volume_get(Material *ma, bool deferred_compilation);

  /** Number of materials still compiling this sync (used to redraw the viewport). */
  int queued_count = 0;

  void begin_sync()
  {
    queued_count = 0;
  }
};

}  // namespace blender::goo
