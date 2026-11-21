/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_light_types.h"
#include "DNA_object_types.h"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "node_shader_util.hh"

namespace blender {

namespace nodes::node_shader_light_info_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.add_output<decl::Color>("Light Color"_ustr);
  b.add_output<decl::Float>("Light Power"_ustr);
  b.add_output<decl::Float>("Perceptual Power"_ustr);
}

static void node_shader_buts_light_info(ui::Layout &layout, bContext * /*C*/, PointerRNA *ptr)
{
  layout.prop(ptr, "light_object", ui::ITEM_R_SPLIT_EMPTY_NAME, IFACE_("Light"), ICON_LIGHT);
}

static int node_shader_gpu_light_info(GPUMaterial *mat,
                                      bNode *node,
                                      bNodeExecData * /*execdata*/,
                                      GPUNodeStack *in,
                                      GPUNodeStack *out)
{
  Object *ob = id_cast<Object *>(node->id);

  float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  float power = 1.0f;

  if (ob && ob->type == OB_LAMP && ob->data != nullptr) {
    const Light *light = id_cast<const Light *>(ob->data);
    color[0] = light->r;
    color[1] = light->g;
    color[2] = light->b;
    power = light->energy;
  }

  return GPU_stack_link(mat,
                        node,
                        "node_light_info_simple",
                        in,
                        out,
                        GPU_constant(color),
                        GPU_constant(&power));
}

}  // namespace nodes::node_shader_light_info_cc

/* node type definition */
void register_node_type_sh_light_info()
{
  namespace file_ns = nodes::node_shader_light_info_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeLightInfo"_ustr, SH_NODE_LIGHT_INFO);
  ntype.ui_name = "Light Info";
  ntype.ui_description = "Get information from a selected light object";
  ntype.enum_name_legacy = "LIGHT_INFO";
  ntype.nclass = NODE_CLASS_INPUT;
  ntype.declare = file_ns::node_declare;
  ntype.draw_buttons = file_ns::node_shader_buts_light_info;
  ntype.add_ui_poll = object_eevee_shader_nodes_poll;
  ntype.gpu_fn = file_ns::node_shader_gpu_light_info;

  bke::node_register_type(ntype);
}

}  // namespace blender
