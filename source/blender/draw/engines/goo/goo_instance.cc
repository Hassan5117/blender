/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include <algorithm>
#include <cmath>

#include "BKE_material.hh"
#include "BKE_node.hh"
#include "BKE_node_legacy_types.hh"
#include "BKE_node_runtime.hh"
#include "BKE_object.hh"

#include "BLI_math_matrix.hh"

#include "DEG_depsgraph_query.hh"

#include "DNA_layer_types.h"
#include "DNA_material_types.h"
#include "DNA_node_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_world_types.h"

#include "GPU_capabilities.hh"
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

void Instance::compute_extents(const Scene *scene, int2 output_extent)
{
  output_extent_ = math::max(output_extent, int2(1));
  overscan_px_ = 0;
  if (scene != nullptr && (scene->eevee.flag & SCE_EEVEE_OVERSCAN) != 0 &&
      scene->eevee.overscan > 0.0f)
  {
    const int max_dim = std::max(output_extent_.x, output_extent_.y);
    const int raw_px = int(std::lround(float(max_dim) * (scene->eevee.overscan / 100.0f)));
    const int max_px = std::max(0, (GPU_max_texture_size() - max_dim) / 2);
    overscan_px_ = std::clamp(raw_px, 0, max_px);
  }
  render_extent_ = output_extent_ + int2(overscan_px_ * 2);
}

float4x4 Instance::overscan_winmat(const float4x4 &base_winmat) const
{
  float4x4 winmat = base_winmat;
  if (overscan_px_ > 0 && render_extent_.x > 0 && render_extent_.y > 0) {
    const float sx = float(output_extent_.x) / float(render_extent_.x);
    const float sy = float(output_extent_.y) / float(render_extent_.y);
    float4x4 scale = float4x4::identity();
    scale[0][0] = sx;
    scale[1][1] = sy;
    winmat = scale * base_winmat;
  }
  return winmat;
}

void Instance::init()
{
  draw_ctx = DRW_context_get();

  const Scene *scene = draw_ctx->scene;
  const World *world = scene ? scene->world : nullptr;
  world_color_ = extract_world_ambient(world);

  is_image_render_ = draw_ctx->is_image_render();
  is_viewport_image_render_ = draw_ctx->is_viewport_image_render();
  film_transparent_ = (scene != nullptr) && (scene->r.alphamode == R_ALPHAPREMUL);

  compute_extents(scene, int2(draw_ctx->viewport_size_get()));
  bloom.init(scene, render_extent_);
}

