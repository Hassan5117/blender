/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include <algorithm>
#include <cmath>

#include "BKE_light.h"

#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"

#include "DNA_light_types.h"
#include "DNA_material_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "GPU_framebuffer.hh"
#include "GPU_texture.hh"

#include "draw_cache.hh"
#include "draw_common.hh"

#include "../eevee/eevee_defines.hh"

#include "goo_defines.hh"
#include "goo_light.hh"
#include "goo_shader.hh"

namespace blender::goo {

static float4x4 make_view_matrix(const float3 &axis_x,
                                 const float3 &axis_y,
                                 const float3 &axis_z,
                                 const float3 &eye)
{
  float4x4 v = float4x4::identity();
  v[0][0] = axis_x.x;
  v[1][0] = axis_x.y;
  v[2][0] = axis_x.z;
  v[3][0] = -math::dot(axis_x, eye);

  v[0][1] = axis_y.x;
  v[1][1] = axis_y.y;
  v[2][1] = axis_y.z;
  v[3][1] = -math::dot(axis_y, eye);

  v[0][2] = axis_z.x;
  v[1][2] = axis_z.y;
  v[2][2] = axis_z.z;
  v[3][2] = -math::dot(axis_z, eye);
  return v;
}

void LightModule::begin_sync(const Scene *scene, float4 world_ambient, float ao_pass_distance)
{
  light_buf_.light_count = 0;
  light_buf_.shadow_slice_count = 0;
  light_buf_.world_ambient = world_ambient;
  int gtao_flags = 0;
  if (scene != nullptr) {
    if ((scene->eevee.flag & SCE_EEVEE_GTAO_ENABLED) != 0) {
      gtao_flags |= GOO_GTAO_FLAG_ENABLED;
    }
    if ((scene->eevee.flag & SCE_EEVEE_GTAO_BENT_NORMALS) != 0) {
      gtao_flags |= GOO_GTAO_FLAG_BENT_NORMALS;
    }
    if ((scene->eevee.flag & SCE_EEVEE_GTAO_BOUNCE) != 0) {
      gtao_flags |= GOO_GTAO_FLAG_BOUNCE;
    }
  }
  float gtao_dist = ((gtao_flags & GOO_GTAO_FLAG_ENABLED) != 0 && scene != nullptr) ?
                        std::max(scene->eevee.gtao_distance, 0.0f) :
                        0.0f;
  if (gtao_dist <= 0.0f && ao_pass_distance > 0.0f) {
    gtao_dist = ao_pass_distance;
  }
  light_buf_.gtao_params = float4(
      gtao_dist,
      scene ? std::max(scene->eevee.gtao_factor, 0.0f) : 1.0f,
      scene ? std::clamp(scene->eevee.goo_gtao_quality, 0.0f, 1.0f) : 0.25f,
      float(gtao_flags));
  light_buf_.sample_params = float4(0.0f, 0.0f, 0.0f, 1.0f);
  render_slices_.clear();
  shadow_mat_subs_.clear();

  use_shadows_ = (scene == nullptr) || ((scene->eevee.flag & SCE_EEVEE_SHADOW_ENABLED) != 0);
  const float scale = scene ? scene->eevee.shadow_resolution_scale : 1.0f;
  shadow_res_ = std::clamp(int(std::lround(float(GOO_SHADOW_RES) * scale)), 256, 4096);

  const int cur_layers = shadow_tx_.gpu_texture() ? std::max(1, GPU_texture_depth(shadow_tx_)) : 1;
  if (shadow_tx_.ensure_2d_array(gpu::TextureFormat::SFLOAT_32_DEPTH,
                                 int2(shadow_res_),
                                 cur_layers,
                                 GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT))
  {
    shadow_tx_.clear(float4(1.0f));
  }
  const int cur_id_layers = shadow_id_tx_.gpu_texture() ?
                                std::max(1, GPU_texture_depth(shadow_id_tx_)) :
                                1;
  if (shadow_id_tx_.ensure_2d_array(gpu::TextureFormat::UINT_32,
                                    int2(shadow_res_),
                                    cur_id_layers,
                                    GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT))
  {
    shadow_id_tx_.clear(uint4(0u));
  }

  /* 1x1 dummy textures bound on material-evaluated shadow shaders to avoid Metal read/write hazards. */
  if (dummy_shadow_depth_tx_.ensure_2d_array(
          gpu::TextureFormat::SFLOAT_32_DEPTH,
          int2(1),
          1,
          GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT))
  {
    dummy_shadow_depth_tx_.clear(float4(1.0f));
  }
  if (dummy_shadow_id_tx_.ensure_2d_array(
          gpu::TextureFormat::UINT_32,
          int2(1),
          1,
          GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT))
  {
    dummy_shadow_id_tx_.clear(uint4(0u));
  }
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

  shadow_ps_.init();
  shadow_ps_.state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL);

