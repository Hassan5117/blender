/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include "BKE_material.hh"
#include "BKE_node.hh"
#include "BKE_node_legacy_types.hh"
#include "BKE_node_runtime.hh"
#include "BKE_object.hh"

#include "BLI_math_matrix.hh"

#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_world_types.h"

#include "GPU_framebuffer.hh"
#include "GPU_shader.hh"
#include "GPU_texture.hh"

#include "draw_cache.hh"
#include "draw_common.hh"
#include "draw_view_data.hh"

#include "../eevee/eevee_defines.hh"

#include "goo_defines.hh"
#include "goo_instance.hh"
#include "goo_shader.hh"

namespace blender::goo {

static float4 extract_world_ambient(const World *world)
{
  if (world == nullptr) {
    return float4(0.05f, 0.05f, 0.05f, 1.0f);
  }
  float3 color(world->horr, world->horg, world->horb);
  if (world->nodetree != nullptr && world->nodetree->runtime != nullptr) {
    for (const bNode *node : world->nodetree->all_nodes()) {
      if (node->type_legacy == SH_NODE_BACKGROUND) {
        const bNodeSocket *color_sock = bke::node_find_socket(*node, SOCK_IN, "Color"_ustr);
        const bNodeSocket *strength_sock = bke::node_find_socket(*node, SOCK_IN, "Strength"_ustr);
        float strength = 1.0f;
        if (strength_sock != nullptr && strength_sock->link == nullptr &&
            strength_sock->default_value != nullptr)
        {
          strength = static_cast<const bNodeSocketValueFloat *>(strength_sock->default_value)
                         ->value;
        }
        if (color_sock != nullptr && color_sock->link == nullptr &&
            color_sock->default_value != nullptr)
        {
          const auto *val = static_cast<const bNodeSocketValueRGBA *>(color_sock->default_value);
          color = float3(val->value[0], val->value[1], val->value[2]) * strength;
        }
        else {
          color = float3(0.12f, 0.13f, 0.16f) * strength;
        }
        break;
      }
    }
  }
  return float4(color, 1.0f);
}

void Instance::init()
{
  draw_ctx = DRW_context_get();

  const World *world = draw_ctx->scene ? draw_ctx->scene->world : nullptr;
  world_color_ = extract_world_ambient(world);

  is_image_render_ = draw_ctx->is_image_render();

  const float2 size = draw_ctx->viewport_size_get();
  bloom.init(draw_ctx->scene, int2(size));
}

void Instance::begin_sync()
{
  material_subs_.clear();
  gpumat_subs_.clear();
  refract_subs_.clear();
  has_refract_draws_ = false;
  prepass_depth_ptr_ = nullptr;
  scene_color_ptr_ = nullptr;
  materials.begin_sync();
  lights.begin_sync(world_color_);

  if (dummy_color_tx_.ensure_2d(
          gpu::TextureFormat::SFLOAT_16_16_16_16, int2(1), GPU_TEXTURE_USAGE_SHADER_READ))
  {
    dummy_color_tx_.clear(float4(0.0f));
  }

  prepass_ps_.init();
  prepass_ps_.state_set(DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL | DRW_STATE_CULL_BACK);
  prepass_ps_.shader_set(ShaderModule::get().shadow_mesh.get());

  surface_ps_.init();
  surface_ps_.state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH |
                        DRW_STATE_DEPTH_LESS_EQUAL | DRW_STATE_CULL_BACK);
  surface_ps_.shader_set(ShaderModule::get().surface_flat.get());
  surface_ps_.push_constant("world_color", world_color_);

  refract_ps_.init();
  refract_ps_.state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH |
                        DRW_STATE_DEPTH_LESS_EQUAL | DRW_STATE_CULL_BACK);

  has_world_shader_ = false;
  World *world = draw_ctx->scene ? draw_ctx->scene->world : nullptr;
  GPUMaterial *world_mat = materials.world_get(world, !is_image_render_);
  if (world_mat != nullptr) {
    world_ps_.init();
    world_ps_.state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_LESS_EQUAL);
    world_ps_.material_set(*DRW_manager_get(), world_mat, !is_image_render_);
    world_ps_.bind_texture(RBUFS_UTILITY_TEX_SLOT, ShaderModule::get().utility_tx);
    world_ps_.bind_ubo(GOO_LIGHT_UBO_SLOT, lights.light_buf());
    world_ps_.bind_ubo(GOO_SHADOW_UBO_SLOT, lights.shadow_buf());
    world_ps_.bind_texture(
        GOO_SHADOW_TEX_SLOT, lights.shadow_tx_ptr(), GPUSamplerState::default_sampler());
    world_ps_.bind_texture(
        GOO_DEPTH_TEX_SLOT, &prepass_depth_ptr_, GPUSamplerState::default_sampler());
    world_ps_.bind_texture(
        GOO_SCENE_COLOR_TEX_SLOT, dummy_color_tx_, GPUSamplerState::default_sampler());
    world_ps_.push_constant("goo_light_groups", int4(0, 0, 0, 1));
    world_ps_.push_constant("goo_light_group_shadows", int4(0, 0, 0, 1));
    world_ps_.draw_procedural(GPU_PRIM_TRIS, 1, 3);
    has_world_shader_ = true;
  }

  bloom.sync();
}

