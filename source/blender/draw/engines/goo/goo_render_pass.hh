/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Render passes (Depth, Mist, Normal, Position, Diffuse/Glossy Direct & Color, Emit, Env,
 * Shadow, AO), Shader AOVs, and Cryptomatte (Object, Asset, Material) accumulation and readback.
 */

#pragma once

#include <string>

#include "BLI_rect.h"
#include "BLI_vector.hh"

#include "BKE_cryptomatte.hh"

#include "DRW_gpu_wrapper.hh"
#include "DRW_render.hh"
#include "draw_handle.hh"
#include "draw_manager.hh"
#include "draw_pass.hh"
#include "draw_view.hh"

#include "goo_defines.hh"
#include "goo_shader_shared.hh"

namespace blender {
struct Material;
struct Object;
struct RenderEngine;
struct RenderLayer;
struct RenderResult;
struct Scene;
struct View3D;
struct ViewLayer;
struct ViewLayerAOV;
struct World;
}  // namespace blender

namespace blender::goo {

using namespace draw;

class RenderPassModule {
 private:
  UniformBuffer<GooPassBlock> pass_buf_ = {"GooPassBlock"};
  draw::StorageArrayBuffer<float2, 16> crypto_object_buf_;

  bke::cryptomatte::CryptomatteSessionPtr session_;
  bke::cryptomatte::CryptomatteLayer *object_layer_ = nullptr;
  bke::cryptomatte::CryptomatteLayer *asset_layer_ = nullptr;
  bke::cryptomatte::CryptomatteLayer *material_layer_ = nullptr;

  TextureFromPool sample_rp_color_tx_ = {"goo.sample_rp_color"};
  TextureFromPool sample_rp_value_tx_ = {"goo.sample_rp_value"};
  TextureFromPool sample_rp_crypto_tx_ = {"goo.sample_rp_crypto"};

  Texture accum_rp_color_tx_ = {"goo.accum_rp_color"};
  Texture accum_rp_value_tx_ = {"goo.accum_rp_value"};
  Texture accum_crypto_tx_ = {"goo.accum_rp_crypto"};

  Texture dummy_rp_color_tx_ = {"goo.dummy_rp_color"};
  Texture dummy_rp_value_tx_ = {"goo.dummy_rp_value"};
  Texture dummy_rp_crypto_tx_ = {"goo.dummy_rp_crypto"};
  Texture dummy_accum_color_tx_ = {"goo.dummy_accum_color"};
  Texture dummy_accum_value_tx_ = {"goo.dummy_accum_value"};
  Texture dummy_accum_crypto_tx_ = {"goo.dummy_accum_crypto"};

  gpu::Texture *rp_color_ptr_ = nullptr;
  gpu::Texture *rp_value_ptr_ = nullptr;
  gpu::Texture *rp_crypto_ptr_ = nullptr;

  gpu::Texture *accum_sample_color_ptr_ = nullptr;
  gpu::Texture *accum_sample_value_ptr_ = nullptr;
  gpu::Texture *accum_sample_crypto_ptr_ = nullptr;
  gpu::Texture *accum_sample_depth_ptr_ = nullptr;
  gpu::Texture *accum_out_color_ptr_ = nullptr;
  gpu::Texture *accum_out_value_ptr_ = nullptr;
  gpu::Texture *accum_out_crypto_ptr_ = nullptr;
  gpu::Texture *display_color_ptr_ = nullptr;
  gpu::Texture *display_value_ptr_ = nullptr;
  gpu::Texture *display_crypto_ptr_ = nullptr;

  PassSimple display_ps_ = {"Goo.PassDisplay"};

  int2 unf_output_extent_ = int2(1);
  int2 unf_overscan_offset_ = int2(0);
  int unf_sample_index_ = 0;
  float unf_sample_weight_ = 1.0f;
  int unf_crypto_layer_count_ = 0;
  int unf_crypto_levels_ = 6;
  int unf_display_mode_ = GOO_DISPLAY_MODE_COMBINED;
  int unf_display_layer_ = 0;
  float unf_depth_scale_ = 0.05f;

  Vector<ViewLayerAOV *> active_color_aovs_;
  Vector<ViewLayerAOV *> active_value_aovs_;

  int2 output_extent_ = int2(1);
  bool ao_pass_enabled_ = false;
  float ao_distance_ = 0.2f;
  int crypto_layer_count_ = 0;
  bool has_sample_color_ = false;
  bool has_sample_value_ = false;
  bool has_sample_crypto_ = false;
  bool has_accum_color_ = false;
  bool has_accum_value_ = false;
  bool has_accum_crypto_ = false;

  float register_cryptomatte_id(int crypto_flag_bit, const ID &id) const;

 public:
  void init(const Scene *scene,
            const ViewLayer *view_layer,
            const View3D *v3d,
            bool is_image_render,
            float4 world_fallback,
            bool has_world_shader,
            bool use_sky);

  void begin_sync(const ViewLayer *view_layer, bool is_image_render);
  void sync_object(const Object *ob, ResourceHandleRange handle);
  float sync_material(const Material *ma);
  void end_sync();

  void acquire_buffers(int2 output_extent, int2 render_extent);
  void release_sample_buffers();

  template<typename PassT> void bind_resources(PassT &pass)
  {
    pass.bind_ubo(GOO_PASS_UBO_SLOT, pass_buf_);
    pass.bind_ssbo(GOO_CRYPTO_BUF_SLOT, &crypto_object_buf_);
    pass.bind_image(GOO_RP_COLOR_IMG_SLOT, &rp_color_ptr_);
    pass.bind_image(GOO_RP_VALUE_IMG_SLOT, &rp_value_ptr_);
    pass.bind_image(GOO_RP_CRYPTO_IMG_SLOT, &rp_crypto_ptr_);
  }

  void clear_sample_buffers();
  void accumulate_sample(Manager &manager,
                         View &sample_view,
                         gpu::Texture *sample_depth_tx,
                         int sample_index,
                         int total_samples,
                         int overscan_px);
  void finalize_passes(Manager &manager);
  bool is_display_override() const
  {
    return unf_display_mode_ != GOO_DISPLAY_MODE_COMBINED;
  }
  void display_viewport_pass(Manager &manager, View &view, Framebuffer &output_fb);

  bool ao_pass_enabled() const
  {
    return ao_pass_enabled_;
  }
  float ao_distance() const
  {
    return ao_distance_;
  }
  bool world_pass_needed() const
  {
    return pass_buf_.env_id >= 0 || pass_buf_.aov_color_len > 0 || pass_buf_.aov_value_len > 0;
  }
  bool any_pass_active() const;

  void read_render_passes(RenderLayer *layer,
                          const ViewLayer *view_layer,
                          const char *viewname,
                          const rcti &rect);
  void store_metadata(RenderResult *rr);

  static void update_passes(RenderEngine *engine, Scene *scene, ViewLayer *view_layer);
  static Vector<std::string> cryptomatte_pass_names(const char *base_name, int levels);
};

}  // namespace blender::goo