void Instance::begin_sync()
{
  material_subs_.clear();
  gpumat_subs_.clear();
  refract_subs_.clear();
  prepass_mat_subs_.clear();
  has_refract_draws_ = false;
  has_blend_draws_ = false;
  has_blend_refract_draws_ = false;
  prepass_depth_ptr_ = nullptr;
  scene_color_ptr_ = nullptr;
  materials.begin_sync();
  lights.begin_sync(draw_ctx->scene, world_color_);

  if (dummy_depth_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_32_DEPTH,
                                int2(1),
                                GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT))
  {
    dummy_depth_tx_.clear(float4(1.0f));
  }
  if (dummy_color_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_16_16_16_16,
                                int2(1),
                                GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT))
  {
    dummy_color_tx_.clear(float4(0.0f));
  }
  if (dummy_horizon_tx_.ensure_2d(gpu::TextureFormat::UNORM_8_8_8_8,
                                  int2(1),
                                  GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT))
  {
    dummy_horizon_tx_.clear(float4(0.0f));
  }
  gtao_horizon_ptr_ = dummy_horizon_tx_;

  ShaderModule &sh = ShaderModule::get();
  const bool deferred_compile = !is_image_render_ && !is_viewport_image_render_;

  prepass_ps_.init();
  prepass_ps_.state_set(DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL);
  prepass_cull_sub_ = &prepass_ps_.sub("PrepassCull");
  prepass_cull_sub_->state_set(DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL |
                               DRW_STATE_CULL_BACK);
  prepass_cull_sub_->shader_set(sh.shadow_mesh.get());
  prepass_nocull_sub_ = &prepass_ps_.sub("PrepassNoCull");
  prepass_nocull_sub_->state_set(DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL);
  prepass_nocull_sub_->shader_set(sh.shadow_mesh.get());

  surface_ps_.init();
  surface_ps_.state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH |
                        DRW_STATE_DEPTH_LESS_EQUAL);
  surface_ps_.shader_set(sh.surface_flat.get());
  surface_ps_.push_constant("world_color", world_color_);

  refract_ps_.init();
  refract_ps_.state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH |
                        DRW_STATE_DEPTH_LESS_EQUAL);

  blend_ps_.init();

  has_world_shader_ = false;
  World *world = draw_ctx->scene ? draw_ctx->scene->world : nullptr;
  GPUMaterial *world_mat = materials.world_get(world, deferred_compile);
  if (world_mat != nullptr) {
    world_ps_.init();
    world_ps_.state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_DEPTH_LESS_EQUAL);
    world_ps_.material_set(*DRW_manager_get(), world_mat, deferred_compile);
    world_ps_.bind_texture(RBUFS_UTILITY_TEX_SLOT, sh.utility_tx);
    world_ps_.bind_ubo(GOO_LIGHT_UBO_SLOT, lights.light_buf());
    world_ps_.bind_ubo(GOO_SHADOW_UBO_SLOT, lights.shadow_buf());
    world_ps_.bind_texture(
        GOO_SHADOW_TEX_SLOT, lights.shadow_tx_ptr(), GPUSamplerState::default_sampler());
    world_ps_.bind_texture(
        GOO_SHADOW_ID_TEX_SLOT, lights.shadow_id_tx_ptr(), GPUSamplerState::default_sampler());
    world_ps_.bind_texture(
        GOO_DEPTH_TEX_SLOT, &prepass_depth_ptr_, GPUSamplerState::default_sampler());
    world_ps_.bind_texture(
        GOO_SCENE_COLOR_TEX_SLOT, dummy_color_tx_, GPUSamplerState::default_sampler());
    world_ps_.bind_texture(
        GOO_HORIZON_TEX_SLOT, dummy_horizon_tx_, GPUSamplerState::default_sampler());
    world_ps_.push_constant("goo_light_groups", int4(0, 0, 0, 1));
    world_ps_.push_constant("goo_light_group_shadows", int4(0, 0, 0, 1));
    world_ps_.push_constant("goo_alpha_threshold", 0.0f);
    world_ps_.push_constant("goo_material_flags", 0);
    world_ps_.draw_procedural(GPU_PRIM_TRIS, 1, 3);
    has_world_shader_ = true;
  }

  gtao_ps_.init();
  gtao_ps_.state_set(DRW_STATE_WRITE_COLOR);
  gtao_ps_.shader_set(sh.effect_gtao.get());
  gtao_ps_.bind_ubo(GOO_LIGHT_UBO_SLOT, lights.light_buf());
  gtao_ps_.bind_texture(RBUFS_UTILITY_TEX_SLOT, sh.utility_tx);
  gtao_ps_.bind_texture(
      GOO_DEPTH_TEX_SLOT, &prepass_depth_ptr_, GPUSamplerState::default_sampler());
  gtao_ps_.draw_procedural(GPU_PRIM_TRIS, 1, 3);

  taa_accum_ps_.init();
  taa_accum_ps_.state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ADD_FULL);
  taa_accum_ps_.shader_set(sh.taa_accum.get());
  taa_accum_ps_.bind_texture("sample_tx", &unf_sample_tx_);
  taa_accum_ps_.push_constant("sample_weight", &unf_sample_weight_);
  taa_accum_ps_.draw_procedural(GPU_PRIM_TRIS, 1, 3);

  taa_blend_ps_.init();
  taa_blend_ps_.state_set(DRW_STATE_WRITE_COLOR);
  taa_blend_ps_.shader_set(sh.taa_blend.get());
  taa_blend_ps_.bind_texture("sample_tx", &unf_sample_tx_);
  taa_blend_ps_.bind_texture("history_tx", &unf_history_tx_);
  taa_blend_ps_.push_constant("blend_alpha", &unf_blend_alpha_);
  taa_blend_ps_.draw_procedural(GPU_PRIM_TRIS, 1, 3);

  taa_reproject_ps_.init();
  taa_reproject_ps_.state_set(DRW_STATE_WRITE_COLOR);
  taa_reproject_ps_.shader_set(sh.taa_reproject.get());
  taa_reproject_ps_.bind_texture("sample_tx", &unf_sample_tx_);
  taa_reproject_ps_.bind_texture(
      "history_tx", &unf_history_tx_, GPUSamplerState{GPU_SAMPLER_FILTERING_LINEAR});
  taa_reproject_ps_.bind_texture("depth_tx", &unf_depth_tx_);
  taa_reproject_ps_.push_constant("prev_persmat", &unf_prev_persmat_);
  taa_reproject_ps_.draw_procedural(GPU_PRIM_TRIS, 1, 3);

  film_resolve_ps_.init();
  film_resolve_ps_.state_set(DRW_STATE_WRITE_COLOR);
  film_resolve_ps_.shader_set(sh.film_resolve.get());
  film_resolve_ps_.bind_texture("source_tx", &unf_resolve_tx_);
  film_resolve_ps_.push_constant("overscan_offset", &unf_overscan_offset_);
  film_resolve_ps_.draw_procedural(GPU_PRIM_TRIS, 1, 3);

  film_crop_depth_ps_.init();
  film_crop_depth_ps_.state_set(DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_ALWAYS);
  film_crop_depth_ps_.shader_set(sh.film_crop_depth.get());
  film_crop_depth_ps_.bind_texture("depth_tx", &unf_depth_tx_);
  film_crop_depth_ps_.push_constant("overscan_offset", &unf_overscan_offset_);
  film_crop_depth_ps_.draw_procedural(GPU_PRIM_TRIS, 1, 3);

  bloom.sync();
}

