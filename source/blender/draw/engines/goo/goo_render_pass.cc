/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include <algorithm>
#include <cstring>

#include "BLI_hash.h"
#include "BLI_math_base.hh"
#include "BLI_rect.h"
#include "BLI_string.h"

#include "DNA_camera_types.h"
#include "DNA_layer_types.h"
#include "DNA_material_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_enums.h"
#include "DNA_scene_types.h"
#include "DNA_view3d_types.h"
#include "DNA_world_types.h"

#include "GPU_state.hh"
#include "GPU_texture.hh"

#include "IMB_imbuf_types.hh"

#include "RE_engine.h"
#include "RE_pipeline.h"

#include "goo_render_pass.hh"
#include "goo_shader.hh"

namespace blender::goo {

Vector<std::string> RenderPassModule::cryptomatte_pass_names(const char *base_name, int levels)
{
  Vector<std::string> names;
  const int count = (std::clamp(levels, 2, GOO_CRYPTO_LEVELS_MAX) + 1) / 2;
  for (int i = 0; i < count; i++) {
    char buf[64];
    SNPRINTF(buf, "%s%02d", base_name, i);
    names.append(std::string(buf));
  }
  return names;
}

void RenderPassModule::update_passes(RenderEngine *engine, Scene *scene, ViewLayer *view_layer)
{
  RE_engine_register_pass(engine, scene, view_layer, RE_PASSNAME_COMBINED, 4, "RGBA", SOCK_RGBA);

  const int passflag = view_layer->passflag;
  if (passflag & SCE_PASS_DEPTH) {
    RE_engine_register_pass(engine, scene, view_layer, RE_PASSNAME_DEPTH, 1, "Z", SOCK_FLOAT);
  }
  if (passflag & SCE_PASS_MIST) {
    RE_engine_register_pass(engine, scene, view_layer, RE_PASSNAME_MIST, 1, "Z", SOCK_FLOAT);
  }
  if (passflag & SCE_PASS_NORMAL) {
    RE_engine_register_pass(engine, scene, view_layer, RE_PASSNAME_NORMAL, 3, "XYZ", SOCK_VECTOR);
  }
  if (passflag & SCE_PASS_POSITION) {
    RE_engine_register_pass(
        engine, scene, view_layer, RE_PASSNAME_POSITION, 3, "XYZ", SOCK_VECTOR);
  }
  if (passflag & SCE_PASS_DIFFUSE_DIRECT) {
    RE_engine_register_pass(
        engine, scene, view_layer, RE_PASSNAME_DIFFUSE_DIRECT, 3, "RGB", SOCK_RGBA);
  }
  if (passflag & SCE_PASS_DIFFUSE_COLOR) {
    RE_engine_register_pass(
        engine, scene, view_layer, RE_PASSNAME_DIFFUSE_COLOR, 3, "RGB", SOCK_RGBA);
  }
  if (passflag & SCE_PASS_GLOSSY_DIRECT) {
    RE_engine_register_pass(
        engine, scene, view_layer, RE_PASSNAME_GLOSSY_DIRECT, 3, "RGB", SOCK_RGBA);
  }
  if (passflag & SCE_PASS_GLOSSY_COLOR) {
    RE_engine_register_pass(
        engine, scene, view_layer, RE_PASSNAME_GLOSSY_COLOR, 3, "RGB", SOCK_RGBA);
  }
  if (passflag & SCE_PASS_EMIT) {
    RE_engine_register_pass(engine, scene, view_layer, RE_PASSNAME_EMIT, 3, "RGB", SOCK_RGBA);
  }
  if (passflag & SCE_PASS_ENVIRONMENT) {
    RE_engine_register_pass(
        engine, scene, view_layer, RE_PASSNAME_ENVIRONMENT, 3, "RGB", SOCK_RGBA);
  }
  if (passflag & SCE_PASS_SHADOW) {
    RE_engine_register_pass(engine, scene, view_layer, RE_PASSNAME_SHADOW, 3, "RGB", SOCK_RGBA);
  }
  if (passflag & SCE_PASS_AO) {
    RE_engine_register_pass(engine, scene, view_layer, RE_PASSNAME_AO, 3, "RGB", SOCK_RGBA);
  }

  for (ViewLayerAOV &aov : view_layer->aovs) {
    if ((aov.flag & AOV_CONFLICT) != 0) {
      continue;
    }
    if (aov.type == AOV_TYPE_COLOR) {
      RE_engine_register_pass(engine, scene, view_layer, aov.name, 4, "RGBA", SOCK_RGBA);
    }
    else if (aov.type == AOV_TYPE_VALUE) {
      RE_engine_register_pass(engine, scene, view_layer, aov.name, 1, "X", SOCK_FLOAT);
    }
  }

  auto register_crypto = [&](eViewLayerCryptomatteFlags flag, const char *base_name) {
    if (view_layer->cryptomatte_flag & flag) {
      for (const std::string &name :
           cryptomatte_pass_names(base_name, view_layer->cryptomatte_levels))
      {
        RE_engine_register_pass(engine, scene, view_layer, name.c_str(), 4, "rgba", SOCK_RGBA);
      }
    }
  };
  register_crypto(VIEW_LAYER_CRYPTOMATTE_OBJECT, RE_PASSNAME_CRYPTOMATTE_OBJECT);
  register_crypto(VIEW_LAYER_CRYPTOMATTE_ASSET, RE_PASSNAME_CRYPTOMATTE_ASSET);
  register_crypto(VIEW_LAYER_CRYPTOMATTE_MATERIAL, RE_PASSNAME_CRYPTOMATTE_MATERIAL);
}

void RenderPassModule::init(const Scene *scene,
                            const ViewLayer *view_layer,
                            const View3D *v3d,
                            bool is_image_render,
                            float4 world_fallback,
                            bool has_world_shader,
                            bool use_sky)
{
  pass_buf_ = GooPassBlock{};
  pass_buf_.normal_id = -1;
  pass_buf_.position_id = -1;
  pass_buf_.diff_dir_id = -1;
  pass_buf_.diff_col_id = -1;
  pass_buf_.gloss_dir_id = -1;
  pass_buf_.gloss_col_id = -1;
  pass_buf_.emit_id = -1;
  pass_buf_.env_id = -1;
  pass_buf_.shadow_id = -1;
  pass_buf_.ao_id = -1;
  pass_buf_.aov_color_id = -1;
  pass_buf_.aov_color_len = 0;
  pass_buf_.depth_id = -1;
  pass_buf_.mist_id = -1;
  pass_buf_.aov_value_id = -1;
  pass_buf_.aov_value_len = 0;
  pass_buf_.color_layer_count = 0;
  pass_buf_.value_layer_count = 0;
  pass_buf_.crypto_flags = 0;
  pass_buf_.crypto_levels = view_layer ?
                                std::clamp(int(view_layer->cryptomatte_levels),
                                           2,
                                           GOO_CRYPTO_LEVELS_MAX) :
                                6;

  active_color_aovs_.clear();
  active_value_aovs_.clear();
  ao_pass_enabled_ = false;
  ao_distance_ = view_layer ? std::max(0.0f, view_layer->eevee.ambient_occlusion_distance) : 0.2f;
  crypto_layer_count_ = 0;
  unf_display_mode_ = GOO_DISPLAY_MODE_COMBINED;
  unf_display_layer_ = 0;
  unf_depth_scale_ = 0.05f;

  int color_layers = 0;
  int value_layers = 0;

  if (is_image_render && view_layer != nullptr) {
    const int passflag = view_layer->passflag;
    if (passflag & SCE_PASS_NORMAL) {
      pass_buf_.normal_id = color_layers++;
    }
    if (passflag & SCE_PASS_POSITION) {
      pass_buf_.position_id = color_layers++;
    }
    if (passflag & SCE_PASS_DIFFUSE_DIRECT) {
      pass_buf_.diff_dir_id = color_layers++;
    }
    if (passflag & SCE_PASS_DIFFUSE_COLOR) {
      pass_buf_.diff_col_id = color_layers++;
    }
    if (passflag & SCE_PASS_GLOSSY_DIRECT) {
      pass_buf_.gloss_dir_id = color_layers++;
    }
    if (passflag & SCE_PASS_GLOSSY_COLOR) {
      pass_buf_.gloss_col_id = color_layers++;
    }
    if (passflag & SCE_PASS_EMIT) {
      pass_buf_.emit_id = color_layers++;
    }
    if (passflag & SCE_PASS_ENVIRONMENT) {
      pass_buf_.env_id = color_layers++;
    }
    if (passflag & SCE_PASS_SHADOW) {
      pass_buf_.shadow_id = color_layers++;
    }
    if (passflag & SCE_PASS_AO) {
      pass_buf_.ao_id = color_layers++;
      ao_pass_enabled_ = true;
    }

    for (ViewLayerAOV &aov : view_layer->aovs) {
      if ((aov.flag & AOV_CONFLICT) != 0) {
        continue;
      }
      if (active_color_aovs_.size() + active_value_aovs_.size() >= GOO_AOV_MAX) {
        break;
      }
      if (aov.type == AOV_TYPE_COLOR) {
        active_color_aovs_.append(&aov);
      }
      else if (aov.type == AOV_TYPE_VALUE) {
        active_value_aovs_.append(&aov);
      }
    }

    if (passflag & SCE_PASS_DEPTH) {
      pass_buf_.depth_id = value_layers++;
    }
    if (passflag & SCE_PASS_MIST) {
      pass_buf_.mist_id = value_layers++;
    }

    if (view_layer->cryptomatte_flag & VIEW_LAYER_CRYPTOMATTE_OBJECT) {
      pass_buf_.crypto_flags |= GOO_CRYPTO_FLAG_OBJECT;
      crypto_layer_count_++;
    }
    if (view_layer->cryptomatte_flag & VIEW_LAYER_CRYPTOMATTE_ASSET) {
      pass_buf_.crypto_flags |= GOO_CRYPTO_FLAG_ASSET;
      crypto_layer_count_++;
    }
    if (view_layer->cryptomatte_flag & VIEW_LAYER_CRYPTOMATTE_MATERIAL) {
      pass_buf_.crypto_flags |= GOO_CRYPTO_FLAG_MATERIAL;
      crypto_layer_count_++;
    }
  }
  else if (!is_image_render && v3d != nullptr && v3d->shading.render_pass != EEVEE_RENDER_PASS_COMBINED) {
    const int rp = v3d->shading.render_pass;
    switch (rp) {
      case EEVEE_RENDER_PASS_DEPTH:
        pass_buf_.depth_id = value_layers++;
        unf_display_mode_ = GOO_DISPLAY_MODE_DEPTH_PASS;
        unf_display_layer_ = pass_buf_.depth_id;
        break;
      case EEVEE_RENDER_PASS_MIST:
        pass_buf_.mist_id = value_layers++;
        unf_display_mode_ = GOO_DISPLAY_MODE_VALUE_PASS;
        unf_display_layer_ = pass_buf_.mist_id;
        break;
      case EEVEE_RENDER_PASS_NORMAL:
        pass_buf_.normal_id = color_layers++;
        unf_display_mode_ = GOO_DISPLAY_MODE_COLOR_PASS;
        unf_display_layer_ = pass_buf_.normal_id;
        break;
      case EEVEE_RENDER_PASS_POSITION:
        pass_buf_.position_id = color_layers++;
        unf_display_mode_ = GOO_DISPLAY_MODE_COLOR_PASS;
        unf_display_layer_ = pass_buf_.position_id;
        break;
      case EEVEE_RENDER_PASS_DIFFUSE_LIGHT:
        pass_buf_.diff_dir_id = color_layers++;
        unf_display_mode_ = GOO_DISPLAY_MODE_COLOR_PASS;
        unf_display_layer_ = pass_buf_.diff_dir_id;
        break;
      case EEVEE_RENDER_PASS_DIFFUSE_COLOR:
        pass_buf_.diff_col_id = color_layers++;
        unf_display_mode_ = GOO_DISPLAY_MODE_COLOR_PASS;
        unf_display_layer_ = pass_buf_.diff_col_id;
        break;
      case EEVEE_RENDER_PASS_SPECULAR_LIGHT:
        pass_buf_.gloss_dir_id = color_layers++;
        unf_display_mode_ = GOO_DISPLAY_MODE_COLOR_PASS;
        unf_display_layer_ = pass_buf_.gloss_dir_id;
        break;
      case EEVEE_RENDER_PASS_SPECULAR_COLOR:
        pass_buf_.gloss_col_id = color_layers++;
        unf_display_mode_ = GOO_DISPLAY_MODE_COLOR_PASS;
        unf_display_layer_ = pass_buf_.gloss_col_id;
        break;
      case EEVEE_RENDER_PASS_EMIT:
        pass_buf_.emit_id = color_layers++;
        unf_display_mode_ = GOO_DISPLAY_MODE_COLOR_PASS;
        unf_display_layer_ = pass_buf_.emit_id;
        break;
      case EEVEE_RENDER_PASS_ENVIRONMENT:
        pass_buf_.env_id = color_layers++;
        unf_display_mode_ = GOO_DISPLAY_MODE_COLOR_PASS;
        unf_display_layer_ = pass_buf_.env_id;
        break;
      case EEVEE_RENDER_PASS_SHADOW:
        pass_buf_.shadow_id = color_layers++;
        unf_display_mode_ = GOO_DISPLAY_MODE_COLOR_PASS;
        unf_display_layer_ = pass_buf_.shadow_id;
        break;
      case EEVEE_RENDER_PASS_AO:
        pass_buf_.ao_id = color_layers++;
        ao_pass_enabled_ = true;
        unf_display_mode_ = GOO_DISPLAY_MODE_COLOR_PASS;
        unf_display_layer_ = pass_buf_.ao_id;
        break;
      case EEVEE_RENDER_PASS_AOV:
        if (view_layer != nullptr) {
          ViewLayerAOV *aov = static_cast<ViewLayerAOV *>(BLI_findstring(
              &view_layer->aovs, v3d->shading.aov_name, offsetof(ViewLayerAOV, name)));
          if (aov != nullptr && (aov->flag & AOV_CONFLICT) == 0) {
            if (aov->type == AOV_TYPE_COLOR) {
              active_color_aovs_.append(aov);
              unf_display_mode_ = GOO_DISPLAY_MODE_COLOR_PASS;
              unf_display_layer_ = color_layers;
            }
            else if (aov->type == AOV_TYPE_VALUE) {
              active_value_aovs_.append(aov);
              unf_display_mode_ = GOO_DISPLAY_MODE_VALUE_PASS;
              unf_display_layer_ = value_layers;
            }
          }
        }
        break;
      case EEVEE_RENDER_PASS_CRYPTOMATTE_OBJECT:
        pass_buf_.crypto_flags = GOO_CRYPTO_FLAG_OBJECT;
        crypto_layer_count_ = 1;
        unf_display_mode_ = GOO_DISPLAY_MODE_CRYPTO_PASS;
        unf_display_layer_ = 0;
        break;
      case EEVEE_RENDER_PASS_CRYPTOMATTE_ASSET:
        pass_buf_.crypto_flags = GOO_CRYPTO_FLAG_ASSET;
        crypto_layer_count_ = 1;
        unf_display_mode_ = GOO_DISPLAY_MODE_CRYPTO_PASS;
        unf_display_layer_ = 0;
        break;
      case EEVEE_RENDER_PASS_CRYPTOMATTE_MATERIAL:
        pass_buf_.crypto_flags = GOO_CRYPTO_FLAG_MATERIAL;
        crypto_layer_count_ = 1;
        unf_display_mode_ = GOO_DISPLAY_MODE_CRYPTO_PASS;
        unf_display_layer_ = 0;
        break;
      default:
        break;
    }
  }

  if (!active_color_aovs_.is_empty()) {
    pass_buf_.aov_color_id = color_layers;
    pass_buf_.aov_color_len = int(active_color_aovs_.size());
    color_layers += pass_buf_.aov_color_len;
  }
  if (!active_value_aovs_.is_empty()) {
    pass_buf_.aov_value_id = value_layers;
    pass_buf_.aov_value_len = int(active_value_aovs_.size());
    value_layers += pass_buf_.aov_value_len;
  }

  for (int i : active_color_aovs_.index_range()) {
    pass_buf_.aov_hash[i / 4][i % 4] = BLI_hash_string(active_color_aovs_[i]->name);
  }
  for (int i : active_value_aovs_.index_range()) {
    const int idx = pass_buf_.aov_color_len + i;
    pass_buf_.aov_hash[idx / 4][idx % 4] = BLI_hash_string(active_value_aovs_[i]->name);
  }

  pass_buf_.color_layer_count = color_layers;
  pass_buf_.value_layer_count = value_layers;

  const World *world = (view_layer && view_layer->world_override) ?
                           view_layer->world_override :
                           (scene ? scene->world : nullptr);
  float mist_start = world ? world->miststa : 5.0f;
  float mist_distance = world ? std::max(world->mistdist, 1e-4f) : 25.0f;
  int mist_type = world ? int(world->mistype) : int(WO_MIST_QUADRATIC);
  float mist_exponent = 1.0f;
  switch (mist_type) {
    case WO_MIST_QUADRATIC:
      mist_exponent = 2.0f;
      break;
    case WO_MIST_LINEAR:
      mist_exponent = 1.0f;
      break;
    case WO_MIST_INVERSE_QUADRATIC:
      mist_exponent = 0.5f;
      break;
  }
  const float clip_far = (scene && scene->camera && scene->camera->type == OB_CAMERA) ?
                             reinterpret_cast<const Camera *>(scene->camera->data)->clip_end :
                             100.0f;
  unf_depth_scale_ = (clip_far > 1e-3f) ? (1.0f / std::max(clip_far * 0.25f, 1.0f)) : 0.05f;

  const bool use_fallback_env = (!use_sky) || (!has_world_shader);
  pass_buf_.mist_params = float4(
      1.0f / mist_distance, -mist_start / mist_distance, mist_exponent, use_fallback_env ? 1.0f : 0.0f);
  pass_buf_.env_fallback = use_sky ? float4(world_fallback.xyz(), 1.0f) : float4(0.0f, 0.0f, 0.0f, 1.0f);
}

bool RenderPassModule::any_pass_active() const
{
  return pass_buf_.color_layer_count > 0 || pass_buf_.value_layer_count > 0 ||
         pass_buf_.crypto_flags != 0;
}

void RenderPassModule::begin_sync(const ViewLayer *view_layer, bool is_image_render)
{
  session_.reset();
  object_layer_ = nullptr;
  asset_layer_ = nullptr;
  material_layer_ = nullptr;

  if (pass_buf_.crypto_flags != 0 && is_image_render && view_layer != nullptr) {
    session_.reset(BKE_cryptomatte_init_from_view_layer(view_layer));
    for (const std::string &layer_name :
         bke::cryptomatte::BKE_cryptomatte_layer_names_get(*session_))
    {
      StringRef name_ref = layer_name;
      bke::cryptomatte::CryptomatteLayer *layer = bke::cryptomatte::BKE_cryptomatte_layer_get(
          *session_, layer_name);
      if (name_ref.endswith(RE_PASSNAME_CRYPTOMATTE_OBJECT)) {
        object_layer_ = layer;
      }
      else if (name_ref.endswith(RE_PASSNAME_CRYPTOMATTE_ASSET)) {
        asset_layer_ = layer;
      }
      else if (name_ref.endswith(RE_PASSNAME_CRYPTOMATTE_MATERIAL)) {
        material_layer_ = layer;
      }
    }
  }

  crypto_object_buf_.resize(16);

  const eGPUTextureUsage dummy_usage = GPU_TEXTURE_USAGE_SHADER_READ |
                                      GPU_TEXTURE_USAGE_SHADER_WRITE |
                                      GPU_TEXTURE_USAGE_ATTACHMENT;
  if (dummy_rp_color_tx_.ensure_2d_array(
          gpu::TextureFormat::SFLOAT_16_16_16_16, int2(1), 1, dummy_usage))
  {
    dummy_rp_color_tx_.clear(float4(0.0f));
  }
  if (dummy_rp_value_tx_.ensure_2d_array(gpu::TextureFormat::SFLOAT_32, int2(1), 1, dummy_usage)) {
    dummy_rp_value_tx_.clear(float4(0.0f));
  }
  if (dummy_rp_crypto_tx_.ensure_2d(
          gpu::TextureFormat::SFLOAT_32_32_32_32, int2(1), dummy_usage))
  {
    dummy_rp_crypto_tx_.clear(float4(0.0f));
  }
  if (dummy_accum_color_tx_.ensure_2d_array(
          gpu::TextureFormat::SFLOAT_32_32_32_32,
          int2(1),
          1,
          GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_SHADER_WRITE))
  {
    dummy_accum_color_tx_.clear(float4(0.0f));
  }
  if (dummy_accum_value_tx_.ensure_2d_array(
          gpu::TextureFormat::SFLOAT_32,
          int2(1),
          1,
          GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_SHADER_WRITE))
  {
    dummy_accum_value_tx_.clear(float4(0.0f));
  }
  if (dummy_accum_crypto_tx_.ensure_2d_array(
          gpu::TextureFormat::SFLOAT_32_32_32_32,
          int2(1),
          1,
          GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_SHADER_WRITE))
  {
    dummy_accum_crypto_tx_.clear(float4(0.0f));
  }

  rp_color_ptr_ = dummy_rp_color_tx_;
  rp_value_ptr_ = dummy_rp_value_tx_;
  rp_crypto_ptr_ = dummy_rp_crypto_tx_;
  accum_sample_color_ptr_ = dummy_rp_color_tx_;
  accum_sample_value_ptr_ = dummy_rp_value_tx_;
  accum_sample_crypto_ptr_ = dummy_rp_crypto_tx_;
  accum_sample_depth_ptr_ = nullptr;
  accum_out_color_ptr_ = dummy_accum_color_tx_;
  accum_out_value_ptr_ = dummy_accum_value_tx_;
  accum_out_crypto_ptr_ = dummy_accum_crypto_tx_;
  display_color_ptr_ = dummy_accum_color_tx_;
  display_value_ptr_ = dummy_accum_value_tx_;
  display_crypto_ptr_ = dummy_accum_crypto_tx_;

  ShaderModule &sh = ShaderModule::get();

  display_ps_.init();
  display_ps_.state_set(DRW_STATE_WRITE_COLOR);
  display_ps_.shader_set(sh.pass_display.get());
  display_ps_.bind_texture("accum_color_tx", &display_color_ptr_);
  display_ps_.bind_texture("accum_value_tx", &display_value_ptr_);
  display_ps_.bind_texture("accum_crypto_tx", &display_crypto_ptr_);
  display_ps_.push_constant("display_mode", &unf_display_mode_);
  display_ps_.push_constant("display_layer", &unf_display_layer_);
  display_ps_.push_constant("depth_scale", &unf_depth_scale_);
  display_ps_.draw_procedural(GPU_PRIM_TRIS, 1, 3);
}

float RenderPassModule::register_cryptomatte_id(int crypto_flag_bit, const ID &id) const
{
  uint32_t hash = 0;
  if (session_) {
    if (crypto_flag_bit == GOO_CRYPTO_FLAG_OBJECT && object_layer_) {
      hash = object_layer_->add_ID(id);
    }
    else if (crypto_flag_bit == GOO_CRYPTO_FLAG_ASSET && asset_layer_) {
      hash = asset_layer_->add_ID(id);
    }
    else if (crypto_flag_bit == GOO_CRYPTO_FLAG_MATERIAL && material_layer_) {
      hash = material_layer_->add_ID(id);
    }
  }
  if (hash == 0) {
    const char *name = &id.name[2];
    const int name_len = BLI_strnlen(name, MAX_NAME - 2);
    hash = BKE_cryptomatte_hash(name, name_len);
  }
  return BKE_cryptomatte_hash_to_float(hash);
}

void RenderPassModule::sync_object(const Object *ob, ResourceHandleRange handle)
{
  if (ob == nullptr || (pass_buf_.crypto_flags & (GOO_CRYPTO_FLAG_OBJECT | GOO_CRYPTO_FLAG_ASSET)) == 0) {
    return;
  }

  float2 object_hashes(0.0f, 0.0f);
  if (pass_buf_.crypto_flags & GOO_CRYPTO_FLAG_OBJECT) {
    object_hashes[0] = register_cryptomatte_id(GOO_CRYPTO_FLAG_OBJECT, ob->id);
  }
  if (pass_buf_.crypto_flags & GOO_CRYPTO_FLAG_ASSET) {
    const Object *asset = ob;
    while (asset->parent != nullptr) {
      asset = asset->parent;
    }
    object_hashes[1] = register_cryptomatte_id(GOO_CRYPTO_FLAG_ASSET, asset->id);
  }

  for (ResourceID resource_id : handle.id_range()) {
    crypto_object_buf_.get_or_resize(resource_id.index()) = object_hashes;
  }
}

float RenderPassModule::sync_material(const Material *ma)
{
  if (ma == nullptr || (pass_buf_.crypto_flags & GOO_CRYPTO_FLAG_MATERIAL) == 0) {
    return 0.0f;
  }
  return register_cryptomatte_id(GOO_CRYPTO_FLAG_MATERIAL, ma->id);
}

void RenderPassModule::end_sync()
{
  pass_buf_.push_update();
  crypto_object_buf_.push_update();
}

void RenderPassModule::acquire_buffers(int2 output_extent, int2 render_extent)
{
  output_extent_ = math::max(output_extent, int2(1));
  const int2 safe_render_extent = math::max(render_extent, int2(1));

  has_sample_color_ = false;
  has_sample_value_ = false;
  has_sample_crypto_ = false;
  has_accum_color_ = false;
  has_accum_value_ = false;
  has_accum_crypto_ = false;

  const eGPUTextureUsage sample_usage = GPU_TEXTURE_USAGE_SHADER_READ |
                                       GPU_TEXTURE_USAGE_SHADER_WRITE |
                                       GPU_TEXTURE_USAGE_ATTACHMENT;
  const eGPUTextureUsage accum_usage = GPU_TEXTURE_USAGE_SHADER_READ |
                                      GPU_TEXTURE_USAGE_SHADER_WRITE |
                                      GPU_TEXTURE_USAGE_HOST_READ;

  if (pass_buf_.color_layer_count > 0) {
    sample_rp_color_tx_.acquire_2d_array(safe_render_extent,
                                         pass_buf_.color_layer_count,
                                         gpu::TextureFormat::SFLOAT_16_16_16_16,
                                         sample_usage);
    has_sample_color_ = true;
    rp_color_ptr_ = sample_rp_color_tx_;
    accum_sample_color_ptr_ = sample_rp_color_tx_;

    accum_rp_color_tx_.ensure_2d_array(gpu::TextureFormat::SFLOAT_32_32_32_32,
                                       output_extent_,
                                       pass_buf_.color_layer_count,
                                       accum_usage);
    has_accum_color_ = true;
    accum_out_color_ptr_ = accum_rp_color_tx_;
    display_color_ptr_ = accum_rp_color_tx_;
  }
  else {
    rp_color_ptr_ = dummy_rp_color_tx_;
    accum_sample_color_ptr_ = dummy_rp_color_tx_;
    accum_out_color_ptr_ = dummy_accum_color_tx_;
    display_color_ptr_ = dummy_accum_color_tx_;
  }

  if (pass_buf_.aov_value_len > 0) {
    sample_rp_value_tx_.acquire_2d_array(
        safe_render_extent, pass_buf_.aov_value_len, gpu::TextureFormat::SFLOAT_32, sample_usage);
    has_sample_value_ = true;
    rp_value_ptr_ = sample_rp_value_tx_;
    accum_sample_value_ptr_ = sample_rp_value_tx_;
  }
  else {
    rp_value_ptr_ = dummy_rp_value_tx_;
    accum_sample_value_ptr_ = dummy_rp_value_tx_;
  }

  if (pass_buf_.value_layer_count > 0) {
    accum_rp_value_tx_.ensure_2d_array(
        gpu::TextureFormat::SFLOAT_32, output_extent_, pass_buf_.value_layer_count, accum_usage);
    has_accum_value_ = true;
    accum_out_value_ptr_ = accum_rp_value_tx_;
    display_value_ptr_ = accum_rp_value_tx_;
  }
  else {
    accum_out_value_ptr_ = dummy_accum_value_tx_;
    display_value_ptr_ = dummy_accum_value_tx_;
  }

  if (pass_buf_.crypto_flags != 0 && crypto_layer_count_ > 0) {
    sample_rp_crypto_tx_.acquire_2d(
        safe_render_extent, gpu::TextureFormat::SFLOAT_32_32_32_32, sample_usage);
    has_sample_crypto_ = true;
    rp_crypto_ptr_ = sample_rp_crypto_tx_;
    accum_sample_crypto_ptr_ = sample_rp_crypto_tx_;

    const int pass_len = (std::clamp(pass_buf_.crypto_levels, 2, GOO_CRYPTO_LEVELS_MAX) + 1) / 2;
    const int crypto_slices = std::max(1, crypto_layer_count_ * pass_len);
    accum_crypto_tx_.ensure_2d_array(
        gpu::TextureFormat::SFLOAT_32_32_32_32, output_extent_, crypto_slices, accum_usage);
    has_accum_crypto_ = true;
    accum_out_crypto_ptr_ = accum_crypto_tx_;
    display_crypto_ptr_ = accum_crypto_tx_;
  }
  else {
    rp_crypto_ptr_ = dummy_rp_crypto_tx_;
    accum_sample_crypto_ptr_ = dummy_rp_crypto_tx_;
    accum_out_crypto_ptr_ = dummy_accum_crypto_tx_;
    display_crypto_ptr_ = dummy_accum_crypto_tx_;
  }
}

void RenderPassModule::release_sample_buffers()
{
  rp_color_ptr_ = dummy_rp_color_tx_;
  rp_value_ptr_ = dummy_rp_value_tx_;
  rp_crypto_ptr_ = dummy_rp_crypto_tx_;
  accum_sample_color_ptr_ = dummy_rp_color_tx_;
  accum_sample_value_ptr_ = dummy_rp_value_tx_;
  accum_sample_crypto_ptr_ = dummy_rp_crypto_tx_;

  if (has_sample_color_) {
    sample_rp_color_tx_.release();
    has_sample_color_ = false;
  }
  if (has_sample_value_) {
    sample_rp_value_tx_.release();
    has_sample_value_ = false;
  }
  if (has_sample_crypto_) {
    sample_rp_crypto_tx_.release();
    has_sample_crypto_ = false;
  }
}

void RenderPassModule::clear_sample_buffers()
{
  if (has_sample_color_) {
    sample_rp_color_tx_.clear(float4(0.0f));
  }
  if (has_sample_value_) {
    sample_rp_value_tx_.clear(float4(0.0f));
  }
  if (has_sample_crypto_) {
    sample_rp_crypto_tx_.clear(float4(0.0f));
  }
}

void RenderPassModule::accumulate_sample(Manager &manager,
                                         View &sample_view,
                                         gpu::Texture *sample_depth_tx,
                                         int sample_index,
                                         int total_samples,
                                         int overscan_px)
{
  if (!any_pass_active()) {
    return;
  }

  unf_output_extent_ = output_extent_;
  unf_overscan_offset_ = int2(overscan_px);
  unf_sample_index_ = sample_index;
  unf_sample_weight_ = 1.0f / float(std::max(total_samples, 1));
  accum_sample_depth_ptr_ = sample_depth_tx;

  ShaderModule &sh = ShaderModule::get();
  const int3 groups = int3(math::divide_ceil(output_extent_, int2(8)), 1);

  PassSimple pass = {"Goo.PassAccumRun"};
  pass.state_set(DRW_STATE_NO_DRAW);
  pass.shader_set(sh.pass_accum.get());
  pass.bind_ubo(GOO_PASS_UBO_SLOT, pass_buf_);
  pass.bind_texture("sample_color_tx", &accum_sample_color_ptr_);
  pass.bind_texture("sample_value_tx", &accum_sample_value_ptr_);
  pass.bind_texture("sample_crypto_tx", &accum_sample_crypto_ptr_);
  pass.bind_texture("sample_depth_tx", &accum_sample_depth_ptr_);
  pass.bind_image("accum_color_img", &accum_out_color_ptr_);
  pass.bind_image("accum_value_img", &accum_out_value_ptr_);
  pass.bind_image("accum_crypto_img", &accum_out_crypto_ptr_);
  pass.push_constant("output_extent", &unf_output_extent_);
  pass.push_constant("overscan_offset", &unf_overscan_offset_);
  pass.push_constant("sample_index", &unf_sample_index_);
  pass.push_constant("sample_weight", &unf_sample_weight_);
  pass.barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS | GPU_BARRIER_TEXTURE_FETCH);
  pass.dispatch(groups);
  manager.submit(pass, sample_view);
}