  shadow_cull_sub_ = &shadow_ps_.sub("ShadowCull");
  shadow_cull_sub_->state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH |
                              DRW_STATE_DEPTH_LESS_EQUAL | DRW_STATE_CULL_BACK);
  shadow_cull_sub_->shader_set(ShaderModule::get().shadow_id_mesh.get());

  shadow_nocull_sub_ = &shadow_ps_.sub("ShadowNoCull");
  shadow_nocull_sub_->state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH |
                                DRW_STATE_DEPTH_LESS_EQUAL);
  shadow_nocull_sub_->shader_set(ShaderModule::get().shadow_id_mesh.get());

  shadow_curves_sub_ = &shadow_ps_.sub("ShadowCurves");
  shadow_curves_sub_->state_set(DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH |
                                DRW_STATE_DEPTH_LESS_EQUAL);
  shadow_curves_sub_->shader_set(ShaderModule::get().shadow_id_curves.get());
}

PassMain::Sub &LightModule::shadow_mat_sub_get(Manager &manager,
                                               const Material *ma,
                                               GPUMaterial *shadow_gpumat,
                                               bool deferred,
                                               bool is_curves)
{
  return *shadow_mat_subs_.lookup_or_add_cb(shadow_gpumat, [&]() {
    PassMain::Sub &sub = shadow_ps_.sub(ma ? ma->id.name + 2 : "ShadowMat");
    const bool cull_back = !is_curves && ma && (ma->blend_flag & MA_BL_CULL_BACKFACE_SHADOW) != 0;
    DRWState state = DRW_STATE_WRITE_COLOR | DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL;
    if (cull_back) {
      state |= DRW_STATE_CULL_BACK;
    }
    sub.state_set(state);
    sub.material_set(manager, shadow_gpumat, deferred);
    sub.bind_texture(RBUFS_UTILITY_TEX_SLOT, ShaderModule::get().utility_tx);
    sub.bind_ubo(GOO_LIGHT_UBO_SLOT, light_buf_);
    sub.bind_ubo(GOO_SHADOW_UBO_SLOT, shadow_buf_);
    sub.bind_texture(
        GOO_SHADOW_TEX_SLOT, dummy_shadow_depth_tx_, GPUSamplerState::default_sampler());
    sub.bind_texture(
        GOO_SHADOW_ID_TEX_SLOT, dummy_shadow_id_tx_, GPUSamplerState::default_sampler());
    sub.bind_texture(GOO_DEPTH_TEX_SLOT, dummy_depth_tx_, GPUSamplerState::default_sampler());
    sub.bind_texture(GOO_SCENE_COLOR_TEX_SLOT, dummy_color_tx_, GPUSamplerState::default_sampler());
    sub.bind_texture(GOO_HORIZON_TEX_SLOT, dummy_horizon_tx_, GPUSamplerState::default_sampler());

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
    const int mat_flags = (ma && ma->blend_shadow == MA_BS_CLIP) ? GOO_MAT_FLAG_ALPHA_CLIP :
                                                                   GOO_MAT_FLAG_ALPHA_HASHED;
    sub.push_constant("goo_light_groups", light_groups);
    sub.push_constant("goo_light_group_shadows", shadow_groups);
    sub.push_constant("goo_alpha_threshold", ma ? ma->alpha_threshold : 0.5f);
    sub.push_constant("goo_material_flags", mat_flags);
    sub.push_constant("goo_refraction_depth", 0.0f);
    return &sub;
  });
}

