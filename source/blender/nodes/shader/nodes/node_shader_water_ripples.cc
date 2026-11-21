/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "node_shader_util.hh"
#include "node_util.hh"

#include "BKE_texture.h"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

namespace blender {

namespace nodes::node_shader_water_ripples_cc {

static void node_shader_init_water_ripples(bNodeTree * /*ntree*/, bNode *node)
{
  NodeWaterRipples *storage = MEM_new<NodeWaterRipples>(__func__);
  BKE_texture_mapping_default(&storage->base.tex_mapping, TEXMAP_TYPE_POINT);
  BKE_texture_colormapping_default(&storage->base.color_mapping);
  storage->mode = NODE_WATER_RIPPLES_DROPS;
  node->storage = storage;
}

static void node_layout(ui::Layout &layout, bContext * /*C*/, PointerRNA *ptr)
{
  ui::Layout &col = layout.column(false);
  col.prop(ptr, "mode", ui::ITEM_R_SPLIT_EMPTY_NAME, std::nullopt, ICON_NONE);
}

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>("Vector"_ustr).default_value({0.0f, 0.0f, 0.0f});
  b.add_input<decl::Float>("Time"_ustr).default_value(0.0f);
  b.add_input<decl::Float>("Mode"_ustr).default_value(0.0f).min(0.0f).max(3.0f).available(false);
  b.add_input<decl::Float>("Scale"_ustr).default_value(1.0f).min(-10.0f).max(10.0f);
  b.add_input<decl::Float>("Intensity"_ustr).default_value(1.0f).min(-10.0f).max(10.0f);
  b.add_input<decl::Float>("Speed"_ustr).default_value(1.0f).min(-5.0f).max(5.0f);
  b.add_input<decl::Float>("Detail"_ustr).default_value(0.5f).min(0.0f).max(1.0f);
  b.add_input<decl::Float>("Bias"_ustr).default_value(0.6f).min(0.01f).max(0.99f);

  b.add_output<decl::Vector>("Distorted Vector"_ustr);
  b.add_output<decl::Float>("Mask"_ustr);
}

static int gpu_shader_water_ripples(GPUMaterial *mat,
                                    bNode *node,
                                    bNodeExecData * /*execdata*/,
                                    GPUNodeStack *in,
                                    GPUNodeStack *out)
{
  return GPU_stack_link(mat, node, "node_water_ripples", in, out);
}

static void node_update(bNodeTree * /*tree*/, bNode *node)
{
  const NodeWaterRipples *storage = static_cast<const NodeWaterRipples *>(node->storage);
  if (storage == nullptr) {
    return;
  }

  bNodeSocket *mode_socket = bke::node_find_socket(*node, SOCK_IN, "Mode"_ustr);
  if (mode_socket && mode_socket->default_value) {
    bNodeSocketValueFloat *mode_val = static_cast<bNodeSocketValueFloat *>(
        mode_socket->default_value);
    mode_val->value = float(storage->mode);
  }
}

}  // namespace nodes::node_shader_water_ripples_cc

void register_node_type_sh_water_ripples()
{
  namespace file_ns = nodes::node_shader_water_ripples_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeWaterRipples"_ustr, SH_NODE_WATER_RIPPLES);
  ntype.ui_name = "Water Ripples";
  ntype.ui_description = "Generate concentric ripples simulating water surface disturbances";
  ntype.enum_name_legacy = "WATER_RIPPLES";
  ntype.nclass = NODE_CLASS_TEXTURE;
  ntype.declare = file_ns::node_declare;
  ntype.gpu_fn = file_ns::gpu_shader_water_ripples;
  ntype.draw_buttons = file_ns::node_layout;
  ntype.initfunc = file_ns::node_shader_init_water_ripples;
  ntype.updatefunc = file_ns::node_update;
  bke::node_type_storage(
      ntype, "NodeWaterRipples", node_free_standard_storage, node_copy_standard_storage);

  bke::node_register_type(ntype);
}

}  // namespace blender
