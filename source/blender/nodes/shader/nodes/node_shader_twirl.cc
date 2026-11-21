/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "node_shader_util.hh"
#include "node_util.hh"

#include "BKE_texture.h"

namespace blender {

namespace nodes::node_shader_twirl_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>("Vector"_ustr).default_value({0.0f, 0.0f, 0.0f});
  b.add_input<decl::Vector>("Center"_ustr).default_value({0.5f, 0.5f, 0.0f}).min(0.0f).max(1.0f);
  b.add_input<decl::Float>("Amount"_ustr).default_value(0.0f).min(-100.0f).max(100.0f);
  b.add_output<decl::Vector>("Vector"_ustr);
}

static void node_init(bNodeTree * /*ntree*/, bNode *node)
{
  NodeTwirl *storage = MEM_new<NodeTwirl>(__func__);
  BKE_texture_mapping_default(&storage->base.tex_mapping, TEXMAP_TYPE_POINT);
  BKE_texture_colormapping_default(&storage->base.color_mapping);
  node->storage = storage;
}

static int gpu_shader_twirl(GPUMaterial *mat,
                            bNode *node,
                            bNodeExecData * /*execdata*/,
                            GPUNodeStack *in,
                            GPUNodeStack *out)
{
  return GPU_stack_link(mat, node, "node_twirl", in, out);
}

}  // namespace nodes::node_shader_twirl_cc

void register_node_type_sh_twirl()
{
  namespace file_ns = nodes::node_shader_twirl_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeTwirl"_ustr, SH_NODE_TWIRL);
  ntype.ui_name = "Twirl";
  ntype.ui_description = "Twirl the input vector around a center point by a specified amount";
  ntype.enum_name_legacy = "TWIRL";
  ntype.nclass = NODE_CLASS_OP_VECTOR;
  ntype.declare = file_ns::node_declare;
  ntype.initfunc = file_ns::node_init;
  bke::node_type_storage(
      ntype, "NodeTwirl", node_free_standard_storage, node_copy_standard_storage);
  ntype.gpu_fn = file_ns::gpu_shader_twirl;

  bke::node_register_type(ntype);
}

}  // namespace blender