void RenderPassModule::finalize_passes(Manager &manager)
{
  if (!any_pass_active()) {
    return;
  }

  if (has_accum_crypto_ && crypto_layer_count_ > 0) {
    unf_output_extent_ = output_extent_;
    unf_crypto_layer_count_ = crypto_layer_count_;
    unf_crypto_levels_ = pass_buf_.crypto_levels;
    const int3 groups = int3(math::divide_ceil(output_extent_, int2(8)), 1);

    PassSimple sort_pass = {"Goo.CryptoSortRun"};
    sort_pass.state_set(DRW_STATE_NO_DRAW);
    sort_pass.shader_set(ShaderModule::get().cryptomatte_sort.get());
    sort_pass.bind_image("accum_crypto_img", &accum_out_crypto_ptr_);
    sort_pass.push_constant("output_extent", &unf_output_extent_);
    sort_pass.push_constant("crypto_layer_count", &unf_crypto_layer_count_);
    sort_pass.push_constant("crypto_levels", &unf_crypto_levels_);
    sort_pass.barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS);
    sort_pass.dispatch(groups);
    manager.submit(sort_pass);
  }
}

void RenderPassModule::display_viewport_pass(Manager &manager, View &view, Framebuffer &output_fb)
{
  if (!is_display_override()) {
    return;
  }
  display_ps_.barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS | GPU_BARRIER_TEXTURE_FETCH);
  output_fb.bind();
  manager.submit(display_ps_, view);
}

