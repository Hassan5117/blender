/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "BLI_string_utf8.h"

#include "BLT_translation.hh"

#include "RNA_access.hh"
#include "RNA_enum_types.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "node_shader_util.hh"
#include "node_util.hh"

namespace blender {

namespace nodes::node_shader_sdf_vector_op_cc {

static void sh_node_sdf_vector_op_declare(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Vector>("Vector1"_ustr).hide_value();
  b.add_input<decl::Vector>("Vector2"_ustr).no_muted_links();
  b.add_input<decl::Vector>("Vector3"_ustr).no_muted_links();
  b.add_input<decl::Float>("Scale"_ustr).min(-100000.0f).max(100000.0f).default_value(1.0f);
  b.add_input<decl::Float>("Value1"_ustr).min(-100000.0f).max(100000.0f).default_value(0.0f);
  b.add_input<decl::Float>("Value2"_ustr).min(-100000.0f).max(100000.0f).default_value(0.0f);
  b.add_input<decl::Float>("Angle"_ustr).subtype(PROP_ANGLE);
  b.add_input<decl::Int>("Count1"_ustr).min(0).max(127).default_value(4);
  b.add_input<decl::Int>("Count2"_ustr).min(0).max(127).default_value(4);

  b.add_output<decl::Vector>("Vector"_ustr);
  b.add_output<decl::Vector>("Position"_ustr);
  b.add_output<decl::Float>("Value"_ustr);
}

static const char *node_shader_sdf_vector_op_get_name(int mode)
{
  switch (mode) {
    case SHD_SDF_VEC_OP_SPIN:
      return "node_sdf_vector_op_spin";
    case SHD_SDF_VEC_OP_SWIZZLE:
      return "node_sdf_vector_op_swizzle";
    case SHD_SDF_VEC_OP_EXTRUDE:
      return "node_sdf_vector_op_extrude";
    case SHD_SDF_VEC_OP_TWIST:
      return "node_sdf_vector_op_twist";
    case SHD_SDF_VEC_OP_SWIRL:
      return "node_sdf_vector_op_swirl";
    case SHD_SDF_VEC_OP_RADIAL_SHEAR:
      return "node_sdf_vector_op_radial_shear";
    case SHD_SDF_VEC_OP_PINCH_INFLATE:
      return "node_sdf_vector_op_pinch_inflate";
    case SHD_SDF_VEC_OP_BEND:
      return "node_sdf_vector_op_bend";
    case SHD_SDF_VEC_OP_REPEAT_FINITE:
      return "node_sdf_vector_op_repeat";
    case SHD_SDF_VEC_OP_REPEAT_INF:
      return "node_sdf_vector_op_repeat_inf";
    case SHD_SDF_VEC_OP_REPEAT_INF_MIRROR:
      return "node_sdf_vector_op_repeat_inf_mirror";
    case SHD_SDF_VEC_OP_ROTATE:
      return "node_sdf_vector_op_rotate";
    case SHD_SDF_VEC_OP_REFLECT:
      return "node_sdf_vector_op_reflect";
    case SHD_SDF_VEC_OP_MIRROR:
      return "node_sdf_vector_op_mirror";
    case SHD_SDF_VEC_OP_POLAR:
      return "node_sdf_vector_op_polar";
    case SHD_SDF_VEC_OP_MAP_UV:
      return "node_sdf_vector_op_map_uv";
    case SHD_SDF_VEC_OP_MAP_11:
      return "node_sdf_vector_op_map_11";
    case SHD_SDF_VEC_OP_MAP_05:
      return "node_sdf_vector_op_map_05";
    case SHD_SDF_VEC_OP_ROTATE_UV:
      return "node_sdf_vector_op_uv_rotate";
    case SHD_SDF_VEC_OP_SCALE_UV:
      return "node_sdf_vector_op_uv_scale";
    case SHD_SDF_VEC_OP_RND_UV:
      return "node_sdf_vector_op_random_uv_rotate";
    case SHD_SDF_VEC_OP_RND_UV_FLIP:
      return "node_sdf_vector_op_random_uv_flip";
    case SHD_SDF_VEC_OP_OCTANT:
      return "node_sdf_vector_op_octant";
    case SHD_SDF_VEC_OP_TILESET:
      return "node_sdf_vector_op_tileset";
    case SHD_SDF_VEC_OP_GRID:
      return "node_sdf_vector_op_grid";
  }

  return nullptr;
}

static int node_shader_gpu_sdf_vector_op(GPUMaterial *mat,
                                         bNode *node,
                                         bNodeExecData * /*execdata*/,
                                         GPUNodeStack *in,
                                         GPUNodeStack *out)
{
  const NodeSdfVectorOp *sdf = static_cast<const NodeSdfVectorOp *>(node->storage);
  const char *name = node_shader_sdf_vector_op_get_name(sdf->operation);
  if (name != nullptr) {
    const float axis = float(sdf->axis);
    return GPU_stack_link(mat, node, name, in, out, GPU_constant(&axis));
  }
  return 0;
}

static void node_shader_label_sdf_vector_op(const bNodeTree * /*ntree*/,
                                            const bNode *node,
                                            char *label,
                                            int label_maxncpy)
{
  const NodeSdfVectorOp *node_storage = static_cast<const NodeSdfVectorOp *>(node->storage);
  const char *name;
  const bool enum_label = RNA_enum_name(
      rna_enum_node_sdf_vector_op_items, node_storage->operation, &name);
  if (!enum_label) {
    name = N_("Unknown SDF Vector Op");
  }
  BLI_strncpy_utf8(label, IFACE_(name), label_maxncpy);
}

static void node_shader_update_sdf_vector_op(bNodeTree *ntree, bNode *node)
{
  const NodeSdfVectorOp *sdf = static_cast<const NodeSdfVectorOp *>(node->storage);

  Span<bNodeSocket *> inputs = node->input_sockets();
  Span<bNodeSocket *> outputs = node->output_sockets();

  bNodeSocket *sock_vector1 = inputs[0];
  bNodeSocket *sock_vector2 = inputs[1];
  bNodeSocket *sock_vector3 = inputs[2];
  bNodeSocket *sock_scale = inputs[3];
  bNodeSocket *sock_value = inputs[4];
  bNodeSocket *sock_value2 = inputs[5];
  bNodeSocket *sock_angle = inputs[6];
  bNodeSocket *sock_count = inputs[7];
  bNodeSocket *sock_count2 = inputs[8];

  bNodeSocket *sock_vector_out = outputs[0];
  bNodeSocket *sock_position_out = outputs[1];
  bNodeSocket *sock_value_out = outputs[2];

  bke::node_set_socket_availability(*ntree,
                                    *sock_value_out,
                                    ELEM(sdf->operation,
                                         SHD_SDF_VEC_OP_MIRROR,
                                         SHD_SDF_VEC_OP_REFLECT,
                                         SHD_SDF_VEC_OP_POLAR,
                                         SHD_SDF_VEC_OP_EXTRUDE));
  bke::node_set_socket_availability(
      *ntree, *sock_position_out, ELEM(sdf->operation, SHD_SDF_VEC_OP_MIRROR, SHD_SDF_VEC_OP_GRID));

  bke::node_set_socket_availability(*ntree,
                                    *sock_vector2,
                                    ELEM(sdf->operation,
                                         SHD_SDF_VEC_OP_SWIRL,
                                         SHD_SDF_VEC_OP_RADIAL_SHEAR,
                                         SHD_SDF_VEC_OP_PINCH_INFLATE,
                                         SHD_SDF_VEC_OP_ROTATE_UV,
                                         SHD_SDF_VEC_OP_GRID,
                                         SHD_SDF_VEC_OP_RND_UV,
                                         SHD_SDF_VEC_OP_RND_UV_FLIP,
                                         SHD_SDF_VEC_OP_MIRROR,
                                         SHD_SDF_VEC_OP_EXTRUDE,
                                         SHD_SDF_VEC_OP_REFLECT,
                                         SHD_SDF_VEC_OP_REPEAT_FINITE,
                                         SHD_SDF_VEC_OP_REPEAT_INF,
                                         SHD_SDF_VEC_OP_REPEAT_INF_MIRROR));
  bke::node_set_socket_availability(*ntree,
                                    *sock_vector3,
                                    ELEM(sdf->operation,
                                         SHD_SDF_VEC_OP_SWIRL,
                                         SHD_SDF_VEC_OP_RADIAL_SHEAR,
                                         SHD_SDF_VEC_OP_REPEAT_FINITE));

  bke::node_set_socket_availability(
      *ntree, *sock_scale, ELEM(sdf->operation, SHD_SDF_VEC_OP_SCALE_UV, SHD_SDF_VEC_OP_TILESET));
  bke::node_set_socket_availability(*ntree,
                                    *sock_value,
                                    !ELEM(sdf->operation,
                                          SHD_SDF_VEC_OP_MAP_11,
                                          SHD_SDF_VEC_OP_MAP_05,
                                          SHD_SDF_VEC_OP_MAP_UV,
                                          SHD_SDF_VEC_OP_RND_UV_FLIP,
                                          SHD_SDF_VEC_OP_ROTATE_UV,
                                          SHD_SDF_VEC_OP_RND_UV,
                                          SHD_SDF_VEC_OP_SCALE_UV,
                                          SHD_SDF_VEC_OP_EXTRUDE,
                                          SHD_SDF_VEC_OP_GRID,
                                          SHD_SDF_VEC_OP_MIRROR,
                                          SHD_SDF_VEC_OP_ROTATE,
                                          SHD_SDF_VEC_OP_SWIZZLE,
                                          SHD_SDF_VEC_OP_BEND,
                                          SHD_SDF_VEC_OP_REPEAT_FINITE,
                                          SHD_SDF_VEC_OP_REPEAT_INF,
                                          SHD_SDF_VEC_OP_REPEAT_INF_MIRROR));
  bke::node_set_socket_availability(
      *ntree,
      *sock_value2,
      ELEM(sdf->operation, SHD_SDF_VEC_OP_TILESET, SHD_SDF_VEC_OP_PINCH_INFLATE));
  bke::node_set_socket_availability(*ntree,
                                    *sock_angle,
                                    ELEM(sdf->operation,
                                         SHD_SDF_VEC_OP_BEND,
                                         SHD_SDF_VEC_OP_TWIST,
                                         SHD_SDF_VEC_OP_ROTATE,
                                         SHD_SDF_VEC_OP_ROTATE_UV));
  bke::node_set_socket_availability(
      *ntree, *sock_count, ELEM(sdf->operation, SHD_SDF_VEC_OP_TILESET));
  bke::node_set_socket_availability(
      *ntree, *sock_count2, ELEM(sdf->operation, SHD_SDF_VEC_OP_TILESET));

  node_sock_label_clear(sock_value);
  node_sock_label_clear(sock_value2);
  node_sock_label_clear(sock_value_out);
  node_sock_label_clear(sock_vector1);
  node_sock_label_clear(sock_vector2);
  node_sock_label_clear(sock_vector_out);
  node_sock_label(sock_vector1, "Vector");
  node_sock_label(sock_vector2, "Vector");
  sock_vector2->flag &= ~SOCK_HIDE_VALUE;

  switch (sdf->operation) {
    case SHD_SDF_VEC_OP_GRID:
      node_sock_label(sock_vector2, "Scale");
      node_sock_label(sock_vector_out, "UVW");
      break;
    case SHD_SDF_VEC_OP_ROTATE_UV:
      node_sock_label(sock_vector2, "Center");
      node_sock_label(sock_vector1, "UV");
      node_sock_label(sock_vector_out, "UV");
      break;
    case SHD_SDF_VEC_OP_MAP_UV:
      node_sock_label(sock_vector_out, "UVW");
      break;
    case SHD_SDF_VEC_OP_MAP_11:
    case SHD_SDF_VEC_OP_MAP_05:
      node_sock_label(sock_vector1, "UV");
      node_sock_label(sock_vector_out, "Vector");
      break;
    case SHD_SDF_VEC_OP_TILESET:
      node_sock_label(sock_value, "Index");
      node_sock_label(sock_value2, "Padding");
      node_sock_label(sock_vector1, "UV");
      node_sock_label(sock_vector_out, "UV");
      sock_vector2->flag |= SOCK_HIDE_VALUE;
      break;
    case SHD_SDF_VEC_OP_RND_UV:
    case SHD_SDF_VEC_OP_RND_UV_FLIP:
      node_sock_label(sock_value, "Padding");
      node_sock_label(sock_vector1, "UV");
      node_sock_label(sock_vector2, "Position");
      node_sock_label(sock_vector_out, "UV");
      sock_vector2->flag |= SOCK_HIDE_VALUE;
      break;
    case SHD_SDF_VEC_OP_REPEAT_FINITE:
      node_sock_label(sock_vector2, "Spacing");
      node_sock_label(sock_vector3, "Count");
      break;
    case SHD_SDF_VEC_OP_REPEAT_INF_MIRROR:
    case SHD_SDF_VEC_OP_REPEAT_INF:
    case SHD_SDF_VEC_OP_MIRROR:
      node_sock_label(sock_vector2, "Spacing");
      break;
    case SHD_SDF_VEC_OP_REFLECT:
      node_sock_label(sock_value, "Offset");
      node_sock_label(sock_vector2, "Normal");
      node_sock_label(sock_value_out, "Mask");
      break;
    case SHD_SDF_VEC_OP_POLAR:
      node_sock_label(sock_value, "Count");
      node_sock_label(sock_value_out, "Mask");
      break;
    case SHD_SDF_VEC_OP_EXTRUDE:
      node_sock_label(sock_value_out, "Internal Distance");
      break;
    case SHD_SDF_VEC_OP_TWIST:
      node_sock_label(sock_value, "Twist");
      break;
    case SHD_SDF_VEC_OP_SWIRL:
    case SHD_SDF_VEC_OP_RADIAL_SHEAR:
    case SHD_SDF_VEC_OP_PINCH_INFLATE:
      node_sock_label(sock_value, "Strength");
      node_sock_label(sock_value2, "Radius");
      node_sock_label(sock_vector2, "Center");
      node_sock_label(sock_vector3, "Offset");
      break;
    case SHD_SDF_VEC_OP_SPIN:
      node_sock_label(sock_value, "Offset");
      break;
  }
}

static void node_shader_init_sdf_vector_op(bNodeTree * /*ntree*/, bNode *node)
{
  NodeSdfVectorOp *sdf = MEM_new<NodeSdfVectorOp>(__func__);
  sdf->operation = SHD_SDF_VEC_OP_GRID;
  sdf->axis = SHD_SDF_AXIS_XYZ;
  node->storage = sdf;
}

static void node_shader_buts_sdf_vector_op(ui::Layout &layout, bContext * /*C*/, PointerRNA *ptr)
{
  layout.prop(ptr, "operation", UI_ITEM_NONE, "", ICON_NONE);
  const int type = RNA_enum_get(ptr, "operation");
  if (ELEM(type,
           SHD_SDF_VEC_OP_ROTATE_UV,
           SHD_SDF_VEC_OP_OCTANT,
           SHD_SDF_VEC_OP_GRID,
           SHD_SDF_VEC_OP_TWIST,
           SHD_SDF_VEC_OP_SWIRL,
           SHD_SDF_VEC_OP_RADIAL_SHEAR,
           SHD_SDF_VEC_OP_MIRROR,
           SHD_SDF_VEC_OP_SWIZZLE,
           SHD_SDF_VEC_OP_ROTATE,
           SHD_SDF_VEC_OP_POLAR,
           SHD_SDF_VEC_OP_BEND,
           SHD_SDF_VEC_OP_SPIN,
           SHD_SDF_VEC_OP_EXTRUDE))
  {
    layout.prop(ptr, "axis", UI_ITEM_NONE, "", ICON_NONE);
  }
}

}  // namespace nodes::node_shader_sdf_vector_op_cc

void register_node_type_sh_sdf_vector_op()
{
  namespace file_ns = nodes::node_shader_sdf_vector_op_cc;

  static bke::bNodeType ntype;

  sh_node_type_base(&ntype, "ShaderNodeSdfVectorOp"_ustr, SH_NODE_SDF_VECTOR_OP);
  ntype.ui_name = "Sdf Vector Operator";
  ntype.enum_name_legacy = "SDF_VECTOR_OP";
  ntype.nclass = NODE_CLASS_OP_VECTOR;
  ntype.declare = file_ns::sh_node_sdf_vector_op_declare;
  bke::node_type_storage(
      ntype, "NodeSdfVectorOp", node_free_standard_storage, node_copy_standard_storage);
  ntype.gpu_fn = file_ns::node_shader_gpu_sdf_vector_op;
  ntype.initfunc = file_ns::node_shader_init_sdf_vector_op;
  ntype.labelfunc = file_ns::node_shader_label_sdf_vector_op;
  ntype.updatefunc = file_ns::node_shader_update_sdf_vector_op;
  ntype.draw_buttons = file_ns::node_shader_buts_sdf_vector_op;

  bke::node_register_type(ntype);
}

}  // namespace blender
