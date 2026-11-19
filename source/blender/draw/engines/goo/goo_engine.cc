/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include "BKE_report.hh"

#include "BLI_math_matrix.hh"
#include "BLI_rect.h"

#include "BLT_translation.hh"

#include "DEG_depsgraph_query.hh"

#include "GPU_framebuffer.hh"
#include "GPU_texture.hh"

#include "RE_engine.h"
#include "RE_pipeline.h"

#include "IMB_imbuf_types.hh"

#include "draw_view_data.hh"

#include "goo_engine.h" /* Own include. */
#include "goo_instance.hh"
#include "goo_shader.hh"

namespace blender {

namespace goo {

/* -------------------------------------------------------------------- */
/** \name Shader module
 * \{ */

static ShaderModule *g_shader_module = nullptr;

ShaderModule &ShaderModule::get()
{
  if (g_shader_module == nullptr) {
    g_shader_module = new ShaderModule();
  }
  return *g_shader_module;
}

void ShaderModule::module_free()
{
  delete g_shader_module;
  g_shader_module = nullptr;
}

/** \} */

DrawEngine *Engine::create_instance()
{
  return new Instance();
}

void Engine::free_static()
{
  ShaderModule::module_free();
}

/* -------------------------------------------------------------------- */
/** \name Final render (F12)
 * \{ */

static bool render_framebuffers_init(const DRWContext *draw_ctx)
{
  const float2 viewport_size = draw_ctx->viewport_size_get();
  const int2 size = {int(viewport_size.x), int(viewport_size.y)};

  DefaultTextureList *dtxl = draw_ctx->viewport_texture_list_get();
  if (dtxl->color == nullptr) {
    eGPUTextureUsage usage = GPU_TEXTURE_USAGE_GENERAL;
    dtxl->color = GPU_texture_create_2d(
        "goo.color", size.x, size.y, 1, gpu::TextureFormat::SFLOAT_16_16_16_16, usage, nullptr);
    dtxl->depth = GPU_texture_create_2d("goo.depth",
                                        size.x,
                                        size.y,
                                        1,
                                        gpu::TextureFormat::SFLOAT_32_DEPTH_UINT_8,
                                        usage,
                                        nullptr);
  }
  if (!(dtxl->depth && dtxl->color)) {
    return false;
  }

  DefaultFramebufferList *dfbl = draw_ctx->viewport_framebuffer_list_get();
  GPU_framebuffer_ensure_config(
      &dfbl->default_fb,
      {GPU_ATTACHMENT_TEXTURE(dtxl->depth), GPU_ATTACHMENT_TEXTURE(dtxl->color)});
  return GPU_framebuffer_check_valid(dfbl->default_fb, nullptr);
}

static void render_to_image(RenderEngine *engine, RenderLayer *layer, const rcti rect)
{
  const DRWContext *draw_ctx = DRW_context_get();
  if (!render_framebuffers_init(draw_ctx)) {
    RE_engine_report(engine, RPT_ERROR, "Goo Engine: failed to allocate GPU buffers");
    return;
  }

  Depsgraph *depsgraph = draw_ctx->depsgraph;
  Object *camera_ob = DEG_get_evaluated(depsgraph, RE_GetCamera(engine->re));

  float4x4 winmat, viewinv;
  RE_GetCameraWindow(engine->re, camera_ob, winmat.ptr());
  RE_GetCameraModelMatrix(engine->re, camera_ob, viewinv.ptr());
  const float4x4 viewmat = math::invert(viewinv);

  DRW_cache_restart();
  draw::View::default_set(viewmat, winmat);

  Instance instance;
  instance.init();

  draw::Manager &manager = *DRW_manager_get();
  manager.begin_sync();
  instance.begin_sync();
  DRW_render_object_iter(
      engine,
      depsgraph,
      [&](draw::ObjectRef &ob_ref, RenderEngine * /*engine*/, Depsgraph * /*depsgraph*/) {
        instance.object_sync(ob_ref, manager);
      });
  instance.end_sync();
  manager.end_sync();

  DRW_submission_start();
  DefaultTextureList *dtxl = draw_ctx->viewport_texture_list_get();
  DefaultFramebufferList *dfbl = draw_ctx->viewport_framebuffer_list_get();
  instance.draw_to(manager, draw::View::default_get(), dtxl->color, dtxl->depth);
  DRW_submission_end();

  const char *viewname = RE_GetActiveRenderView(engine->re);
  RenderPass *rp = RE_pass_find_by_name(layer, RE_PASSNAME_COMBINED, viewname);
  if (rp) {
    GPU_framebuffer_bind(dfbl->default_fb);
    GPU_framebuffer_read_color(dfbl->default_fb,
                               rect.xmin,
                               rect.ymin,
                               BLI_rcti_size_x(&rect),
                               BLI_rcti_size_y(&rect),
                               4,
                               0,
                               GPU_DATA_FLOAT,
                               rp->ibuf->float_data_for_write());
  }
}

static void render_update_passes(RenderEngine *engine, Scene *scene, ViewLayer *view_layer)
{
  if (view_layer->passflag & SCE_PASS_COMBINED) {
    RE_engine_register_pass(engine, scene, view_layer, RE_PASSNAME_COMBINED, 4, "RGBA", SOCK_RGBA);
  }
}

static void render(RenderEngine *engine, Depsgraph *depsgraph)
{
  DRW_render_to_image(engine, depsgraph, render_to_image, [](RenderResult *) {});
}

/** \} */

}  // namespace goo

RenderEngineType DRW_engine_viewport_goo_type = {
    /*next*/ nullptr,
    /*prev*/ nullptr,
    /*idname*/ "GOO_ENGINE",
    /*name*/ N_("Goo Engine"),
    /*flag*/ RE_INTERNAL | RE_USE_PREVIEW | RE_USE_STEREO_VIEWPORT | RE_USE_GPU_CONTEXT,
    /*update*/ nullptr,
    /*render*/ &goo::render,
    /*render_frame_finish*/ nullptr,
    /*draw*/ nullptr,
    /*bake*/ nullptr,
    /*view_update*/ nullptr,
    /*view_draw*/ nullptr,
    /*update_script_node*/ nullptr,
    /*update_render_passes*/ &goo::render_update_passes,
    /*update_custom_camera*/ nullptr,
    /*draw_engine*/ nullptr,
    /*rna_ext*/
    {
        /*data*/ nullptr,
        /*srna*/ nullptr,
        /*call*/ nullptr,
    },
};

}  // namespace blender