static void copy_layer_readback(gpu::Texture *layer_tx,
                                int src_channels,
                                int dst_channels,
                                const int2 extent,
                                const rcti &rect,
                                RenderPass *rp)
{
  if (layer_tx == nullptr || rp == nullptr || rp->ibuf == nullptr) {
    return;
  }
  float *src = static_cast<float *>(GPU_texture_read(layer_tx, GPU_DATA_FLOAT, 0));
  if (src == nullptr) {
    return;
  }
  float *dst = rp->ibuf->float_data_for_write();
  const int rw = std::min(rp->rectx, BLI_rcti_size_x(&rect));
  const int rh = std::min(rp->recty, BLI_rcti_size_y(&rect));

  if (dst != nullptr && rw > 0 && rh > 0) {
    for (int y = 0; y < rh; y++) {
      const int sy = std::clamp(rect.ymin + y, 0, extent.y - 1);
      for (int x = 0; x < rw; x++) {
        const int sx = std::clamp(rect.xmin + x, 0, extent.x - 1);
        const int src_idx = (sy * extent.x + sx) * src_channels;
        const int dst_idx = (y * rp->rectx + x) * dst_channels;
        for (int c = 0; c < dst_channels; c++) {
          dst[dst_idx + c] = src[src_idx + c];
        }
      }
    }
  }
  MEM_delete(src);
}

