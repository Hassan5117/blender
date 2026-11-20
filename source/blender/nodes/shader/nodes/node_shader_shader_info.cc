/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "node_shader_util.hh"
#include "node_util.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

namespace blender {

namespace nodes::node_shader_shader_info_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>("WorldPosition"_ustr).hide_value();
  b.add_input<decl::Vector>("Normal"_ustr).hide_value();

  b.add_output<decl::Color>("Diffuse Shading"_ustr);
  b.add_output<decl::Float>("Cast Shadows"_ustr);
  b.add_output<decl::Float>("Self Shadows"_ustr);
  b.add_output<decl::Color>("Ambient Lighting"_ustr);
  b.add_output<decl::Float>("Half-lambert factor"_ustr);
}

static void node_shader_init_shader_info(bNodeTree * /*ntree*/, bNode *node)
{
  NodeShaderInfo *shinfo = MEM_new<NodeShaderInfo>("NodeShaderInfo");
  node->storage = shinfo;
}

static void node_shader_buts_shader_info(ui::Layout &layout, bContext * /*C*/, PointerRNA *ptr)
{
  layout.prop(
      ptr, "use_own_light_groups", ui::ITEM_R_SPLIT_EMPTY_NAME, IFACE_("Light Groups"), ICON_NONE);
}

static int node_shader_gpu_shader_info(GPUMaterial *mat,
                                       bNode *node,
                                       bNodeExecData * /*execdata*/,
                                       GPUNodeStack *in,
                                       GPUNodeStack *out)
{
  /* Set this to ensure light/shadow evaluation is enabled for the material. */
  GPU_material_flag_set(mat, GPU_MATFLAG_DIFFUSE);

  if (!in[0].link) {
    GPU_link(mat, "world_position_get", &in[0].link);
  }
  if (!in[1].link) {
    GPU_link(mat, "world_normals_get", &in[1].link);
  }

  auto *info = static_cast<NodeShaderInfo *>(node->storage);
  if (info && info->use_own_light_groups) {
    /* GPU_uniform supports floats only; shader reinterprets bits via floatBitsToInt(). */
    return GPU_stack_link(mat,
                          node,
                          "node_shader_info_light_groups",
                          in,
                          out,
                          GPU_uniform(reinterpret_cast<const float *>(info->light_group_bits)),
                          GPU_uniform(
                              reinterpret_cast<const float *>(info->light_group_shadow_bits)));
  }
  return GPU_stack_link(mat, node, "node_shader_info", in, out);
}

}  // namespace nodes::node_shader_shader_info_cc

/* node type definition */
void register_node_type_sh_shader_info()
{
  namespace file_ns = nodes::node_shader_shader_info_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeShaderInfo"_ustr, SH_NODE_SHADER_INFO);
  ntype.ui_name = "Shader Info";
  ntype.ui_description =
      "Separate internal lighting into multiple outputs, and allow for per-node light-groups.";
  ntype.enum_name_legacy = "SHADERINFO";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.draw_buttons = file_ns::node_shader_buts_shader_info;
  ntype.initfunc = file_ns::node_shader_init_shader_info;
  ntype.add_ui_poll = object_eevee_shader_nodes_poll;

  bke::node_type_storage(
      ntype, "NodeShaderInfo", node_free_standard_storage, node_copy_standard_storage);

  ntype.gpu_fn = file_ns::node_shader_gpu_shader_info;

  bke::node_register_type(ntype);
}

}  // namespace blender