void LightModule::caster_slot_sync(Manager &manager,
                                   gpu::Batch *geom,
                                   const Material *ma,
                                   GPUMaterial *shadow_gpumat,
                                   ResourceHandleRange handle,
                                   bool deferred)
{
  if (!use_shadows_ || geom == nullptr) {
    return;
  }
  const int shadow_mode = ma ? int(ma->blend_shadow) : int(MA_BS_SOLID);
  if (shadow_mode == MA_BS_NONE) {
    return;
  }
  if (ELEM(shadow_mode, MA_BS_CLIP, MA_BS_HASHED) && shadow_gpumat != nullptr) {
    shadow_mat_sub_get(manager, ma, shadow_gpumat, deferred).draw(geom, handle);
    return;
  }
  const bool cull_back = ma && (ma->blend_flag & MA_BL_CULL_BACKFACE_SHADOW) != 0;
  PassMain::Sub *sub = cull_back ? shadow_cull_sub_ : shadow_nocull_sub_;
  sub->draw(geom, handle);
}

void LightModule::caster_curves_sync(Manager &manager,
                                     const Scene *scene,
                                     const ObjectRef &ob_ref,
                                     ParticleSystem *psys,
                                     ModifierData *md,
                                     const Material *ma,
                                     GPUMaterial *shadow_gpumat,
                                     ResourceHandleRange handle,
                                     bool deferred)
{
  if (!use_shadows_) {
    return;
  }
  const int shadow_mode = ma ? int(ma->blend_shadow) : int(MA_BS_SOLID);
  if (shadow_mode == MA_BS_NONE) {
    return;
  }
  const bool use_mat_shadow = ELEM(shadow_mode, MA_BS_CLIP, MA_BS_HASHED) &&
                              shadow_gpumat != nullptr;
  PassMain::Sub &parent_sub = use_mat_shadow ?
                                  shadow_mat_sub_get(manager, ma, shadow_gpumat, deferred, true) :
                                  *shadow_curves_sub_;
  PassMain::Sub &call_sub = parent_sub.sub("ShadowCurves");
  GPUMaterial *setup_mat = use_mat_shadow ? shadow_gpumat : nullptr;
  gpu::Batch *geom = nullptr;
  if (psys != nullptr) {
    geom = hair_sub_pass_setup(call_sub, scene, ob_ref, psys, md, setup_mat);
  }
  else {
    const char *unused_err = nullptr;
    geom = curves_sub_pass_setup(call_sub, scene, ob_ref.object, unused_err, setup_mat);
  }
  if (geom != nullptr) {
    call_sub.draw(geom, handle);
  }
}