void RenderPassModule::read_render_passes(RenderLayer *layer,
                                          const ViewLayer *view_layer,
                                          const char *viewname,
                                          const rcti &rect)
{
  if (layer == nullptr || view_layer == nullptr || !any_pass_active()) {
    return;
  }

  GPU_memory_barrier(GPU_BARRIER_TEXTURE_UPDATE | GPU_BARRIER_SHADER_IMAGE_ACCESS);

  if (has_accum_color_) {
    accum_rp_color_tx_.ensure_layer_views();
    auto read_color3 = [&](const char *pass_name, int layer_idx) {
      if (layer_idx < 0) {
        return;
      }
      RenderPass *rp = RE_pass_find_by_name(layer, pass_name, viewname);
      copy_layer_readback(accum_rp_color_tx_.layer_view(layer_idx), 4, 3, output_extent_, rect, rp);
    };
    read_color3(RE_PASSNAME_NORMAL, pass_buf_.normal_id);
    read_color3(RE_PASSNAME_POSITION, pass_buf_.position_id);
    read_color3(RE_PASSNAME_DIFFUSE_DIRECT, pass_buf_.diff_dir_id);
    read_color3(RE_PASSNAME_DIFFUSE_COLOR, pass_buf_.diff_col_id);
    read_color3(RE_PASSNAME_GLOSSY_DIRECT, pass_buf_.gloss_dir_id);
    read_color3(RE_PASSNAME_GLOSSY_COLOR, pass_buf_.gloss_col_id);
    read_color3(RE_PASSNAME_EMIT, pass_buf_.emit_id);
    read_color3(RE_PASSNAME_ENVIRONMENT, pass_buf_.env_id);
    read_color3(RE_PASSNAME_SHADOW, pass_buf_.shadow_id);
    read_color3(RE_PASSNAME_AO, pass_buf_.ao_id);

    for (int i : active_color_aovs_.index_range()) {
      RenderPass *rp = RE_pass_find_by_name(layer, active_color_aovs_[i]->name, viewname);
      copy_layer_readback(
          accum_rp_color_tx_.layer_view(pass_buf_.aov_color_id + i), 4, 4, output_extent_, rect, rp);
    }
  }

  if (has_accum_value_) {
    accum_rp_value_tx_.ensure_layer_views();
    auto read_val1 = [&](const char *pass_name, int layer_idx) {
      if (layer_idx < 0) {
        return;
      }
      RenderPass *rp = RE_pass_find_by_name(layer, pass_name, viewname);
      copy_layer_readback(accum_rp_value_tx_.layer_view(layer_idx), 1, 1, output_extent_, rect, rp);
    };
    read_val1(RE_PASSNAME_DEPTH, pass_buf_.depth_id);
    read_val1(RE_PASSNAME_MIST, pass_buf_.mist_id);

    for (int i : active_value_aovs_.index_range()) {
      RenderPass *rp = RE_pass_find_by_name(layer, active_value_aovs_[i]->name, viewname);
      copy_layer_readback(
          accum_rp_value_tx_.layer_view(pass_buf_.aov_value_id + i), 1, 1, output_extent_, rect, rp);
    }
  }

  if (has_accum_crypto_ && pass_buf_.crypto_flags != 0) {
    accum_crypto_tx_.ensure_layer_views();
    const int pass_len = (std::clamp(pass_buf_.crypto_levels, 2, GOO_CRYPTO_LEVELS_MAX) + 1) / 2;
    int active_layer = 0;

    auto read_crypto_layer = [&](int bit_flag, const char *base_name) {
      if ((pass_buf_.crypto_flags & bit_flag) == 0) {
        return;
      }
      const int base_slice = active_layer * pass_len;
      active_layer++;
      Vector<std::string> names = cryptomatte_pass_names(base_name, pass_buf_.crypto_levels);
      for (int p = 0; p < pass_len && p < names.size(); p++) {
        RenderPass *rp = RE_pass_find_by_name(layer, names[p].c_str(), viewname);
        copy_layer_readback(
            accum_crypto_tx_.layer_view(base_slice + p), 4, 4, output_extent_, rect, rp);
      }
    };

    read_crypto_layer(GOO_CRYPTO_FLAG_OBJECT, RE_PASSNAME_CRYPTOMATTE_OBJECT);
    read_crypto_layer(GOO_CRYPTO_FLAG_ASSET, RE_PASSNAME_CRYPTOMATTE_ASSET);
    read_crypto_layer(GOO_CRYPTO_FLAG_MATERIAL, RE_PASSNAME_CRYPTOMATTE_MATERIAL);
  }
}

void RenderPassModule::store_metadata(RenderResult *rr)
{
  if (session_ && rr != nullptr) {
    BKE_cryptomatte_store_metadata(session_.get(), rr);
  }
}

}  // namespace blender::goo
