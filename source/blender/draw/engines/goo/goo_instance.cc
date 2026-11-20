/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include "BKE_material.hh"
#include "BKE_object.hh"

#include "BLI_math_matrix.hh"

#include "DNA_light_types.h"
#include "DNA_material_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_world_types.h"

#include "GPU_framebuffer.hh"
#include "GPU_shader.hh"

#include "draw_cache.hh"
#include "draw_common.hh"
#include "draw_view_data.hh"

#include "../eevee/eevee_defines.hh"

#include "goo_instance.hh"
#include "goo_shader.hh"

namespace blender::goo {

void Instance::init()
{
  draw_ctx = DRW_context_get();

  const World *world = draw_ctx->scene ? draw_ctx->scene->world : nullptr;
  if (world) {
    world_color_ = float4(world->horr, world->horg, world->horb, 1.0f);
  }
  else {
    world_color_ = float4(0.05f, 0.05f, 0.05f, 1.0f);
  }

  is_image_render_ = draw_ctx->is_image_render();

  const float2 size = draw_ctx->viewport_size_get();
  bloom.init(draw_ctx->scene, int2(size));
}

void Instance::begin_sync()
{
  material_subs_.clear();
  gpumat_subs_.clear();
  materials.begin_sync();
  light_count_ = 0;

  surface_ps_.init();
  surface_ps_.state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH |
                        DRW_STATE_DEPTH_LESS_EQUAL | DRW_STATE_CULL_BACK);
  surface_ps_.shader_set(ShaderModule::get().surface_flat.get());
  surface_ps_.push_constant("world_color", world_color_);

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

PassMain::Sub &Instance::gpumat_sub_get(Manager &manager, GPUMaterial *gpumat)
{
  return *gpumat_subs_.lookup_or_add_cb(gpumat, [&]() {
    const Material *ma = GPU_material_get_material(gpumat);
    PassMain::Sub &sub = surface_ps_.sub(ma ? ma->id.name + 2 : "Material");
    sub.material_set(manager, gpumat, !is_image_render_);
    sub.bind_texture(RBUFS_UTILITY_TEX_SLOT, ShaderModule::get().utility_tx);
    /* References: light data is only complete after all objects are synced. */
    sub.push_constant("goo_lights", lights_data_, max_lights * 2);
    sub.push_constant("goo_light_count", &light_count_);
    sub.push_constant("goo_world_color", &world_color_);
    return &sub;
  });
}

void Instance::light_sync(Object *ob)
{
  if (light_count_ >= max_lights) {
    return;
  }
  const Light *la = &DRW_object_get_data_for_drawing<const Light>(*ob);
  const float3 color = float3(la->r, la->g, la->b);
  const float4x4 &mat = ob->object_to_world();
  float4 &a = lights_data_[light_count_ * 2 + 0];
  float4 &b = lights_data_[light_count_ * 2 + 1];
  if (la->type == LA_SUN) {
    /* Direction towards the light (sun shines along its local -Z). */
    a = float4(math::normalize(mat.z_axis()), 0.0f);
    b = float4(color * la->energy / float(M_PI), la->radius);
  }
  else {
    a = float4(mat.location(), 1.0f);
    /* Radiant power [W] to radiance-like scale used by the stand-in Lambert term. */
    b = float4(color * la->energy / (4.0f * float(M_PI) * float(M_PI)), la->radius);
  }
  light_count_++;
}

void Instance::object_sync(ObjectRef &ob_ref, Manager &manager)
{
  Object *ob = ob_ref.object;
  if (ob->type == OB_LAMP) {
    if (DRW_object_visibility_in_active_context(ob) & OB_VISIBLE_SELF) {
      light_sync(ob);
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
    Span<gpu::Batch *> geoms = DRW_cache_mesh_surface_shaded_get(
        ob, Span<const GPUMaterial *>(const_cast<const GPUMaterial **>(gpumats.data()), mat_count));
    for (int i : geoms.index_range()) {
      if (geoms[i] == nullptr) {
        continue;
      }
      gpumat_sub_get(manager, gpumats[i]).draw(geoms[i], handle);
    }
    return;
  }

  /* Fallback while shaders compile (or on compile failure): flat shading. */
  gpu::Batch *geom = DRW_cache_mesh_surface_get(ob);
  if (geom == nullptr) {
    return;
  }
  const Material *ma = BKE_object_material_get_eval(ob, 1);
  material_sub_get(ma).draw(geom, handle);
}

void Instance::end_sync()
{
  if (materials.queued_count > 0 && !is_image_render_) {
    /* Keep redrawing until all materials are compiled. */
    DRW_viewport_request_redraw();
  }
}

void Instance::draw_to(Manager &manager, View &view, gpu::Texture *color, gpu::Texture *depth)
{
  output_fb_.ensure(GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(color));

  if (!bloom.enabled()) {
    /* No post effect: render straight into the output. */
    main_fb_.ensure(GPU_ATTACHMENT_TEXTURE(depth), GPU_ATTACHMENT_TEXTURE(color));
    main_fb_.bind();
    GPU_framebuffer_clear_color_depth(main_fb_, double4(world_color_), 1.0f);
    manager.submit(surface_ps_, view);
    return;
  }

  /* Render surfaces into an intermediate buffer that post effects can sample. */
  const int2 extent = int2(GPU_texture_width(color), GPU_texture_height(color));
  color_tx_.acquire_2d(extent,
                       gpu::TextureFormat::SFLOAT_16_16_16_16,
                       GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);
  main_fb_.ensure(GPU_ATTACHMENT_TEXTURE(depth), GPU_ATTACHMENT_TEXTURE(color_tx_));
  main_fb_.bind();
  GPU_framebuffer_clear_color_depth(main_fb_, double4(world_color_), 1.0f);
  manager.submit(surface_ps_, view);

  bloom.render(manager, view, color_tx_, output_fb_);

  color_tx_.release();
}

void Instance::draw(Manager &manager)
{
  DefaultTextureList *dtxl = draw_ctx->viewport_texture_list_get();
  draw_to(manager, View::default_get(), dtxl->color, dtxl->depth);
}

}  // namespace blender::goo