PassMain::Sub &Instance::material_sub_get(const Material *ma)
{
  return *material_subs_.lookup_or_add_cb(ma, [&]() {
    PassMain::Sub &sub = surface_ps_.sub(ma ? ma->id.name + 2 : "Default");
    DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL;
    if (ma && (ma->blend_flag & MA_BL_CULL_BACKFACE) != 0) {
      state |= DRW_STATE_CULL_BACK;
    }
    sub.state_set(state);
    const float4 color = ma ? float4(ma->r, ma->g, ma->b, 1.0f) : float4(0.8f, 0.8f, 0.8f, 1.0f);
    sub.push_constant("base_color", color);
    return &sub;
  });
}

void Instance::configure_material_sub(PassMain::Sub &sub,
                                      Manager &manager,
                                      GPUMaterial *gpumat,
                                      const Material *ma,
                                      bool is_refract,
                                      bool is_prepass)
{
  const Material *mat = ma ? ma : GPU_material_get_material(gpumat);
  const bool deferred_compile = !is_image_render_ && !is_viewport_image_render_;
  sub.material_set(manager, gpumat, deferred_compile);
  sub.bind_texture(RBUFS_UTILITY_TEX_SLOT, ShaderModule::get().utility_tx);
  sub.bind_ubo(GOO_LIGHT_UBO_SLOT, lights.light_buf());
  sub.bind_ubo(GOO_SHADOW_UBO_SLOT, lights.shadow_buf());
  sub.bind_texture(
      GOO_SHADOW_TEX_SLOT, lights.shadow_tx_ptr(), GPUSamplerState::default_sampler());
  sub.bind_texture(
      GOO_SHADOW_ID_TEX_SLOT, lights.shadow_id_tx_ptr(), GPUSamplerState::default_sampler());
  if (is_prepass) {
    sub.bind_texture(GOO_DEPTH_TEX_SLOT, dummy_depth_tx_, GPUSamplerState::default_sampler());
    sub.bind_texture(GOO_HORIZON_TEX_SLOT, dummy_horizon_tx_, GPUSamplerState::default_sampler());
  }
  else {
    sub.bind_texture(GOO_DEPTH_TEX_SLOT, &prepass_depth_ptr_, GPUSamplerState::default_sampler());
    sub.bind_texture(GOO_HORIZON_TEX_SLOT, &gtao_horizon_ptr_, GPUSamplerState::default_sampler());
  }
  if (is_refract) {
    sub.bind_texture(
        GOO_SCENE_COLOR_TEX_SLOT, &scene_color_ptr_, GPUSamplerState{GPU_SAMPLER_FILTERING_LINEAR});
  }
  else {
    sub.bind_texture(
        GOO_SCENE_COLOR_TEX_SLOT, dummy_color_tx_, GPUSamplerState::default_sampler());
  }
  const int4 light_groups = mat ? int4(mat->light_group_bits[0],
                                       mat->light_group_bits[1],
                                       mat->light_group_bits[2],
                                       mat->light_group_bits[3]) :
                                  int4(0, 0, 0, 1);
  const int4 shadow_groups = mat ? int4(mat->light_group_shadow_bits[0],
                                        mat->light_group_shadow_bits[1],
                                        mat->light_group_shadow_bits[2],
                                        mat->light_group_shadow_bits[3]) :
                                   int4(0, 0, 0, 1);
  int mat_flags = 0;
  if (mat != nullptr) {
    if (mat->blend_method == MA_BM_CLIP) {
      mat_flags |= GOO_MAT_FLAG_ALPHA_CLIP;
    }
    else if (mat->blend_method == MA_BM_HASHED) {
      mat_flags |= GOO_MAT_FLAG_ALPHA_HASHED;
    }
    else if (mat->blend_method == MA_BM_BLEND) {
      mat_flags |= GOO_MAT_FLAG_ALPHA_BLEND;
    }
    if ((mat->check_shadow_id & MA_SHADOW_ID) != 0) {
      mat_flags |= GOO_MAT_FLAG_CHECK_SHADOW_ID;
    }
  }
  sub.push_constant("goo_light_groups", light_groups);
  sub.push_constant("goo_light_group_shadows", shadow_groups);
  sub.push_constant("goo_alpha_threshold", mat ? mat->alpha_threshold : 0.5f);
  sub.push_constant("goo_material_flags", mat_flags);
}