void LightModule::light_sync(Object *ob, const View &camera_view)
{
  if (light_buf_.light_count >= GOO_MAX_LIGHTS) {
    return;
  }

  const Light &la = DRW_object_get_data_for_drawing<const Light>(*ob);
  const float3 color = BKE_light_color(la);
  const float power = BKE_light_power(la);
  if (power <= 0.0f) {
    return;
  }

  float4x4 obmat = ob->object_to_world();
  float3 scale;
  obmat.view<3, 3>() = math::normalize_and_get_size(obmat.view<3, 3>(), scale);
  if (math::dot(math::cross(float3(obmat.x_axis()), float3(obmat.y_axis())),
                float3(obmat.z_axis())) < 0.0f)
  {
    obmat.y_axis() = -obmat.y_axis();
  }

  GooLightData &dst = light_buf_.lights[light_buf_.light_count++];
  dst = {};
  dst.spot_shadow.y = -1.0f;
  dst.spot_shadow.z = 0.0f;
  dst.spot_shadow.w = la.diff_fac;
  dst.extra.x = la.spec_fac;
  dst.light_group_bits = int4(la.light_group_bits[0],
                              la.light_group_bits[1],
                              la.light_group_bits[2],
                              la.light_group_bits[3]);

  const float3 pos = obmat.location();
  const float3 fwd = -math::normalize(float3(obmat.z_axis()));
  const bool cast_shadow = use_shadows_ && ((la.mode & LA_SHADOW) != 0);
  if (cast_shadow && (la.mode & LA_SHAD_CONTACT) != 0 && la.contact_dist > 0.0f) {
    dst.contact_shadow = float4(la.contact_dist,
                                std::max(la.contact_bias, 1e-4f),
                                std::max(la.contact_thickness, 1e-4f),
                                0.0f);
  }

  switch (la.type) {
    case LA_SUN: {
      dst.position_type = float4(-fwd, float(GOO_LIGHT_SUN));
      dst.color_energy = float4(color * power, la.radius);
      dst.direction_spot_cos = float4(fwd, -1.0f);

      if (cast_shadow) {
        const float cascade_radii[3] = {8.0f, 24.0f, 80.0f};
        const int max_cascades = std::min(3, GOO_MAX_SHADOW_SLICES - light_buf_.shadow_slice_count);
        if (max_cascades > 0) {
          const int slice_start = light_buf_.shadow_slice_count;
          dst.spot_shadow.y = float(slice_start);
          dst.spot_shadow.z = float(max_cascades);

          const float3 axis_z = -fwd;
          const float3 up_hint = (fabsf(axis_z.z) < 0.99f) ? float3(0.0f, 0.0f, 1.0f) :
                                                             float3(0.0f, 1.0f, 0.0f);
          const float3 axis_x = math::normalize(math::cross(up_hint, axis_z));
          const float3 axis_y = math::cross(axis_z, axis_x);

          const float3 cam_pos = camera_view.location();
          const float3 cam_look = -math::normalize(camera_view.forward());

          for (int c = 0; c < max_cascades; c++) {
            const float r = cascade_radii[c];
            const float focus_dist = camera_view.is_persp() ?
                                         std::min(r * 0.7f, math::length(cam_pos) + 4.0f) :
                                         0.0f;
            float3 focus = cam_pos + cam_look * focus_dist;
            /* Snap focus to shadow texels to prevent edge swimming. */
            const float texel_world = (2.0f * r) / float(shadow_res_);
            float fx = std::floor(math::dot(focus, axis_x) / texel_world) * texel_world;
            float fy = std::floor(math::dot(focus, axis_y) / texel_world) * texel_world;
            float fz = math::dot(focus, axis_z);
            focus = axis_x * fx + axis_y * fy + axis_z * fz;

            const float depth_half = std::max(60.0f, r * 2.5f);
            const float near = 0.1f;
            const float far = 2.0f * depth_half;
            const float3 eye = focus + axis_z * depth_half;

            const float4x4 viewmat = make_view_matrix(axis_x, axis_y, axis_z, eye);
            const float4x4 winmat = math::projection::orthographic(-r, r, -r, r, near, far);

            const int layer = light_buf_.shadow_slice_count++;
            GooShadowSlice &slice = shadow_buf_.slices[layer];
            slice.vp = winmat * viewmat;
            slice.params = float4(
                texel_world * 1.5f, 0.0005f, far - near, 1.0f / float(shadow_res_));
            render_slices_.append({layer, viewmat, winmat});
          }
        }
      }
      break;
    }

    case LA_SPOT: {
      const float cos_outer = std::cos(la.spotsize * 0.5f);
      const float cos_inner = std::cos(la.spotsize * 0.5f * (1.0f - la.spotblend));
      dst.position_type = float4(pos, float(GOO_LIGHT_SPOT));
      dst.color_energy = float4(color * power / (4.0f * float(M_PI)), la.radius);
      dst.direction_spot_cos = float4(fwd, cos_outer);
      dst.spot_shadow.x = 1.0f / std::max(1e-4f, cos_inner - cos_outer);

      if (cast_shadow && (GOO_MAX_SHADOW_SLICES - light_buf_.shadow_slice_count) >= 1) {
        const float far = std::max(la.att_dist, 40.0f);
        const float near = std::clamp(la.clipsta, 0.05f, far * 0.25f);
        const float half_tan = std::tan(std::clamp(la.spotsize * 0.5f, 0.02f, float(M_PI) * 0.49f));
        const float extent = near * half_tan;
        const float4x4 viewmat = math::invert(obmat);
        const float4x4 winmat = math::projection::perspective(
            -extent, extent, -extent, extent, near, far);

        const int layer = light_buf_.shadow_slice_count++;
        dst.spot_shadow.y = float(layer);
        dst.spot_shadow.z = 1.0f;
        dst.extra.y = far;
        dst.extra.z = near;

        GooShadowSlice &slice = shadow_buf_.slices[layer];
        slice.vp = winmat * viewmat;
        slice.params = float4(0.02f, 0.0005f, 0.0f, 1.0f / float(shadow_res_));
        render_slices_.append({layer, viewmat, winmat});
      }
      break;
    }

    case LA_AREA: {
      const float area_radius = std::max(la.area_size * scale.x, la.area_sizey * scale.y) * 0.5f;
      dst.position_type = float4(pos, float(GOO_LIGHT_AREA));
      dst.color_energy = float4(color * power / float(M_PI), std::max(0.05f, area_radius));
      dst.direction_spot_cos = float4(fwd, 0.0f);
      break;
    }

    case LA_LOCAL:
    default: {
      dst.position_type = float4(pos, float(GOO_LIGHT_POINT));
      dst.color_energy = float4(color * power / (4.0f * float(M_PI)), la.radius);
      dst.direction_spot_cos = float4(fwd, -1.0f);

      if (cast_shadow && (GOO_MAX_SHADOW_SLICES - light_buf_.shadow_slice_count) >= 6) {
        const float far = std::max(la.att_dist, 40.0f);
        const float near = std::clamp(la.clipsta, 0.05f, far * 0.25f);
        const float4x4 winmat = math::projection::perspective(-near, near, -near, near, near, far);

        /* 6 right-handed cube-map faces (+X, -X, +Y, -Y, +Z, -Z), matching GLSL face lookup. */
        static constexpr float3 face_axes[6][3] = {
            {float3(0, 0, 1), float3(0, 1, 0), float3(-1, 0, 0)},  /* +X */
            {float3(0, 0, -1), float3(0, 1, 0), float3(1, 0, 0)},  /* -X */
            {float3(-1, 0, 0), float3(0, 0, -1), float3(0, -1, 0)},/* +Y */
            {float3(-1, 0, 0), float3(0, 0, 1), float3(0, 1, 0)},  /* -Y */
            {float3(-1, 0, 0), float3(0, 1, 0), float3(0, 0, -1)}, /* +Z */
            {float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1)},   /* -Z */
        };

        const int slice_start = light_buf_.shadow_slice_count;
        dst.spot_shadow.y = float(slice_start);
        dst.spot_shadow.z = 6.0f;
        dst.extra.y = far;
        dst.extra.z = near;

        for (int face = 0; face < 6; face++) {
          const float4x4 viewmat = make_view_matrix(
              face_axes[face][0], face_axes[face][1], face_axes[face][2], pos);
          const int layer = light_buf_.shadow_slice_count++;
          GooShadowSlice &slice = shadow_buf_.slices[layer];
          slice.vp = winmat * viewmat;
          slice.params = float4(0.025f, 0.001f, 0.0f, 1.0f / float(shadow_res_));
          render_slices_.append({layer, viewmat, winmat});
        }
      }
      break;
    }
  }
}

