/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#pragma once

#include "DNA_world_types.h"

#include "DRW_render.hh"

#include "draw_manager.hh"
#include "draw_pass.hh"
#include "draw_view.hh"

#include "goo_bloom.hh"
#include "goo_light.hh"
#include "goo_lookdev.hh"
#include "goo_material.hh"
#include "goo_probe.hh"
#include "goo_render_pass.hh"
#include "goo_sampling.hh"
#include "goo_ssr.hh"

namespace blender {
struct ModifierData;
struct ParticleSystem;
struct RenderLayer;
struct RenderResult;
struct ViewLayer;
}  // namespace blender

namespace blender::goo {

using namespace draw;

/**
 * Engine instance. One per viewport (or per F12 render).
 *
 * Phase-0 skeleton: draws mesh surfaces with a trivial lit shader so that the
 * engine plumbing (registration, sync, submission, render-to-image) is proven on 5.x
 * before porting the EEVEE-Legacy based Goo pipeline on top of it.
 */
class Instance : public DrawEngine {
 public:
  const DRWContext *draw_ctx = nullptr;

  /** Depth prepass (opaque / clip / hashed geometry) for Curvature and ScreenspaceInfo. */
  PassMain prepass_ps_ = {"Goo.Prepass"};
  PassMain::Sub *prepass_cull_sub_ = nullptr;
  PassMain::Sub *prepass_nocull_sub_ = nullptr;
  PassMain::Sub *prepass_curves_sub_ = nullptr;
  Map<GPUMaterial *, PassMain::Sub *> prepass_mat_subs_;

  /** Surface pass. One sub-pass per material. */
  PassMain surface_ps_ = {"Goo.Surface"};
  Map<const Material *, PassMain::Sub *> material_subs_;

  /** Second-pass surface draw for materials reading scene color/depth (`ScreenspaceInfo`). */
  PassMain refract_ps_ = {"Goo.Refract"};
  Map<GPUMaterial *, PassMain::Sub *> refract_subs_;
  bool has_refract_draws_ = false;

  /** Alpha-blended pass sorted back-to-front per draw. */
  PassSortable blend_ps_ = {"Goo.Blend"};
  bool has_blend_draws_ = false;
  bool has_blend_refract_draws_ = false;

  /** Procedural World background pass (and Env pass-only fallback under film_transparent). */
  PassSimple world_ps_ = {"Goo.World"};
  PassSimple world_env_only_ps_ = {"Goo.WorldEnvOnly"};
  PassSimple world_capture_ps_ = {"Goo.WorldCapture"};
  bool has_world_shader_ = false;
  float4 world_color_ = float4(0.05f, 0.05f, 0.05f, 1.0f);
  bool use_sky_ = true;
  bool use_solid_ = true;
  bool use_strand_ = true;

  /** Node-tree materials (one sub-pass per GPUMaterial). */
  MaterialModule materials;
  Map<GPUMaterial *, PassMain::Sub *> gpumat_subs_;

  /** Scene lights + shadow atlas. */
  LightModule lights;
  /** World IBL, LookDev, and Light Probes. */
  LookdevWorld lookdev_world_;
  LookdevParameters prev_lookdev_params_;
  const World *prev_world_ptr_ = nullptr;
  ProbeModule probes;
  /** Scene color/depth pyramids & Screen-Space Reflections. */
  SsrModule ssr;
  /** Render passes, Shader AOVs, and Cryptomatte. */
  RenderPassModule render_passes;
  bool is_image_render_ = false;
  bool is_viewport_image_render_ = false;

  /** Sampling / TAA / Film / Overscan state. */
  Sampling sampling_;
  View sample_view_ = {"goo.sample_view"};
  int2 output_extent_ = int2(1);
  int2 render_extent_ = int2(1);
  int overscan_px_ = 0;
  bool film_transparent_ = false;

  /** Offscreen buffers for depth prepass, GTAO, screenspace info copy, TAA, and post effects. */
  TextureFromPool prepass_depth_tx_ = {"goo.prepass_depth"};
  TextureFromPool gtao_horizon_tx_ = {"goo.gtao_horizon"};
  Texture scene_color_tx_ = {"goo.scene_color"};
  TextureFromPool sample_color_tx_ = {"goo.sample_color"};
  TextureFromPool sample_depth_tx_ = {"goo.sample_depth"};
  TextureFromPool taa_accum_tx_ = {"goo.taa_accum"};
  TextureFromPool bloom_out_tx_ = {"goo.bloom_out"};
  Texture dummy_depth_tx_ = {"goo.dummy_depth"};
  Texture dummy_color_tx_ = {"goo.dummy_color"};
  Texture dummy_horizon_tx_ = {"goo.dummy_horizon"};
  gpu::Texture *prepass_depth_ptr_ = nullptr;
  gpu::Texture *scene_color_ptr_ = nullptr;
  gpu::Texture *gtao_horizon_ptr_ = nullptr;