PassMain::Sub &Instance::material_sub_get(const Material *ma)
{
  return *material_subs_.lookup_or_add_cb(ma, [&]() {
    PassMain::Sub &sub = surface_ps_.sub(ma ? ma->id.name + 2 : "Default");
    const float4 color = ma ? float4(ma->r, ma->g, ma->b, 1.0f) : float4(0.8f, 0.8f, 0.8f, 1.0f);
    sub.push_constant("base_color", color);
    return &sub;
  });
}

void Instance::configure_material_sub(PassMain::Sub &sub,
                                      Manager &manager,
                                      GPUMaterial *gpumat,
                                      bool is_refract)
{
  const Material *ma = GPU_material_get_material(gpumat);
  sub.material_set(manager, gpumat, !is_image_render_);
  sub.bind_texture(RBUFS_UTILITY_TEX_SLOT, ShaderModule::get().utility_tx);
  sub.bind_ubo(GOO_LIGHT_UBO_SLOT, lights.light_buf());
  sub.bind_ubo(GOO_SHADOW_UBO_SLOT, lights.shadow_buf());
  sub.bind_texture(
      GOO_SHADOW_TEX_SLOT, lights.shadow_tx_ptr(), GPUSamplerState::default_sampler());
  sub.bind_texture(GOO_DEPTH_TEX_SLOT, &prepass_depth_ptr_, GPUSamplerState::default_sampler());
  if (is_refract) {
    sub.bind_texture(
        GOO_SCENE_COLOR_TEX_SLOT, &scene_color_ptr_, GPUSamplerState{GPU_SAMPLER_FILTERING_LINEAR});
  }
  else {
    sub.bind_texture(
        GOO_SCENE_COLOR_TEX_SLOT, dummy_color_tx_, GPUSamplerState::default_sampler());
  }
  const int4 light_groups = ma ? int4(ma->light_group_bits[0],
                                      ma->light_group_bits[1],
                                      ma->light_group_bits[2],
                                      ma->light_group_bits[3]) :
                                 int4(0, 0, 0, 1);
  const int4 shadow_groups = ma ? int4(ma->light_group_shadow_bits[0],
                                       ma->light_group_shadow_bits[1],
                                       ma->light_group_shadow_bits[2],
                                       ma->light_group_shadow_bits[3]) :
                                  int4(0, 0, 0, 1);
  sub.push_constant("goo_light_groups", light_groups);
  sub.push_constant("goo_light_group_shadows", shadow_groups);
}

PassMain::Sub &Instance::gpumat_sub_get(Manager &manager, GPUMaterial *gpumat)
{
  return *gpumat_subs_.lookup_or_add_cb(gpumat, [&]() {
    const Material *ma = GPU_material_get_material(gpumat);
    PassMain::Sub &sub = surface_ps_.sub(ma ? ma->id.name + 2 : "Material");
    configure_material_sub(sub, manager, gpumat, false);
    return &sub;
  });
}

PassMain::Sub &Instance::refract_sub_get(Manager &manager, GPUMaterial *gpumat)
{
  return *refract_subs_.lookup_or_add_cb(gpumat, [&]() {
    const Material *ma = GPU_material_get_material(gpumat);
    PassMain::Sub &sub = refract_ps_.sub(ma ? ma->id.name + 2 : "Refract");
    configure_material_sub(sub, manager, gpumat, true);
    return &sub;
  });
}

