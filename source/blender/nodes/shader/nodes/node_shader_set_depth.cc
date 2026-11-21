/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "node_shader_util.hh"

namespace blender {

namespace nodes::node_shader_set_depth_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Shader>("Shader"_ustr);
  b.add_input<decl::Float>("View Depth"_ustr).hide_value();
  b.add_output<decl::Shader>("Shader"_ustr);
}

static int node_shader_gpu_set_depth(GPUMaterial *mat,
                                     bNode *node,
                                     bNodeExecData * /*execdata*/,
                                     GPUNodeStack *in,
                                     GPUNodeStack *out)
{
  GPU_material_flag_set(mat, GPU_MATFLAG_SET_DEPTH);

  if (!in[1].link) {
    GPU_link(mat, "goo_view_z_get", &in[1].link);
  }

  return GPU_stack_link(mat, node, "node_set_depth", in, out);
}

}  // namespace nodes::node_shader_set_depth_cc

/* node type definition */
void register_node_type_sh_set_depth()
{
  namespace file_ns = nodes::node_shader_set_depth_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeSetDepth"_ustr, SH_NODE_SET_DEPTH);
  ntype.ui_name = "Set Depth";
  ntype.ui_description = "Pixel depth offset";
  ntype.enum_name_legacy = "SET_DEPTH";
  ntype.nclass = NODE_CLASS_SHADER;
  ntype.declare = file_ns::node_declare;
  ntype.add_ui_poll = object_eevee_shader_nodes_poll;
  ntype.gpu_fn = file_ns::node_shader_gpu_set_depth;

  bke::node_register_type(ntype);
}

}  // namespace blender