PassMain::Sub &Instance::gpumat_sub_get(Manager &manager, GPUMaterial *gpumat, const Material *ma)
{
  return *gpumat_subs_.lookup_or_add_cb(gpumat, [&]() {
    const Material *mat = ma ? ma : GPU_material_get_material(gpumat);
    PassMain::Sub &sub = surface_ps_.sub(mat ? mat->id.name + 2 : "Material");
    DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL;
    if (mat && (mat->blend_flag & MA_BL_CULL_BACKFACE) != 0) {
      state |= DRW_STATE_CULL_BACK;
    }
    sub.state_set(state);
    configure_material_sub(sub, manager, gpumat, mat, false, false);
    return &sub;
  });
}

PassMain::Sub &Instance::refract_sub_get(Manager &manager, GPUMaterial *gpumat, const Material *ma)
{
  return *refract_subs_.lookup_or_add_cb(gpumat, [&]() {
    const Material *mat = ma ? ma : GPU_material_get_material(gpumat);
    PassMain::Sub &sub = refract_ps_.sub(mat ? mat->id.name + 2 : "Refract");
    DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL;
    if (mat && (mat->blend_flag & MA_BL_CULL_BACKFACE) != 0) {
      state |= DRW_STATE_CULL_BACK;
    }
    sub.state_set(state);
    configure_material_sub(sub, manager, gpumat, mat, true, false);
    return &sub;
  });
}

