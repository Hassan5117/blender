/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_texture_types.h"

#include "BKE_colorband.hh"

#include "BLI_color_types.hh"
#include "BLI_math_base.h"

#include "NOD_multi_function.hh"

#include "node_shader_util.hh"
#include "node_util.hh"

namespace blender {

namespace nodes::node_shader_oklab_color_ramp_cc {

static void sh_node_oklab_valtorgb_declare(NodeDeclarationBuilder &b)
{
  b.is_function_node();
  b.add_input<decl::Float>("Factor"_ustr, "Fac"_ustr)
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description(
          "The value used to map onto the color gradient using OKLab color space for better "
          "perceptual interpolation");
  b.add_output<decl::Color>("Color"_ustr);
  b.add_output<decl::Float>("Alpha"_ustr);
}

static void node_shader_init_oklab_valtorgb(bNodeTree * /*ntree*/, bNode *node)
{
  node->storage = BKE_colorband_add(true);
}

static int gpu_shader_oklab_valtorgb(GPUMaterial *mat,
                                     bNode *node,
                                     bNodeExecData * /*execdata*/,
                                     GPUNodeStack *in,
                                     GPUNodeStack *out)
{
  ColorBand *coba = static_cast<ColorBand *>(node->storage);
  if (coba == nullptr || coba->tot == 0) {
    return 0;
  }

  if (coba->tot == 1) {
    return GPU_stack_link(mat,
                          node,
                          "oklab_valtorgb_opti_constant",
                          in,
                          out,
                          GPU_constant(&coba->data[0].pos),
                          GPU_uniform(&coba->data[0].r),
                          GPU_uniform(&coba->data[0].r));
  }

  if (coba->tot == 2) {
    const float span = coba->data[1].pos - coba->data[0].pos;
    if (span > 1e-6f) {
      float mul_bias[2];
      mul_bias[0] = 1.0f / span;
      mul_bias[1] = -mul_bias[0] * coba->data[0].pos;
      return GPU_stack_link(mat,
                            node,
                            "oklab_valtorgb_opti_ease",
                            in,
                            out,
                            GPU_uniform(mul_bias),
                            GPU_uniform(&coba->data[0].r),
                            GPU_uniform(&coba->data[1].r));
    }
  }

  float *array, layer;
  int size;
  BKE_colorband_evaluate_table_rgba_oklab(coba, &array, &size);
  GPUNodeLink *tex = GPU_color_band(mat, size, array, &layer);

  return GPU_stack_link(mat, node, "oklab_valtorgb", in, out, tex, GPU_constant(&layer));
}

class OKLabColorBandFunction : public mf::MultiFunction {
 private:
  std::shared_ptr<const bNodeTree> tree_;
  const ColorBand &color_band_;

 public:
  OKLabColorBandFunction(const ColorBand &color_band, std::shared_ptr<const bNodeTree> tree)
      : tree_(tree), color_band_(color_band)
  {
    static const mf::Signature signature = []() {
      mf::Signature signature;
      mf::SignatureBuilder builder{"OKLab Color Band", signature};
      builder.single_input<float>("Value");
      builder.single_output<ColorGeometry4f>("Color");
      builder.single_output<float>("Alpha");
      return signature;
    }();
    this->set_signature(&signature);
  }

  void call(const IndexMask &mask, mf::Params params, mf::Context /*context*/) const override
  {
    const VArray<float> &values = params.readonly_single_input<float>(0, "Value");
    MutableSpan<ColorGeometry4f> colors = params.uninitialized_single_output<ColorGeometry4f>(
        1, "Color");
    MutableSpan<float> alphas = params.uninitialized_single_output<float>(2, "Alpha");

    ColorBand coba_ease = color_band_;
    coba_ease.ipotype = COLBAND_INTERP_EASE;

    mask.foreach_index([&](const int64_t i) {
      ColorGeometry4f color;
      BKE_colorband_evaluate_oklab(&coba_ease, values[i], color);
      colors[i] = color;
      alphas[i] = color.a;
    });
  }

  void hash_unique(UniqueHashBytes &hash) const override
  {
    static constexpr int8_t id = 1;
    hash.add(&id);
    hash.add(&color_band_);
  }
};

static void sh_node_oklab_valtorgb_build_multi_function(nodes::NodeMultiFunctionBuilder &builder)
{
  const bNode &bnode = builder.node();
  const ColorBand *color_band = static_cast<const ColorBand *>(bnode.storage);
  builder.construct_and_set_matching_fn<OKLabColorBandFunction>(*color_band,
                                                                builder.shared_tree());
}

NODE_SHADER_MATERIALX_BEGIN
#ifdef WITH_MATERIALX
{
  NodeItem res = empty();
  return res;
}
#endif
NODE_SHADER_MATERIALX_END

}  // namespace nodes::node_shader_oklab_color_ramp_cc

void register_node_type_sh_oklab_color_ramp()
{
  namespace file_ns = nodes::node_shader_oklab_color_ramp_cc;

  static bke::bNodeType ntype;

  common_node_type_base(&ntype, "ShaderNodeOKLabColorRamp"_ustr, SH_NODE_OKLAB_COLOR_RAMP);
  ntype.ui_name = "OKLab Color Ramp";
  ntype.ui_description = "Color ramp with perceptually uniform OKLab interpolation";
  ntype.enum_name_legacy = "OKLAB_COLOR_RAMP";
  ntype.nclass = NODE_CLASS_CONVERTER;
  ntype.declare = file_ns::sh_node_oklab_valtorgb_declare;
  ntype.initfunc = file_ns::node_shader_init_oklab_valtorgb;
  ntype.default_width = bke::NodeWidth::_240;
  bke::node_type_storage(
      ntype, "ColorBand", node_free_standard_storage, node_copy_standard_storage);
  ntype.gpu_fn = file_ns::gpu_shader_oklab_valtorgb;
  ntype.build_multi_function = file_ns::sh_node_oklab_valtorgb_build_multi_function;
  ntype.materialx_fn = file_ns::node_shader_materialx;

  bke::node_register_type(ntype);
}

}  // namespace blender
