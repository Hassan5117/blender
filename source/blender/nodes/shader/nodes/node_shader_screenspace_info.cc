/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "node_shader_util.hh"

namespace blender {

namespace nodes::node_shader_screenspace_info_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>("View Position"_ustr).hide_value();

  b.add_output<decl::Color>("Scene Color"_ustr);
  b.add_output<decl::Float>("Scene Depth"_ustr);
}

static int node_shader_gpu_screenspace_info(GPUMaterial *mat,
                                            bNode *node,
                                            bNodeExecData * /*execdata*/,
                                            GPUNodeStack *in,
                                            GPUNodeStack *out)
{
  GPU_material_flag_set(mat, GPU_MATFLAG_REFRACT);

  if (!in[0].link) {
    GPU_link(mat, "view_position_get", &in[0].link);
  }

  return GPU_stack_link(mat, node, "node_screenspace_info", in, out);
}

}  // namespace nodes::node_shader_screenspace_info_cc

/* node type definition */
void register_node_type_sh_screenspace_info()
{
  namespace file_ns = nodes::node_shader_screenspace_info_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeScreenspaceInfo"_ustr, SH_NODE_SCREENSPACE_INFO);
  ntype.ui_name = "Screenspace Info";
  ntype.ui_description = "Sample internal colour and depth buffers";
  ntype.enum_name_legacy = "SCREENSPACEINFO";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.add_ui_poll = object_eevee_shader_nodes_poll;
  ntype.gpu_fn = file_ns::node_shader_gpu_screenspace_info;

  bke::node_register_type(ntype);
}

}  // namespace blender