PassMain::Sub &Instance::prepass_mat_sub_get(Manager &manager,
                                             GPUMaterial *gpumat,
                                             const Material *ma)
{
  return *prepass_mat_subs_.lookup_or_add_cb(gpumat, [&]() {
    const Material *mat = ma ? ma : GPU_material_get_material(gpumat);
    PassMain::Sub &sub = prepass_ps_.sub(mat ? mat->id.name + 2 : "PrepassMat");
    DRWState state = DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL;
    if (mat && (mat->blend_flag & MA_BL_CULL_BACKFACE) != 0) {
      state |= DRW_STATE_CULL_BACK;
    }
    sub.state_set(state);
    configure_material_sub(sub, manager, gpumat, mat, false, true);
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
  const bool cast_shadows = (ob->visibility_flag & OB_HIDE_SHADOW) == 0 &&
                            lights.shadows_enabled();

  /* Node-tree materials. */
  const bool deferred_compile = !is_image_render_ && !is_viewport_image_render_;
  const int mat_count = std::max(1, BKE_object_material_count_eval(ob));
  Vector<GPUMaterial *> gpumats(mat_count, nullptr);
  bool all_ready = true;
  for (int i = 0; i < mat_count; i++) {
    Material *ma = BKE_object_material_get_eval(ob, i + 1);
    gpumats[i] = materials.surface_get(ma, deferred_compile);
    all_ready &= gpumats[i] != nullptr;
  }

  if (all_ready) {
    Span<gpu::Batch *> geoms = DRW_cache_mesh_surface_shaded_get(
        ob, Span<const GPUMaterial *>(const_cast<const GPUMaterial **>(gpumats.data()), mat_count));
    const float sort_z = math::transform_point(View::default_get().viewmat(),
                                               ob->object_to_world().location())
                             .z;

    for (int i : geoms.index_range()) {
      if (geoms[i] == nullptr) {
        continue;
      }
      Material *ma = BKE_object_material_get_eval(ob, i + 1);
      GPUMaterial *gpumat = gpumats[i];
      const bool is_refract = GPU_material_flag_get(gpumat, GPU_MATFLAG_REFRACT);
      const bool cull_back = ma && (ma->blend_flag & MA_BL_CULL_BACKFACE) != 0;

      if (cast_shadows) {
        GPUMaterial *shadow_gpumat = nullptr;
        if (ma && ELEM(ma->blend_shadow, MA_BS_CLIP, MA_BS_HASHED)) {
          shadow_gpumat = materials.shadow_get(ma, deferred_compile);
        }
        lights.caster_slot_sync(
            manager, geoms[i], ma, shadow_gpumat, handle, deferred_compile);
      }

      if (ma && ma->blend_method == MA_BM_BLEND) {
        const bool hide_backface = (ma->blend_flag & MA_BL_HIDE_BACKFACE) != 0;
        if (hide_backface) {
          PassMain::Sub &pre_sub = blend_ps_.sub("BlendPrepass", sort_z);
          DRWState pre_state = DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL;
          if (cull_back) {
            pre_state |= DRW_STATE_CULL_BACK;
          }
          pre_sub.state_set(pre_state);
          pre_sub.shader_set(ShaderModule::get().shadow_mesh.get());
          pre_sub.draw(geoms[i], handle);
        }

        PassMain::Sub &col_sub = blend_ps_.sub(ma->id.name + 2, sort_z);
        DRWState col_state = DRW_STATE_WRITE_COLOR | DRW_STATE_BLEND_ALPHA_PREMUL;
        col_state |= hide_backface ? DRW_STATE_DEPTH_EQUAL : DRW_STATE_DEPTH_LESS_EQUAL;
        if (cull_back) {
          col_state |= DRW_STATE_CULL_BACK;
        }
        col_sub.state_set(col_state);
        configure_material_sub(col_sub, manager, gpumat, ma, is_refract, false);
        col_sub.draw(geoms[i], handle);
        has_blend_draws_ = true;
        has_blend_refract_draws_ |= is_refract;
      }
      else {
        if (!is_refract) {
          if (ma && ELEM(ma->blend_method, MA_BM_CLIP, MA_BM_HASHED)) {
            GPUMaterial *prepass_gpumat = materials.prepass_get(ma, deferred_compile);
            if (prepass_gpumat != nullptr) {
              prepass_mat_sub_get(manager, prepass_gpumat, ma).draw(geoms[i], handle);
            }
            else {
              (cull_back ? prepass_cull_sub_ : prepass_nocull_sub_)->draw(geoms[i], handle);
            }
          }
          else {
            (cull_back ? prepass_cull_sub_ : prepass_nocull_sub_)->draw(geoms[i], handle);
          }
        }

        if (is_refract) {
          refract_sub_get(manager, gpumat, ma).draw(geoms[i], handle);
          has_refract_draws_ = true;
        }
        else {
          gpumat_sub_get(manager, gpumat, ma).draw(geoms[i], handle);
        }
      }
    }
    return;
  }

  /* Fallback while shaders compile (or on compile failure): flat shading. */
  gpu::Batch *geom = DRW_cache_mesh_surface_get(ob);
  if (geom == nullptr) {
    return;
  }
  const Material *ma = BKE_object_material_get_eval(ob, 1);
  if (cast_shadows) {
    lights.caster_slot_sync(manager, geom, ma, nullptr, handle, deferred_compile);
  }
  const bool cull_back = ma && (ma->blend_flag & MA_BL_CULL_BACKFACE) != 0;
  (cull_back ? prepass_cull_sub_ : prepass_nocull_sub_)->draw(geom, handle);
  material_sub_get(ma).draw(geom, handle);
}

void Instance::end_sync()
{
  lights.end_sync();

  if (materials.queued_count > 0 && !is_image_render_ && !is_viewport_image_render_) {
    /* Keep redrawing until all materials are compiled. */
    DRW_viewport_request_redraw();
  }
}

void Instance::render_single_sample(Manager &manager,
                                    View &sample_view,
                                    gpu::Texture *dst_color,
                                    gpu::Texture *dst_depth,
                                    int sample_index,
                                    bool gtao_active)
{
  lights.set_sample_params(
      sample_index, sampling_.temporal_rand_get(sample_index), film_transparent_);

  prepass_fb_.ensure(GPU_ATTACHMENT_TEXTURE(prepass_depth_tx_));
  prepass_fb_.bind();
  GPU_framebuffer_clear_depth(prepass_fb_, 1.0f);
  manager.submit(prepass_ps_, sample_view);

  if (gtao_active) {
    gtao_horizon_ptr_ = gtao_horizon_tx_;
    gtao_fb_.ensure(GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(gtao_horizon_tx_));
    gtao_fb_.bind();
    manager.submit(gtao_ps_, sample_view);
  }
  else {
    gtao_horizon_ptr_ = dummy_horizon_tx_;
  }

  main_fb_.ensure(GPU_ATTACHMENT_TEXTURE(dst_depth), GPU_ATTACHMENT_TEXTURE(dst_color));
  main_fb_.bind();
  const double4 clear_col = film_transparent_ ? double4(0.0) : double4(world_color_);
  GPU_framebuffer_clear_color_depth(main_fb_, clear_col, 1.0f);
  manager.submit(surface_ps_, sample_view);

  if (has_world_shader_ && !film_transparent_) {
    manager.submit(world_ps_, sample_view);
  }

  if (has_refract_draws_ || has_blend_refract_draws_) {
    GPU_texture_copy(scene_color_tx_, dst_color);
  }
  if (has_refract_draws_) {
    main_fb_.bind();
    manager.submit(refract_ps_, sample_view);
  }
  if (has_blend_draws_) {
    main_fb_.bind();
    manager.submit(blend_ps_, sample_view);
  }
}

void Instance::draw_to(Manager &manager, View &view, gpu::Texture *color, gpu::Texture *depth)
{
  const Scene *scene = draw_ctx->scene;
  const int2 color_extent = int2(GPU_texture_width(color), GPU_texture_height(color));
  if (color_extent != output_extent_) {
    compute_extents(scene, color_extent);
    bloom.init(scene, render_extent_);
    bloom.sync();
  }

  lights.render_shadows(manager);

  const float4x4 base_viewmat = view.viewmat();
  const float4x4 base_overscan_winmat = overscan_winmat(view.winmat());
  const float4x4 base_persmat = base_overscan_winmat * base_viewmat;
  const float filter_size = scene ? scene->r.gauss : 1.5f;

  const bool gtao_active = lights.gtao_enabled();
  const bool need_scene_color = has_refract_draws_ || has_blend_refract_draws_;

  prepass_depth_tx_.acquire_2d(render_extent_,
                               gpu::TextureFormat::SFLOAT_32_DEPTH,
                               GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);
  prepass_depth_ptr_ = prepass_depth_tx_;

  sample_depth_tx_.acquire_2d(render_extent_,
                              gpu::TextureFormat::SFLOAT_32_DEPTH_UINT_8,
                              GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);
  sample_color_tx_.acquire_2d(render_extent_,
                              gpu::TextureFormat::SFLOAT_16_16_16_16,
                              GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);

  if (gtao_active) {
    gtao_horizon_tx_.acquire_2d(render_extent_,
                                gpu::TextureFormat::UNORM_8_8_8_8,
                                GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);
  }
  if (need_scene_color) {
    scene_color_tx_.acquire_2d(render_extent_,
                               gpu::TextureFormat::SFLOAT_16_16_16_16,
                               GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);
    scene_color_ptr_ = scene_color_tx_;
  }

  bool has_taa_accum = false;
  gpu::Texture *resolved_tx = sample_color_tx_;

  if (is_image_render_ || is_viewport_image_render_) {
    int total_samples = 1;
    if (scene != nullptr) {
      if (is_image_render_) {
        const uint vl_samples = draw_ctx->view_layer ? draw_ctx->view_layer->samples : 0u;
        total_samples = (vl_samples > 0u) ? int(vl_samples) : scene->eevee.taa_render_samples;
      }
      else {
        total_samples = scene->eevee.taa_samples;
        if (total_samples <= 0) {
          total_samples = 64;
        }
      }
    }
    total_samples = std::max(1, total_samples);

    if (total_samples == 1) {
      sample_view_.sync(base_viewmat, base_overscan_winmat);
      render_single_sample(manager, sample_view_, sample_color_tx_, sample_depth_tx_, 0, gtao_active);
      resolved_tx = sample_color_tx_;
    }
    else {
      taa_accum_tx_.acquire_2d(render_extent_,
                               gpu::TextureFormat::SFLOAT_32_32_32_32,
                               GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);
      has_taa_accum = true;
      taa_accum_fb_.ensure(GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(taa_accum_tx_));
      taa_accum_fb_.bind();
      GPU_framebuffer_clear_color(taa_accum_fb_, double4(0.0));

      unf_sample_weight_ = 1.0f / float(total_samples);
      unf_sample_tx_ = sample_color_tx_;

      for (int s = 0; s < total_samples; s++) {
        const float2 jitter = sampling_.pixel_jitter_get(s, filter_size);
        sample_view_.sync(base_viewmat,
                          Sampling::jittered_winmat(base_overscan_winmat, render_extent_, jitter));
        render_single_sample(
            manager, sample_view_, sample_color_tx_, sample_depth_tx_, s, gtao_active);
        taa_accum_fb_.bind();
        manager.submit(taa_accum_ps_, sample_view_);
      }
      resolved_tx = taa_accum_tx_;
    }
  }
  else {
    const uint64_t deg_count = draw_ctx->depsgraph ? DEG_get_update_count(draw_ctx->depsgraph) : 0;
    bool scene_updated = assign_if_different(depsgraph_last_update_, deg_count);
    if (materials.queued_count > 0 || prev_queued_count_ > 0) {
      scene_updated = true;
    }
    prev_queued_count_ = materials.queued_count;

    if (prev_render_extent_ != render_extent_ || prev_film_transparent_ != film_transparent_ ||
        prev_gtao_params_ != lights.gtao_params())
    {
      scene_updated = true;
      taa_history_valid_ = false;
    }

    const bool view_changed = (base_viewmat != prev_viewmat_) ||
                              (base_overscan_winmat != prev_winmat_);
    const int target_samples = scene ? std::max(0, scene->eevee.taa_samples) : 16;
    const bool is_playback = draw_ctx->is_playback();

    if (target_samples == 1) {
      sample_view_.sync(base_viewmat, base_overscan_winmat);
      render_single_sample(manager, sample_view_, sample_color_tx_, sample_depth_tx_, 0, gtao_active);
      resolved_tx = sample_color_tx_;
      taa_history_valid_ = false;
      taa_sample_index_ = 0;
    }
    else {
      for (int h = 0; h < 2; h++) {
        if (taa_history_tx_[h].ensure_2d(
                gpu::TextureFormat::SFLOAT_16_16_16_16,
                render_extent_,
                GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT))
        {
          taa_history_valid_ = false;
        }
      }

      if (scene_updated || !taa_history_valid_ || is_playback) {
        taa_sample_index_ = 0;
      }
      else if (view_changed) {
        taa_sample_index_ = 0;
      }

      const float2 jitter = sampling_.pixel_jitter_get(taa_sample_index_, filter_size);
      sample_view_.sync(base_viewmat,
                        Sampling::jittered_winmat(base_overscan_winmat, render_extent_, jitter));
      render_single_sample(manager,
                           sample_view_,
                           sample_color_tx_,
                           sample_depth_tx_,
                           taa_sample_index_,
                           gtao_active);

      const bool use_reproj = view_changed && !scene_updated && taa_history_valid_ &&
                              scene != nullptr &&
                              (scene->eevee.flag & SCE_EEVEE_TAA_REPROJECTION) != 0;
      if (use_reproj) {
        const int next_idx = 1 - taa_history_idx_;
        unf_sample_tx_ = sample_color_tx_;
        unf_history_tx_ = taa_history_tx_[taa_history_idx_];
        unf_depth_tx_ = prepass_depth_tx_;
        unf_prev_persmat_ = prev_persmat_;
        taa_accum_fb_.ensure(GPU_ATTACHMENT_NONE,
                             GPU_ATTACHMENT_TEXTURE(taa_history_tx_[next_idx]));
        taa_accum_fb_.bind();
        manager.submit(taa_reproject_ps_, sample_view_);
        taa_history_idx_ = next_idx;
        taa_sample_index_ = 1;
      }
      else if (taa_sample_index_ == 0) {
        GPU_texture_copy(taa_history_tx_[taa_history_idx_], sample_color_tx_);
        taa_sample_index_ = 1;
      }
      else {
        const int next_idx = 1 - taa_history_idx_;
        unf_sample_tx_ = sample_color_tx_;
        unf_history_tx_ = taa_history_tx_[taa_history_idx_];
        unf_blend_alpha_ = 1.0f / float(taa_sample_index_ + 1);
        taa_accum_fb_.ensure(GPU_ATTACHMENT_NONE,
                             GPU_ATTACHMENT_TEXTURE(taa_history_tx_[next_idx]));
        taa_accum_fb_.bind();
        manager.submit(taa_blend_ps_, sample_view_);
        taa_history_idx_ = next_idx;
        taa_sample_index_++;
      }

      taa_history_valid_ = true;
      resolved_tx = taa_history_tx_[taa_history_idx_];

      if ((target_samples == 0 || taa_sample_index_ < target_samples) && !is_playback) {
        DRW_viewport_request_redraw();
      }
    }

    prev_viewmat_ = base_viewmat;
    prev_winmat_ = base_overscan_winmat;
    prev_persmat_ = base_persmat;
    prev_render_extent_ = render_extent_;
    prev_film_transparent_ = film_transparent_;
    prev_gtao_params_ = lights.gtao_params();
  }

  bool has_bloom_out = false;
  gpu::Texture *final_tx = resolved_tx;
  if (bloom.enabled()) {
    bloom_out_tx_.acquire_2d(render_extent_,
                             gpu::TextureFormat::SFLOAT_16_16_16_16,
                             GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);
    has_bloom_out = true;
    bloom_out_fb_.ensure(GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(bloom_out_tx_));
    bloom.render(manager, sample_view_, resolved_tx, bloom_out_fb_);
    final_tx = bloom_out_tx_;
  }

  unf_resolve_tx_ = final_tx;
  unf_overscan_offset_ = int2(overscan_px_);
  output_fb_.ensure(GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(color));
  output_fb_.bind();
  manager.submit(film_resolve_ps_, view);

  if (depth != nullptr) {
    unf_depth_tx_ = sample_depth_tx_;
    output_depth_fb_.ensure(GPU_ATTACHMENT_TEXTURE(depth));
    output_depth_fb_.bind();
    manager.submit(film_crop_depth_ps_, view);
  }

  if (has_bloom_out) {
    bloom_out_tx_.release();
  }
  if (has_taa_accum) {
    taa_accum_tx_.release();
  }
  if (need_scene_color) {
    scene_color_tx_.release();
    scene_color_ptr_ = nullptr;
  }
  if (gtao_active) {
    gtao_horizon_tx_.release();
  }
  gtao_horizon_ptr_ = nullptr;
  sample_color_tx_.release();
  sample_depth_tx_.release();
  prepass_depth_tx_.release();
  prepass_depth_ptr_ = nullptr;
}

void Instance::draw(Manager &manager)
{
  DefaultTextureList *dtxl = draw_ctx->viewport_texture_list_get();
  draw_to(manager, View::default_get(), dtxl->color, dtxl->depth);
}

}  // namespace blender::goo
