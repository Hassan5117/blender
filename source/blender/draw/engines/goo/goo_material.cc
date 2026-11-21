/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include <sstream>

#include "BKE_lib_id.hh"
#include "BKE_material.hh"

#include "BLI_math_bits.h"
#include "BLI_set.hh"

#include "DNA_material_types.h"
#include "DNA_world_types.h"

#include "GPU_capabilities.hh"
#include "GPU_material.hh"
#include "GPU_shader.hh"
#include "GPU_uniform_buffer.hh"

#include "gpu_shader_create_info.hh"

#include "goo_material.hh"

namespace blender::goo {

/* Shader variant identifier. Only one pipeline (forward surface, mesh) for now. Kept as a bitfield
 * so future variants (shadow, prepass, hair, volume, alpha-blend...) map onto Goo 4.4's
 * `VAR_MAT_*` options. */
enum eGooMaterialVariant : uint64_t {
  GOO_MAT_SURFACE_MESH = 1 << 0,
  GOO_MAT_WORLD = 1 << 1,
  GOO_MAT_PREPASS_MESH = 1 << 2,
  GOO_MAT_SHADOW_MESH = 1 << 3,
};

namespace {

/**
 * Reserves sampler and vertex-attribute slots used by Goo's pipeline infos, then assigns
 * non-overlapping slots to codegen's material texture samplers.
 */
class SlotAllocator {
  uint32_t available_samplers_ = ~uint32_t(0u);
  int total_requested_samplers_ = 0;
  bool sampler_overflow_ = false;

  uint32_t available_vertex_id_ = ~uint32_t(0u);
  bool vertex_id_overflow_ = false;

  Set<std::string> visited_infos;

  void reserve_slots_recursive(const gpu::shader::ShaderCreateInfo &info,
                               bool is_gpumat_info = true)
  {
    if (!visited_infos.add_overwrite(info.name_)) {
      return;
    }
    using namespace blender::gpu::shader;
    for (const ShaderCreateInfo::VertIn &vert_in : info.vertex_inputs_) {
      available_vertex_id_ &= ~(uint32_t(1) << vert_in.index);
    }
    for (const ShaderCreateInfo::Resource &res : info.pass_resources_) {
      if (res.bind_type == ShaderCreateInfo::Resource::SAMPLER) {
        available_samplers_ &= ~(uint32_t(1) << res.slot);
      }
    }
    for (const ShaderCreateInfo::Resource &res : info.geometry_resources_) {
      if (res.bind_type == ShaderCreateInfo::Resource::SAMPLER) {
        available_samplers_ &= ~(uint32_t(1) << res.slot);
      }
    }
    if (!is_gpumat_info) {
      for (const ShaderCreateInfo::Resource &res : info.batch_resources_) {
        if (res.bind_type == ShaderCreateInfo::Resource::SAMPLER) {
          available_samplers_ &= ~(uint32_t(1) << res.slot);
        }
      }
    }

    for (const auto &[info_name, _] : info.additional_infos_) {
      const ShaderCreateInfo *child_info = reinterpret_cast<const ShaderCreateInfo *>(
          GPU_shader_create_info_get(info_name.c_str()));
      reserve_slots_recursive(*child_info, false);
    }
  }

 public:
  void reserve_slots(gpu::shader::ShaderCreateInfo &info)
  {
    reserve_slots_recursive(info);
    total_requested_samplers_ = count_bits_uint64(uint64_t(~available_samplers_));

    for (auto &resource : info.batch_resources_) {
      if (resource.bind_type == gpu::shader::ShaderCreateInfo::Resource::BindType::SAMPLER) {
        resource.slot = get_next_sampler();
      }
    }
  }

  bool sampler_overflow() const
  {
    return sampler_overflow_;
  }

  bool vertex_id_overflow() const
  {
    return vertex_id_overflow_;
  }

  int get_next_sampler()
  {
    int next_sampler = available_samplers_ == 0 ? total_requested_samplers_ :
                                                  bitscan_forward_clear_uint(&available_samplers_);
    total_requested_samplers_++;
    if (next_sampler >= GPU_max_textures()) {
      sampler_overflow_ = true;
      return -1;
    }
    return next_sampler;
  }

