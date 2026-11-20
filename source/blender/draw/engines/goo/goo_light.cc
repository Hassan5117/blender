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
#include "DNA_object_types.h"

#include "GPU_framebuffer.hh"
#include "GPU_texture.hh"

#include "draw_cache.hh"

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

void LightModule::begin_sync(float4 world_ambient)
{
  light_buf_.light_count = 0;
  light_buf_.shadow_slice_count = 0;
  light_buf_.world_ambient = world_ambient;
  render_slices_.clear();

  /* Ensure at least 1 layer exists before passes bind `&shadow_tx_`. */
  if (shadow_tx_.gpu_texture() == nullptr) {
    shadow_tx_.ensure_2d_array(gpu::TextureFormat::SFLOAT_32_DEPTH,
                               int2(GOO_SHADOW_RES),
                               1,
                               GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);
  }

  shadow_ps_.init();
  /* No backface culling so single-sided planes / open meshes still cast shadows. */
  shadow_ps_.state_set(DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS_EQUAL);
  shadow_ps_.shader_set(ShaderModule::get().shadow_mesh.get());
}

void LightModule::caster_sync(Object *ob, ResourceHandleRange handle)
{
  if ((ob->visibility_flag & OB_HIDE_SHADOW) != 0) {
    return;
  }
  gpu::Batch *geom = DRW_cache_mesh_surface_get(ob);
  if (geom == nullptr) {
    return;
  }
  shadow_ps_.draw(geom, handle);
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
  const bool cast_shadow = (la.mode & LA_SHADOW) != 0;

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
            const float texel_world = (2.0f * r) / float(GOO_SHADOW_RES);
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
            slice.params = float4(texel_world * 1.5f, 0.0005f, 0.0f, 1.0f / float(GOO_SHADOW_RES));
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
        slice.params = float4(0.02f, 0.0005f, 0.0f, 1.0f / float(GOO_SHADOW_RES));
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
          slice.params = float4(0.025f, 0.001f, 0.0f, 1.0f / float(GOO_SHADOW_RES));
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

void LightModule::render_shadows(Manager &manager)
{
  if (render_slices_.is_empty()) {
    return;
  }

  const int needed_layers = std::max(1, light_buf_.shadow_slice_count);
  shadow_tx_.ensure_2d_array(gpu::TextureFormat::SFLOAT_32_DEPTH,
                             int2(GOO_SHADOW_RES),
                             needed_layers,
                             GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT);

  shadow_view_.visibility_test(false);
  for (const ShadowRenderSlice &slice : render_slices_) {
    shadow_fb_.ensure(GPU_ATTACHMENT_TEXTURE_LAYER(shadow_tx_, slice.layer));
    shadow_fb_.bind();
    shadow_fb_.clear_depth(1.0f);
    shadow_view_.sync(slice.viewmat, slice.winmat);
    manager.submit(shadow_ps_, shadow_view_);
  }
}

}  // namespace blender::goo
