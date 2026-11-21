/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Viewport LookDev studio HDRI world override support.
 */

#pragma once

#include <string>

#include "DNA_image_types.h"
#include "DNA_world_types.h"

namespace blender {

struct bNode;
struct bNodeSocketValueFloat;
struct bNodeSocketValueVector;
struct View3D;

namespace goo {

struct LookdevParameters {
  std::string hdri;
  std::string working_space;
  float rot_z = 0.0f;
  float background_opacity = 0.0f;
  float intensity = 1.0f;
  float blur = 0.0f;
  bool show_scene_world = true;
  bool camera_space = true;

  LookdevParameters();
  explicit LookdevParameters(const View3D *v3d);

  bool operator==(const LookdevParameters &other) const;
  bool operator!=(const LookdevParameters &other) const;
};

class LookdevWorld {
 private:
  bNode *environment_node_ = nullptr;
  bNodeSocketValueFloat *intensity_socket_ = nullptr;
  bNodeSocketValueFloat *angle_socket_ = nullptr;
  bNodeSocketValueVector *flip_y_socket_ = nullptr;
  int *xform_socket_ = nullptr;
  float *rotation_x_socket_ = nullptr;
  blender::Image *image_ = nullptr;
  blender::World *world_ = nullptr;

  LookdevParameters parameters_;
  bool initialized_ = false;

 public:
  LookdevWorld();
  ~LookdevWorld();

  /** Returns true if HDRI / transform / intensity parameters changed. */
  bool sync(const LookdevParameters &new_parameters);

  World *world_get()
  {
    return world_;
  }
  float background_opacity_get() const
  {
    return parameters_.background_opacity;
  }
  float background_blur_get() const
  {
    return parameters_.blur;
  }
  float intensity_get() const
  {
    return parameters_.intensity;
  }
};

}  // namespace goo
}  // namespace blender