  /** Persistent viewport TAA history ping-pong buffers & camera tracking. */
  Texture taa_history_tx_[2] = {{"goo.taa_hist_0"}, {"goo.taa_hist_1"}};
  int taa_history_idx_ = 0;
  int taa_sample_index_ = 0;
  bool taa_history_valid_ = false;
  uint64_t depsgraph_last_update_ = 0;
  int prev_queued_count_ = 0;
  float4x4 prev_viewmat_ = float4x4::identity();
  float4x4 prev_winmat_ = float4x4::identity();
  float4x4 prev_persmat_ = float4x4::identity();
  int2 prev_render_extent_ = int2(0);
  bool prev_film_transparent_ = false;
  float4 prev_gtao_params_ = float4(0.0f);

  Framebuffer prepass_fb_ = {"goo.prepass_fb"};
  Framebuffer gtao_fb_ = {"goo.gtao_fb"};
  Framebuffer main_fb_ = {"goo.main"};
  Framebuffer taa_accum_fb_ = {"goo.taa_accum_fb"};
  Framebuffer bloom_out_fb_ = {"goo.bloom_out_fb"};
  Framebuffer output_fb_ = {"goo.output"};
  Framebuffer output_depth_fb_ = {"goo.output_depth_fb"};

  PassSimple gtao_ps_ = {"Goo.GTAO"};
  PassSimple taa_accum_ps_ = {"Goo.TaaAccum"};
  PassSimple taa_blend_ps_ = {"Goo.TaaBlend"};
  PassSimple taa_reproject_ps_ = {"Goo.TaaReproject"};
  PassSimple film_resolve_ps_ = {"Goo.FilmResolve"};
  PassSimple film_crop_depth_ps_ = {"Goo.FilmCropDepth"};

  gpu::Texture *unf_sample_tx_ = nullptr;
  gpu::Texture *unf_history_tx_ = nullptr;
  gpu::Texture *unf_resolve_tx_ = nullptr;
  gpu::Texture *unf_depth_tx_ = nullptr;
  float unf_sample_weight_ = 1.0f;
  float unf_blend_alpha_ = 1.0f;
  float4x4 unf_prev_persmat_ = float4x4::identity();
  int2 unf_overscan_offset_ = int2(0);

  Bloom bloom;

  StringRefNull name_get() final
  {
    return "Goo Engine";
  }

  void init() final;
  void begin_sync() final;
  void object_sync(ObjectRef &ob_ref, Manager &manager) final;
  void end_sync() final;
  void draw(Manager &manager) final;

  /** Draw into the given color/depth textures (used by viewport and F12). */
  void draw_to(Manager &manager, View &view, gpu::Texture *color, gpu::Texture *depth);

  void read_render_passes(RenderLayer *layer,
                          const ViewLayer *view_layer,
                          const char *viewname,
                          const rcti &rect);
  void store_metadata(RenderResult *render_result);

 private:
  void compute_extents(const Scene *scene, int2 output_extent);
  float4x4 overscan_winmat(const float4x4 &base_winmat) const;
  void sync_curves_object(ObjectRef &ob_ref,
                          Manager &manager,
                          ParticleSystem *psys,
                          ModifierData *md,
                          ResourceHandleRange handle);
  void render_single_sample(Manager &manager,
                            View &sample_view,
                            gpu::Texture *dst_color,
                            gpu::Texture *dst_depth,
                            int sample_index,
                            bool gtao_active,
                            bool ssr_active,
                            bool need_scene_color);
  PassMain::Sub &material_sub_get(const Material *ma);
  PassMain::Sub &gpumat_sub_get(Manager &manager,
                                GPUMaterial *gpumat,
                                const Material *ma,
                                bool is_curves = false);
  PassMain::Sub &refract_sub_get(Manager &manager,
                                 GPUMaterial *gpumat,
                                 const Material *ma,
                                 bool is_curves = false);
  PassMain::Sub &prepass_mat_sub_get(Manager &manager,
                                     GPUMaterial *gpumat,
                                     const Material *ma,
                                     bool is_curves = false);
  void configure_material_sub(PassMain::Sub &sub,
                              Manager &manager,
                              GPUMaterial *gpumat,
                              const Material *ma,
                              bool is_refract,
                              bool is_prepass,
                              bool is_blend = false,
                              bool is_curves = false);
};

}  // namespace blender::goo
