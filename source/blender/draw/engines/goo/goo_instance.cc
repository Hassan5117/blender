/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include "BKE_material.hh"
#include "BKE_object.hh"

#include "DNA_material_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_world_types.h"

#include "GPU_framebuffer.hh"
#include "GPU_shader.hh"

#include "draw_cache.hh"
#include "draw_common.hh"
#include "draw_view_data.hh"

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

  const float2 size = draw_ctx->viewport_size_get();
  bloom.init(draw_ctx->scene, int2(size));
}

void Instance::begin_sync()
{
  material_subs_.clear();

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

void Instance::object_sync(ObjectRef &ob_ref, Manager &manager)
{
  Object *ob = ob_ref.object;
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

  gpu::Batch *geom = DRW_cache_mesh_surface_get(ob);
  if (geom == nullptr) {
    return;
  }

  const Material *ma = BKE_object_material_get_eval(ob, 1);
  ResourceHandleRange handle = manager.unique_handle(ob_ref);
  material_sub_get(ma).draw(geom, handle);
}

void Instance::end_sync() {}

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