void Instance::object_sync(ObjectRef &ob_ref, Manager &manager)
{
  Object *ob = ob_ref.object;
  if (ob->type == OB_LAMP) {
    if (DRW_object_visibility_in_active_context(ob) & OB_VISIBLE_SELF) {
      lights.light_sync(ob, View::default_get());
    }
    return;
  }
  if (ob->type != OB_MESH) {
    return;
  }
  if (!DRW_object_is_renderable(ob)) {
    return;
  }
  const int visibility = DRW_object_visibility_in_active_context(ob);
  if ((visibility & OB_VISIBLE_SELF) == 0) {
    return;
  }

  ResourceHandleRange handle = manager.unique_handle(ob_ref);
  lights.caster_sync(ob, handle);

  /* Node-tree materials. */
  const int mat_count = std::max(1, BKE_object_material_count_eval(ob));
  Vector<GPUMaterial *> gpumats(mat_count, nullptr);
  bool all_ready = true;
  for (int i = 0; i < mat_count; i++) {
    Material *ma = BKE_object_material_get_eval(ob, i + 1);
    gpumats[i] = materials.surface_get(ma, !is_image_render_);
    all_ready &= gpumats[i] != nullptr;
  }

  if (all_ready) {
    bool has_refract = false;
    for (int i = 0; i < mat_count; i++) {
      if (GPU_material_flag_get(gpumats[i], GPU_MATFLAG_REFRACT)) {
        has_refract = true;
        break;
      }
    }
    if (!has_refract) {
      if (gpu::Batch *prepass_geom = DRW_cache_mesh_surface_get(ob)) {
        prepass_ps_.draw(prepass_geom, handle);
      }
    }

    Span<gpu::Batch *> geoms = DRW_cache_mesh_surface_shaded_get(
        ob, Span<const GPUMaterial *>(const_cast<const GPUMaterial **>(gpumats.data()), mat_count));
    for (int i : geoms.index_range()) {
      if (geoms[i] == nullptr) {
        continue;
      }
      if (GPU_material_flag_get(gpumats[i], GPU_MATFLAG_REFRACT)) {
        refract_sub_get(manager, gpumats[i]).draw(geoms[i], handle);
        has_refract_draws_ = true;
      }
      else {
        gpumat_sub_get(manager, gpumats[i]).draw(geoms[i], handle);
      }
    }
    return;
  }

  /* Fallback while shaders compile (or on compile failure): flat shading. */
  gpu::Batch *geom = DRW_cache_mesh_surface_get(ob);
  if (geom == nullptr) {
    return;
  }
  prepass_ps_.draw(geom, handle);
  const Material *ma = BKE_object_material_get_eval(ob, 1);
  material_sub_get(ma).draw(geom, handle);
}

void Instance::end_sync()
{
  lights.end_sync();

  if (materials.queued_count > 0 && !is_image_render_) {
    /* Keep redrawing until all materials are compiled. */
    DRW_viewport_request_redraw();
  }
}

void Instance::draw_to(Manager &manager, View &view, gpu::Texture *color, gpu::Texture *depth)
{
  lights.render_shadows(manager);

  const int2 extent = int2(GPU_texture_width(color), GPU_texture_height(color));
  prepass_depth_tx_.acquire_2d(extent,
                               gpu::TextureFormat::SFLOAT_32_DEPTH,
                               GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);
  prepass_depth_ptr_ = prepass_depth_tx_;
  prepass_fb_.ensure(GPU_ATTACHMENT_TEXTURE(prepass_depth_tx_));
  prepass_fb_.bind();
  GPU_framebuffer_clear_depth(prepass_fb_, 1.0f);
  manager.submit(prepass_ps_, view);

  output_fb_.ensure(GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(color));

  const bool use_bloom = bloom.enabled();
  gpu::Texture *target_color = color;
  if (use_bloom) {
    /* Render surfaces into an intermediate buffer that post effects can sample. */
    color_tx_.acquire_2d(extent,
                         gpu::TextureFormat::SFLOAT_16_16_16_16,
                         GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);
    target_color = color_tx_;
  }

  main_fb_.ensure(GPU_ATTACHMENT_TEXTURE(depth), GPU_ATTACHMENT_TEXTURE(target_color));
  main_fb_.bind();
  GPU_framebuffer_clear_color_depth(main_fb_, double4(world_color_), 1.0f);
  manager.submit(surface_ps_, view);
  if (has_world_shader_) {
    manager.submit(world_ps_, view);
  }

  if (has_refract_draws_) {
    scene_color_tx_.acquire_2d(extent,
                               GPU_texture_format(target_color),
                               GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);
    GPU_texture_copy(scene_color_tx_, target_color);
    scene_color_ptr_ = scene_color_tx_;
    main_fb_.bind();
    manager.submit(refract_ps_, view);
    scene_color_tx_.release();
    scene_color_ptr_ = nullptr;
  }

  if (use_bloom) {
    bloom.render(manager, view, color_tx_, output_fb_);
    color_tx_.release();
  }

  prepass_depth_tx_.release();
  prepass_depth_ptr_ = nullptr;
}

void Instance::draw(Manager &manager)
{
  DefaultTextureList *dtxl = draw_ctx->viewport_texture_list_get();
  draw_to(manager, View::default_get(), dtxl->color, dtxl->depth);
}

}  // namespace blender::goo
