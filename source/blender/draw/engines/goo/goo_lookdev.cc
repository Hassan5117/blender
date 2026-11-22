/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include <cmath>

#include "BLI_listbase.h"
#include "BLI_math_base.h"
#include "BLI_string.h"
#include "BLI_utildefines.h"

#include "BKE_image.hh"
#include "BKE_lib_id.hh"
#include "BKE_node.hh"
#include "BKE_node_legacy_types.hh"
#include "BKE_studiolight.h"

#include "DNA_view3d_types.h"

#include "GPU_material.hh"
#include "GPU_texture.hh"

#include "IMB_colormanagement.hh"

#include "NOD_shader.h"

#include "goo_lookdev.hh"

namespace blender::goo {

LookdevParameters::LookdevParameters()
{
  working_space = IMB_colormanagement_working_space_get();
}

LookdevParameters::LookdevParameters(const View3D *v3d)
{
  working_space = IMB_colormanagement_working_space_get();

  if (v3d == nullptr) {
    return;
  }

  const View3DShading &shading = v3d->shading;
  show_scene_world = (shading.type == OB_RENDER) ?
                         ((shading.flag & V3D_SHADING_SCENE_WORLD_RENDER) != 0) :
                         ((shading.flag & V3D_SHADING_SCENE_WORLD) != 0);
  if (!show_scene_world) {
    rot_z = shading.studiolight_rot_z;
    background_opacity = shading.studiolight_background;
    blur = shading.studiolight_blur;
    intensity = shading.studiolight_intensity;
    hdri = StringRefNull(shading.lookdev_light);
    camera_space = (shading.flag & V3D_SHADING_STUDIOLIGHT_VIEW_ROTATION) != 0;
  }
}

bool LookdevParameters::operator==(const LookdevParameters &other) const
{
  return hdri == other.hdri && working_space == other.working_space && rot_z == other.rot_z &&
         background_opacity == other.background_opacity && blur == other.blur &&
         intensity == other.intensity && show_scene_world == other.show_scene_world &&
         camera_space == other.camera_space;
}

bool LookdevParameters::operator!=(const LookdevParameters &other) const
{
  return !(*this == other);
}

LookdevWorld::LookdevWorld()
{
  world_ = BKE_id_new_nomain<World>("Goo Lookdev");

  using namespace bke;
  bNodeTree &ntree = *world_->nodetree;
  BLI_strncpy(ntree.id.name + 2, "Goo Lookdev World", MAX_NAME - 2);

  bNode &coordinate = *node_add_static_node(nullptr, ntree, SH_NODE_TEX_COORD);
  bNodeSocket &generated_sock = *node_find_socket(coordinate, SOCK_OUT, "Generated"_ustr);

  bNode &transform = *node_add_static_node(nullptr, ntree, SH_NODE_VECT_TRANSFORM);
  bNodeSocket &transform_in = *node_find_socket(transform, SOCK_IN, "Vector"_ustr);
  bNodeSocket &transform_out = *node_find_socket(transform, SOCK_OUT, "Vector"_ustr);
  NodeShaderVectTransform &nodeprop = *static_cast<NodeShaderVectTransform *>(transform.storage);
  nodeprop.convert_from = SHD_VECT_TRANSFORM_SPACE_WORLD;
  xform_socket_ = &nodeprop.convert_to;
  node_add_link(ntree, coordinate, generated_sock, transform, transform_in);

  bNode &flip_y_mul = *node_add_static_node(nullptr, ntree, SH_NODE_VECTOR_MATH);
  flip_y_mul.custom1 = NODE_VECTOR_MATH_MULTIPLY;
  auto &flip_y_value_out = *node_find_socket(flip_y_mul, SOCK_OUT, "Vector"_ustr);
  auto &flip_y_value_in0 = *static_cast<bNodeSocket *>(BLI_findlink(&flip_y_mul.inputs, 0));
  auto &flip_y_value_in1 = *static_cast<bNodeSocket *>(BLI_findlink(&flip_y_mul.inputs, 1));
  flip_y_socket_ = static_cast<bNodeSocketValueVector *>(flip_y_value_in1.default_value);
  flip_y_socket_->value[0] = 1.0f;
  flip_y_socket_->value[1] = 1.0f;
  flip_y_socket_->value[2] = 1.0f;
  node_add_link(ntree, transform, transform_out, flip_y_mul, flip_y_value_in0);

  bNode &rotate_x = *node_add_static_node(nullptr, ntree, SH_NODE_VECTOR_ROTATE);
  rotate_x.custom1 = NODE_VECTOR_ROTATE_TYPE_AXIS_X;
  auto &rotate_x_vector_in = *node_find_socket(rotate_x, SOCK_IN, "Vector"_ustr);
  auto &rotate_x_vector_angle = *node_find_socket(rotate_x, SOCK_IN, "Angle"_ustr);
  auto &rotate_x_out = *node_find_socket(rotate_x, SOCK_OUT, "Vector"_ustr);
  rotation_x_socket_ =
      &static_cast<bNodeSocketValueFloat *>(rotate_x_vector_angle.default_value)->value;
  node_add_link(ntree, flip_y_mul, flip_y_value_out, rotate_x, rotate_x_vector_in);

  bNode &rotate_z = *node_add_static_node(nullptr, ntree, SH_NODE_VECTOR_ROTATE);
  rotate_z.custom1 = NODE_VECTOR_ROTATE_TYPE_AXIS_Z;
  auto &rotate_z_vector_in = *node_find_socket(rotate_z, SOCK_IN, "Vector"_ustr);
  auto &rotate_z_vector_angle = *node_find_socket(rotate_z, SOCK_IN, "Angle"_ustr);
  auto &rotate_z_out = *node_find_socket(rotate_z, SOCK_OUT, "Vector"_ustr);
  angle_socket_ = static_cast<bNodeSocketValueFloat *>(rotate_z_vector_angle.default_value);
  node_add_link(ntree, rotate_x, rotate_x_out, rotate_z, rotate_z_vector_in);

  bNode &environment = *node_add_static_node(nullptr, ntree, SH_NODE_TEX_ENVIRONMENT);
  environment_node_ = &environment;
  NodeTexImage *environment_storage = static_cast<NodeTexImage *>(environment.storage);
  auto &environment_vector_in = *node_find_socket(environment, SOCK_IN, "Vector"_ustr);
  auto &environment_out = *node_find_socket(environment, SOCK_OUT, "Color"_ustr);
  /* Goo's world probe captures LookDev directly without a separate sampling rotation matrix,
   * so feed the rotated coordinates for both background and probe capture passes. */
  node_add_link(ntree, rotate_z, rotate_z_out, environment, environment_vector_in);

  bNode &background = *node_add_static_node(nullptr, ntree, SH_NODE_BACKGROUND);
  auto &background_out = *node_find_socket(background, SOCK_OUT, "Background"_ustr);
  auto &background_color_in = *node_find_socket(background, SOCK_IN, "Color"_ustr);
  intensity_socket_ = static_cast<bNodeSocketValueFloat *>(
      node_find_socket(background, SOCK_IN, "Strength"_ustr)->default_value);
  node_add_link(ntree, environment, environment_out, background, background_color_in);

  bNode &output = *node_add_static_node(nullptr, ntree, SH_NODE_OUTPUT_WORLD);
  auto &output_in = *node_find_socket(output, SOCK_IN, "Surface"_ustr);
  node_add_link(ntree, background, background_out, output, output_in);
  node_set_active(ntree, output);

  image_ = BKE_id_new_nomain<Image>("Goo Lookdev HDRI");
  image_->type = IMA_TYPE_IMAGE;
  image_->source = IMA_SRC_GENERATED;
  ImageTile *base_tile = BKE_image_get_tile(image_, 0);
  base_tile->gen_x = 1;
  base_tile->gen_y = 1;
  base_tile->gen_type = IMA_GENTYPE_BLANK;
  copy_v4_fl(base_tile->gen_color, 0.0f);
  BKE_image_get_gpu_texture(image_, &environment_storage->iuser);
}

LookdevWorld::~LookdevWorld()
{
  if (image_ != nullptr) {
    BKE_id_free(nullptr, &image_->id);
  }
  if (world_ != nullptr) {
    BKE_id_free(nullptr, &world_->id);
  }
}

bool LookdevWorld::sync(const LookdevParameters &new_parameters)
{
  const bool parameters_changed = !initialized_ || (parameters_ != new_parameters);
  if (parameters_changed) {
    initialized_ = true;
    parameters_ = new_parameters;
    intensity_socket_->value = parameters_.intensity;

    GPU_TEXTURE_FREE_SAFE(image_->runtime->gputexture[TEXTARGET_2D][0]);
    environment_node_->id = nullptr;

    StudioLight *sl = BKE_studiolight_find(parameters_.hdri.c_str(),
                                           STUDIOLIGHT_ORIENTATIONS_MATERIAL_MODE);
    if (sl != nullptr) {
      BKE_studiolight_ensure_flag(sl, STUDIOLIGHT_EQUIRECT_RADIANCE_GPUTEXTURE);
      gpu::Texture *texture = sl->equirect_radiance_gputexture;
      if (texture != nullptr) {
        GPU_texture_ref(texture);
        image_->runtime->gputexture[TEXTARGET_2D][0] = texture;
        environment_node_->id = &image_->id;
      }
    }

    if (parameters_.camera_space) {
      *xform_socket_ = SHD_VECT_TRANSFORM_SPACE_CAMERA;
      flip_y_socket_->value[0] = 1.0f;
      flip_y_socket_->value[1] = -1.0f;
      flip_y_socket_->value[2] = 1.0f;
      *rotation_x_socket_ = float(-M_PI / 2.0);
    }
    else {
      *xform_socket_ = SHD_VECT_TRANSFORM_SPACE_WORLD;
      flip_y_socket_->value[0] = 1.0f;
      flip_y_socket_->value[1] = 1.0f;
      flip_y_socket_->value[2] = 1.0f;
      *rotation_x_socket_ = 0.0f;
    }
  }

  const bool rotation_changed = assign_if_different(angle_socket_->value, new_parameters.rot_z);

  if (rotation_changed || parameters_changed) {
    GPU_material_free(&world_->gpumaterial);
  }
  return parameters_changed || rotation_changed;
}

}  // namespace blender::goo