  void set_vertex_input(int index)
  {
    uint32_t bit = uint32_t(1) << index;
    if ((available_vertex_id_ & bit) == 0) {
      vertex_id_overflow_ = true;
    }
    available_vertex_id_ &= ~bit;
  }
};

}  // namespace

/**
 * Amend the create-info produced by GPU codegen with Goo's surface pipeline.
 * Equivalent of Goo 4.4 `eevee_shader_material_create_info_amend`.
 *
 * WATCH: Can be called from a compilation thread. Must not touch engine state.
 */
static void codegen_callback(void * /*thunk*/, GPUMaterial *gpumat, GPUCodegenOutput *codegen_)
{
  using namespace blender::gpu::shader;

  GPUCodegenOutput &codegen = *codegen_;
  ShaderCreateInfo &info = *reinterpret_cast<ShaderCreateInfo *>(codegen.create_info);
  const uint64_t uuid = GPU_material_uuid_get(gpumat);
  const bool is_world = (uuid & GOO_MAT_WORLD) != 0;
  const bool is_prepass = (uuid & GOO_MAT_PREPASS_MESH) != 0;
  const bool is_shadow = (uuid & GOO_MAT_SHADOW_MESH) != 0;

  /* WORKAROUND (same as EEVEE): object attributes come from the draw manager SSBO. */
  if (GPU_material_uniform_attributes(gpumat) != nullptr) {
    info.additional_info("draw_object_attributes");
    for (auto &resource_info : info.batch_resources_) {
      if (resource_info.bind_type == ShaderCreateInfo::Resource::BindType::UNIFORM_BUFFER &&
          resource_info.uniformbuf.name == GPU_ATTRIBUTE_UBO_BLOCK_NAME "[512]")
      {
        info.batch_resources_.remove_first_occurrence_and_reorder(resource_info);
        break;
      }
    }
    info.define("UNI_ATTR(a)", "float4(0.0)");
  }

  /* Vertex inputs from codegen use slots counting down from 15. Goo uses 0 (pos) and 1 (nor). */
  auto vertex_inputs = info.vertex_inputs_;
  info.vertex_inputs_.clear();

  if (is_world) {
    info.define("MAT_GEOM_WORLD");
    info.additional_info("goo_material_world");
  }
  else if (is_prepass) {
    info.additional_info("goo_material_prepass_mesh");
  }
  else if (is_shadow) {
    info.additional_info("goo_material_shadow_mesh");
  }
  else {
    info.additional_info("goo_material_surface_mesh");
    if (GPU_material_flag_get(gpumat, GPU_MATFLAG_SET_DEPTH)) {
      info.define("GOO_USE_SET_DEPTH");
      info.depth_write(DepthWrite::ANY);
    }
  }
  info.define("GOO_ENGINE");

  if (GPU_material_flag_get(gpumat, GPU_MATFLAG_AO)) {
    info.define("MAT_AMBIENT_OCCLUSION");
  }
  if (GPU_material_flag_get(gpumat, GPU_MATFLAG_TRANSPARENT)) {
    info.define("MAT_TRANSPARENT");
  }
  if (GPU_material_flag_get(gpumat, GPU_MATFLAG_SHADER_TO_RGBA)) {
    info.define("MAT_SHADER_TO_RGBA");
  }
  if (GPU_material_flag_get(gpumat, GPU_MATFLAG_DIFFUSE)) {
    info.define("MAT_DIFFUSE");
  }
  if (GPU_material_flag_get(gpumat, GPU_MATFLAG_SUBSURFACE)) {
    info.define("MAT_SUBSURFACE");
  }
  if (GPU_material_flag_get(gpumat, GPU_MATFLAG_REFRACT)) {
    info.define("MAT_REFRACTION");
  }
  if (GPU_material_flag_get(gpumat, GPU_MATFLAG_TRANSLUCENT)) {
    info.define("MAT_TRANSLUCENT");
  }
  if (GPU_material_flag_get(gpumat, GPU_MATFLAG_GLOSSY)) {
    info.define("MAT_REFLECTION");
  }
  if (GPU_material_flag_get(gpumat, GPU_MATFLAG_COAT)) {
    info.define("MAT_CLEARCOAT");
  }
  if (GPU_material_flag_get(gpumat, GPU_MATFLAG_BARYCENTRIC) && !is_world) {
    info.define("USE_BARYCENTRICS");
    info.builtins(BuiltinBits::BARYCENTRIC_COORD);
  }

  SlotAllocator slots;
  slots.reserve_slots(info);

  std::stringstream global_vars;
  if (is_world) {
    for (auto &vert_in : vertex_inputs) {
      global_vars << vert_in.type << " " << vert_in.name << ";\n";
    }
    vertex_inputs.clear();
    if (!info.vertex_out_interfaces_.is_empty()) {
      const StageInterfaceInfo &iface = *info.vertex_out_interfaces_.first();
      global_vars << "struct " << iface.name << " {\n";
      for (const auto &inout : iface.inouts) {
        global_vars << "  " << inout.type << " " << inout.name << ";\n";
      }
      global_vars << "};\n";
      global_vars << iface.name << " " << iface.instance_name << ";\n";
      info.vertex_out_interfaces_.clear();
    }
  }
  else {
    for (auto &vert_in : vertex_inputs) {
      slots.set_vertex_input(vert_in.index);
      info.vertex_in(vert_in.index, vert_in.type, vert_in.name);
    }
  }

  if (slots.vertex_id_overflow()) {
    info.vertex_inputs_.clear();
  }
  if (slots.sampler_overflow()) {
    info.batch_resources_.clear();
  }

  /* Allow to use Reverse-Z on OpenGL. Does nothing in other backends. */
  info.builtins(BuiltinBits::CLIP_CONTROL);

  std::stringstream attr_load;
  attr_load << "{\n";
  attr_load << (!codegen.attr_load.empty() ? codegen.attr_load : "");
  attr_load << "}\n\n";

  /* Make the node-tree resources part of a dedicated interface name. */
  for (auto &res : info.batch_resources_) {
    res.info_name = "goo_node_tree";
  }
  for (auto &res : info.pass_resources_) {
    res.info_name = "goo_node_tree";
  }
  for (auto &res : info.geometry_resources_) {
    res.info_name = "goo_node_tree";
  }

  std::string generated_resource_header = info.typedef_source_generated;
  generated_resource_header += "#ifdef CREATE_INFO_RES_PASS_goo_node_tree\n";
  generated_resource_header += "CREATE_INFO_RES_PASS_goo_node_tree\n";
  generated_resource_header += "#endif\n";
  generated_resource_header += "#ifdef CREATE_INFO_RES_BATCH_goo_node_tree\n";
  generated_resource_header += "CREATE_INFO_RES_BATCH_goo_node_tree\n";
  generated_resource_header += "#endif\n";
  generated_resource_header += "#ifdef CREATE_INFO_RES_GEOMETRY_goo_node_tree\n";
  generated_resource_header += "CREATE_INFO_RES_GEOMETRY_goo_node_tree\n";
  generated_resource_header += "#endif\n\n";
  info.generated_sources.append({"goo_nodetree_type_lib.glsl", {}, generated_resource_header});

  /* Vertex stage: attribute loading + (optional) displacement for meshes. */
  if (!is_world) {
    std::stringstream vert_gen;
    vert_gen << "void attrib_load(MeshVertex domain)" << attr_load.str();

    Vector<StringRefNull> dependencies = {"goo_geom_types_lib.glsl",
                                          "goo_attributes_mesh_lib.glsl"};
    const bool use_vertex_displacement = !codegen.displacement.empty() &&
                                         ELEM(GPU_material_get_material(gpumat) ?
                                                  GPU_material_get_material(gpumat)
                                                      ->displacement_method :
                                                  MA_DISPLACEMENT_BUMP,
                                              MA_DISPLACEMENT_DISPLACE,
                                              MA_DISPLACEMENT_BOTH);
    if (use_vertex_displacement) {
      dependencies.append("goo_nodetree_lib.glsl");
      dependencies.extend(codegen.displacement.dependencies);
    }
    vert_gen << "float3 nodetree_displacement()\n{\n";
    vert_gen << (use_vertex_displacement ? codegen.displacement.serialized :
                                           "return float3(0);\n");
    vert_gen << "}\n\n";
    info.generated_sources.append({"goo_nodetree_vert_lib.glsl", dependencies, vert_gen.str()});
  }

  /* Fragment stage: node-tree surface evaluation. */
  {
    std::stringstream frag_gen;
    Vector<StringRefNull> dependencies;

    if (is_world) {
      dependencies.append("goo_attributes_world_lib.glsl");
      dependencies.append("goo_nodetree_lib.glsl");
      frag_gen << global_vars.str();
      frag_gen << "void attrib_load(WorldPoint domain)" << attr_load.str();
    }
    else {
      dependencies.append("goo_nodetree_lib.glsl");
    }

    for (const auto &graph : codegen.material_functions) {
      frag_gen << graph.serialized;
      dependencies.extend(graph.dependencies);
    }

    if (!codegen.displacement.empty() && !is_world) {
      /* Bump displacement. Needed to recompute normals after displacement. */
      info.define("MAT_DISPLACEMENT_BUMP");
      frag_gen << "float3 nodetree_displacement()\n{\n";
      frag_gen << codegen.displacement.serialized;
      dependencies.extend(codegen.displacement.dependencies);
      frag_gen << "}\n\n";
    }

    frag_gen << "Closure nodetree_surface(float closure_rand)\n{\n";
    frag_gen << "  closure_weights_reset(closure_rand);\n";
    frag_gen << codegen.surface.serialized_or_default("return Closure(0);\n");
    dependencies.extend(codegen.surface.dependencies);
    frag_gen << "}\n\n";

    info.generated_sources.append({"goo_nodetree_frag_lib.glsl", dependencies, frag_gen.str()});
  }
}

MaterialModule::~MaterialModule()
{
  if (default_world_ != nullptr) {
    BKE_id_free(nullptr, default_world_);
  }
}

GPUMaterial *MaterialModule::world_get(World *wo, bool deferred_compilation)
{
  if (wo == nullptr || wo->nodetree == nullptr) {
    if (default_world_ == nullptr) {
      default_world_ = BKE_id_new_nomain<World>("Goo default world");
      default_world_->gpumaterial.clear_no_delete();
    }
    wo = default_world_;
  }
  if (wo->nodetree == nullptr) {
    return nullptr;
  }

  GPUMaterialFromNodeTreeResult result = GPU_material_from_nodetree(nullptr,
                                                                    wo->nodetree,
                                                                    &wo->gpumaterial,
                                                                    wo->id.name,
                                                                    GPU_MAT_GOO,
                                                                    GOO_MAT_WORLD,
                                                                    deferred_compilation,
                                                                    codegen_callback,
                                                                    nullptr);
  GPUMaterial *gpumat = result.material;
  if (gpumat == nullptr) {
    return nullptr;
  }
  switch (GPU_material_status(gpumat)) {
    case GPU_MAT_SUCCESS:
      return gpumat;
    case GPU_MAT_QUEUED:
      queued_count++;
      return nullptr;
    case GPU_MAT_FAILED:
    default:
      return nullptr;
  }
}

static GPUMaterial *material_variant_get(Material *ma,
                                         uint64_t variant,
                                         bool deferred_compilation,
                                         int &r_queued_count)
{
  Material *mat = ma ? ma : BKE_material_default_surface();
  bNodeTree *ntree = mat->nodetree ? mat->nodetree : BKE_material_default_surface()->nodetree;

  GPUMaterialFromNodeTreeResult result = GPU_material_from_nodetree(mat,
                                                                    ntree,
                                                                    &mat->gpumaterial,
                                                                    mat->id.name,
                                                                    GPU_MAT_GOO,
                                                                    variant,
                                                                    deferred_compilation,
                                                                    codegen_callback,
                                                                    nullptr);
  GPUMaterial *gpumat = result.material;
  if (gpumat == nullptr) {
    return nullptr;
  }
  switch (GPU_material_status(gpumat)) {
    case GPU_MAT_SUCCESS:
      return gpumat;
    case GPU_MAT_QUEUED:
      r_queued_count++;
      return nullptr;
    case GPU_MAT_FAILED:
    default:
      return nullptr;
  }
}

GPUMaterial *MaterialModule::surface_get(Material *ma, bool deferred_compilation)
{
  return material_variant_get(ma, GOO_MAT_SURFACE_MESH, deferred_compilation, queued_count);
}

GPUMaterial *MaterialModule::prepass_get(Material *ma, bool deferred_compilation)
{
  return material_variant_get(ma, GOO_MAT_PREPASS_MESH, deferred_compilation, queued_count);
}

GPUMaterial *MaterialModule::shadow_get(Material *ma, bool deferred_compilation)
{
  return material_variant_get(ma, GOO_MAT_SHADOW_MESH, deferred_compilation, queued_count);
}

}  // namespace blender::goo
