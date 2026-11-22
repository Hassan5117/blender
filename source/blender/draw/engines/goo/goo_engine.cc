/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include <algorithm>
#include <cmath>
#include <cstring>

#include "BKE_colortools.hh"
#include "BKE_report.hh"

#include "BLI_math_base.hh"
#include "BLI_math_matrix.hh"
#include "BLI_rect.h"
#include "BLI_vector.hh"

#include "BLT_translation.hh"

#include "DEG_depsgraph_query.hh"

#include "DNA_color_types.h"
#include "DNA_layer_types.h"
#include "DNA_scene_types.h"

#include "GPU_framebuffer.hh"
#include "GPU_texture.hh"

#include "RE_engine.h"
#include "RE_pipeline.h"

#include "IMB_imbuf_types.hh"

#include "draw_view_data.hh"

#include "../eevee/eevee_defines.hh"
#include "../eevee/eevee_lut.hh"

#include "goo_engine.h" /* Own include. */
#include "goo_instance.hh"
#include "goo_sampling.hh"
#include "goo_shader.hh"

namespace blender {

namespace goo {

/* -------------------------------------------------------------------- */
/** \name Utility Texture & Shader module
 * \{ */

UtilityTexture::UtilityTexture()
    : draw::Texture("GooUtilityTx",
                    gpu::TextureFormat::SFLOAT_16_16_16_16,
                    GPU_TEXTURE_USAGE_SHADER_READ,
                    int2(UTIL_TEX_SIZE),
                    UTIL_BSDF_LAYER + UTIL_BSDF_LAYER_COUNT,
                    nullptr)
{
  struct Layer {
    float4 data[UTIL_TEX_SIZE][UTIL_TEX_SIZE];
  };
  constexpr int lut_size = UTIL_TEX_SIZE;
  constexpr int layer_count = UTIL_BSDF_LAYER + UTIL_BSDF_LAYER_COUNT;
  Vector<Layer> data(layer_count);
  {
    Layer &layer = data[UTIL_BLUE_NOISE_LAYER];
    memcpy(layer.data, eevee::lut::blue_noise, sizeof(layer));
  }
  {
    Layer &layer = data[UTIL_SSS_TRANSMITTANCE_PROFILE_LAYER];
    for (auto y : IndexRange(lut_size)) {
      for (auto x : IndexRange(lut_size)) {
        layer.data[y][x][0] = eevee::lut::burley_sss_profile[x][0];
        layer.data[y][x][1] = eevee::lut::random_walk_sss_profile[x][0];
        layer.data[y][x][2] = 0.0f;
        layer.data[y][x][UTIL_DISK_INTEGRAL_COMP] = eevee::lut::ltc_disk_integral[y][x][0];
      }
    }
  }
  {
    Layer &layer = data[UTIL_LTC_MAT_LAYER];
    memcpy(layer.data, eevee::lut::ltc_mat_ggx, sizeof(layer));
  }
  {
    Layer &layer = data[UTIL_BRDF_LAYER];
    for (auto x : IndexRange(lut_size)) {
      for (auto y : IndexRange(lut_size)) {
        layer.data[y][x][0] = eevee::lut::brdf_ggx[y][x][0];
        layer.data[y][x][1] = eevee::lut::brdf_ggx[y][x][1];
        layer.data[y][x][2] = eevee::lut::brdf_ggx[y][x][2];
        layer.data[y][x][3] = 0.0f;
      }
    }
  }
  {
    for (auto layer_id : IndexRange(16)) {
      Layer &layer = data[UTIL_BSDF_LAYER + layer_id];
      for (auto x : IndexRange(lut_size)) {
        for (auto y : IndexRange(lut_size)) {
          layer.data[y][x][0] = eevee::lut::bsdf_ggx[layer_id][y][x][0];
          layer.data[y][x][1] = eevee::lut::bsdf_ggx[layer_id][y][x][1];
          layer.data[y][x][2] = eevee::lut::bsdf_ggx[layer_id][y][x][2];
          layer.data[y][x][3] = eevee::lut::btdf_ggx[layer_id][y][x][0];
        }
      }
    }
  }
  GPU_texture_update_mipmap(*this, 0, GPU_DATA_FLOAT, data.data());
}

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

static void render_update_passes(RenderEngine *engine, Scene *scene, ViewLayer *view_layer)
{
  RenderPassModule::update_passes(engine, scene, view_layer);
}

static void render(RenderEngine *engine, Depsgraph *depsgraph)
{
  Instance *instance = nullptr;

  auto render_to_image_cb = [&](RenderEngine *eng, RenderLayer *layer, const rcti rect) {
    Render *re_ptr = eng->re;
    const DRWContext *draw_ctx = DRW_context_get();
    if (!render_framebuffers_init(draw_ctx)) {
      RE_engine_report(eng, RPT_ERROR, "Goo Engine: failed to allocate GPU buffers");
      return;
    }

    const Scene *scene = draw_ctx->scene;
    const ViewLayer *vl = draw_ctx->view_layer;
    const int initial_frame = scene ? scene->r.cfra : 1;
    const float initial_subframe = scene ? scene->r.subframe : 0.0f;

    const bool mblur_enabled = (scene != nullptr) && ((scene->r.mode & R_MBLUR) != 0) &&
                               (vl == nullptr || (vl->layflag & SCE_LAY_MOTION_BLUR) != 0) &&
                               (scene->r.motion_blur_shutter > 1e-5f);

    const char *viewname = RE_GetActiveRenderView(re_ptr);
    RenderPass *rp = RE_pass_find_by_name(layer, RE_PASSNAME_COMBINED, viewname);

    if (!mblur_enabled) {
      Object *camera_ob = DEG_get_evaluated(depsgraph, RE_GetCamera(re_ptr));

      float4x4 winmat, viewinv;
      RE_GetCameraWindow(re_ptr, camera_ob, winmat.ptr());
      RE_GetCameraModelMatrix(re_ptr, camera_ob, viewinv.ptr());
      const float4x4 viewmat = math::invert(viewinv);

      DRW_cache_restart();
      draw::View::default_set(viewmat, winmat);

      delete instance;
      instance = new Instance();
      instance->set_camera_object(camera_ob);
      instance->init();

      draw::Manager &manager = *DRW_manager_get();
      manager.begin_sync();
      instance->begin_sync();
      DRW_render_object_iter(
          eng,
          depsgraph,
          [&](draw::ObjectRef &ob_ref, RenderEngine * /*engine*/, Depsgraph * /*depsgraph*/) {
            instance->object_sync(ob_ref, manager);
          });
      instance->end_sync();
      manager.end_sync();

      DRW_submission_start();
      DefaultTextureList *dtxl = draw_ctx->viewport_texture_list_get();
      DefaultFramebufferList *dfbl = draw_ctx->viewport_framebuffer_list_get();
      instance->draw_to(manager, draw::View::default_get(), dtxl->color, dtxl->depth);
      DRW_submission_end();

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
      instance->read_render_passes(layer, draw_ctx->view_layer, viewname, rect);
      return;
    }

    const int steps = std::max(1, scene->eevee.motion_blur_steps);
    Vector<float> time_steps(steps * 2 + 1);
    {
      Scene *scene_mut = const_cast<Scene *>(scene);
      BKE_curvemapping_changed(&scene_mut->r.mblur_shutter_curve, false);
      Vector<float> cdf(CM_TABLE);
      Sampling::cdf_from_curvemapping(scene->r.mblur_shutter_curve, cdf);
      Sampling::cdf_invert(cdf, time_steps);

      const float base_time = float(initial_frame) + initial_subframe;
      for (float &t : time_steps) {
        switch (scene->r.motion_blur_position) {
          case SCE_MB_START:
            break;
          case SCE_MB_CENTER:
            t -= 0.5f;
            break;
          case SCE_MB_END:
            t -= 1.0f;
            break;
        }
        t = t * scene->r.motion_blur_shutter + base_time;
      }
    }

    const uint vl_samples = vl ? vl->samples : 0u;
    const int total_samples = std::max(1, (vl_samples > 0u) ? int(vl_samples) :
                                                              scene->eevee.taa_render_samples);
    const int samples_per_step = std::max(1, (total_samples + steps - 1) / steps);

    auto capture_step = [&](float t) {
      const float fl = std::floor(t);
      DRW_render_set_time(eng, depsgraph, int(fl), t - fl);
      DRW_cache_restart();

      MotionBlurModule::StepData step;
      Object *step_cam = DEG_get_evaluated(depsgraph, RE_GetCamera(re_ptr));
      float4x4 step_viewinv = float4x4::identity();
      RE_GetCameraWindow(re_ptr, step_cam, step.winmat.ptr());
      RE_GetCameraModelMatrix(re_ptr, step_cam, step_viewinv.ptr());
      step.viewmat = math::invert(step_viewinv);

      DRW_render_object_iter(
          eng,
          depsgraph,
          [&](draw::ObjectRef &ob_ref, RenderEngine * /*e*/, Depsgraph * /*d*/) {
            if (ob_ref.object == nullptr) {
              return;
            }
            const Object *orig = DEG_get_original(ob_ref.object);
            step.orig_object_mats.add_overwrite(orig, ob_ref.object->object_to_world());
            if (!ob_ref.is_range()) {
              step.object_mats.add_overwrite(draw::ObjectKey(ob_ref), ob_ref.object->object_to_world());
            }
          });
      return step;
    };

    const int w = BLI_rcti_size_x(&rect);
    const int h = BLI_rcti_size_y(&rect);
    const size_t float_count = size_t(w) * size_t(h) * 4;
    Vector<float> accum(rp ? float_count : 0, 0.0f);
    Vector<float> step_buf(rp ? float_count : 0, 0.0f);
    const float inv_steps = 1.0f / float(steps);
    const int mid_step = steps / 2;

    MotionBlurModule::StepData carried_step = capture_step(time_steps[0]);

    delete instance;
    instance = new Instance();

    for (int step_idx = 0; step_idx < steps; step_idx++) {
      MotionBlurModule::StepData next_step = capture_step(time_steps[step_idx * 2 + 2]);
      MotionBlurModule::StepData next_copy;
      next_copy.viewmat = next_step.viewmat;
      next_copy.winmat = next_step.winmat;
      next_copy.object_mats = next_step.object_mats;
      next_copy.orig_object_mats = next_step.orig_object_mats;

      const float curr_t = time_steps[step_idx * 2 + 1];
      const float curr_fl = std::floor(curr_t);
      DRW_render_set_time(eng, depsgraph, int(curr_fl), curr_t - curr_fl);
      DRW_cache_restart();

      Object *camera_ob = DEG_get_evaluated(depsgraph, RE_GetCamera(re_ptr));
      float4x4 winmat, viewinv;
      RE_GetCameraWindow(re_ptr, camera_ob, winmat.ptr());
      RE_GetCameraModelMatrix(re_ptr, camera_ob, viewinv.ptr());
      const float4x4 viewmat = math::invert(viewinv);

      draw::View::default_set(viewmat, winmat);

      instance->set_camera_object(camera_ob);
      instance->motion_blur.set_step_data(std::move(carried_step), std::move(next_step));
      instance->set_sample_override(samples_per_step, step_idx * samples_per_step);
      instance->init();

      draw::Manager &manager = *DRW_manager_get();
      manager.begin_sync();
      instance->begin_sync();
      DRW_render_object_iter(
          eng,
          depsgraph,
          [&](draw::ObjectRef &ob_ref, RenderEngine * /*engine*/, Depsgraph * /*depsgraph*/) {
            instance->object_sync(ob_ref, manager);
          });
      instance->end_sync();
      manager.end_sync();

      DRW_submission_start();
      DefaultTextureList *dtxl = draw_ctx->viewport_texture_list_get();
      DefaultFramebufferList *dfbl = draw_ctx->viewport_framebuffer_list_get();
      instance->draw_to(manager, draw::View::default_get(), dtxl->color, dtxl->depth);
      DRW_submission_end();

      if (rp) {
        GPU_framebuffer_bind(dfbl->default_fb);
        GPU_framebuffer_read_color(dfbl->default_fb,
                                   rect.xmin,
                                   rect.ymin,
                                   w,
                                   h,
                                   4,
                                   0,
                                   GPU_DATA_FLOAT,
                                   step_buf.data());
        for (size_t i = 0; i < float_count; i++) {
          accum[i] += step_buf[i] * inv_steps;
        }
      }
      if (step_idx == mid_step) {
        instance->read_render_passes(layer, draw_ctx->view_layer, viewname, rect);
      }

      carried_step = std::move(next_copy);
    }

    if (rp && rp->ibuf && rp->ibuf->float_data_for_write()) {
      std::memcpy(rp->ibuf->float_data_for_write(), accum.data(), float_count * sizeof(float));
    }

    DRW_render_set_time(eng, depsgraph, initial_frame, initial_subframe);
    RE_engine_frame_set(eng, initial_frame, initial_subframe);
  };

  auto store_metadata_cb = [&](RenderResult *render_result) {
    if (instance != nullptr) {
      instance->store_metadata(render_result);
    }
  };

  DRW_render_to_image(engine, depsgraph, render_to_image_cb, store_metadata_cb);
  delete instance;
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