void LightModule::end_sync()
{
  light_buf_.push_update();
  shadow_buf_.push_update();
}

void LightModule::set_sample_params(int sample_index, float2 sample_rand, bool film_transparent)
{
  light_buf_.sample_params = float4(
      float(sample_index), sample_rand.x, sample_rand.y, film_transparent ? 0.0f : 1.0f);
  light_buf_.push_update();
}

void LightModule::render_shadows(Manager &manager)
{
  if (render_slices_.is_empty()) {
    return;
  }

  const int needed_layers = std::max(1, light_buf_.shadow_slice_count);
  shadow_tx_.ensure_2d_array(gpu::TextureFormat::SFLOAT_32_DEPTH,
                             int2(shadow_res_),
                             needed_layers,
                             GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);
  shadow_id_tx_.ensure_2d_array(gpu::TextureFormat::UINT_32,
                                int2(shadow_res_),
                                needed_layers,
                                GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);

  shadow_view_.visibility_test(false);
  for (const ShadowRenderSlice &slice : render_slices_) {
    shadow_fb_.ensure(GPU_ATTACHMENT_TEXTURE_LAYER(shadow_tx_, slice.layer),
                      GPU_ATTACHMENT_TEXTURE_LAYER(shadow_id_tx_, slice.layer));
    shadow_fb_.bind();
    GPU_framebuffer_clear_color_depth(shadow_fb_, double4(0.0), 1.0f);
    shadow_view_.sync(slice.viewmat, slice.winmat);
    manager.submit(shadow_ps_, shadow_view_);
  }
}

}  // namespace blender::goo
