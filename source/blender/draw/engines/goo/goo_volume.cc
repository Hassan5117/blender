/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include <algorithm>
#include <limits>

#include "BKE_object.hh"

#include "BLI_bounds.hh"
#include "BLI_math_base.hh"
#include "BLI_math_matrix.hh"

#include "DNA_object_types.h"
#include "DNA_scene_types.h"
#include "DNA_world_types.h"

#include "GPU_framebuffer.hh"
#include "GPU_material.hh"
#include "GPU_state.hh"
#include "GPU_texture.hh"

#include "../eevee/eevee_defines.hh"

#include "goo_light.hh"
#include "goo_material.hh"
#include "goo_probe.hh"
#include "goo_shader.hh"
#include "goo_volume.hh"

namespace blender::goo {

void VolumeModule::ensure_render_targets(int2 render_extent)
{
  const int2 ext = math::max(render_extent, int2(1));
  froxel_size_ = int3(math::max(math::divide_ceil(ext, int2(tile_size_)), int2(1)),
                      volume_samples_);
  froxel_dispatch_ = math::divide_ceil(froxel_size_, int3(4));
  integrate_dispatch_ = int3(math::divide_ceil(froxel_size_.xy(), int2(8)), 1);
  resolve_dispatch_ = int3(math::divide_ceil(ext, int2(8)), 1);

  const eGPUTextureUsage rw_usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_SHADER_WRITE;
  const eGPUTextureUsage depth_usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT;

  prop_scatter_tx_.ensure_3d(gpu::TextureFormat::SFLOAT_16_16_16_16, froxel_size_, rw_usage);
  prop_extinct_tx_.ensure_3d(gpu::TextureFormat::SFLOAT_16_16_16_16, froxel_size_, rw_usage);
  prop_emissive_tx_.ensure_3d(gpu::TextureFormat::SFLOAT_16_16_16_16, froxel_size_, rw_usage);
  scatter_lit_tx_.ensure_3d(gpu::TextureFormat::SFLOAT_16_16_16_16, froxel_size_, rw_usage);
  integrated_scatter_tx_.ensure_3d(
      gpu::TextureFormat::SFLOAT_16_16_16_16, froxel_size_, rw_usage);
  integrated_transmit_tx_.ensure_3d(
      gpu::TextureFormat::SFLOAT_16_16_16_16, froxel_size_, rw_usage);

  vol_obj_near_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_32_DEPTH, froxel_size_.xy(), depth_usage);
  vol_obj_far_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_32_DEPTH, froxel_size_.xy(), depth_usage);
  vol_dummy_fb_tx_.ensure_2d(
      gpu::TextureFormat::SFLOAT_16_16_16_16, froxel_size_.xy(), GPU_TEXTURE_USAGE_ATTACHMENT);

  prop_scatter_ptr_ = prop_scatter_tx_;
  prop_extinct_ptr_ = prop_extinct_tx_;
  prop_emissive_ptr_ = prop_emissive_tx_;
  scatter_lit_ptr_ = scatter_lit_tx_;
  integrated_scatter_ptr_ = integrated_scatter_tx_;
  integrated_transmit_ptr_ = integrated_transmit_tx_;
  near_depth_ptr_ = vol_obj_near_tx_;
  far_depth_ptr_ = vol_obj_far_tx_;
}

void VolumeModule::free_render_targets()
{
  prop_scatter_tx_.free();
  prop_extinct_tx_.free();
  prop_emissive_tx_.free();
  scatter_lit_tx_.free();
  integrated_scatter_tx_.free();
  integrated_transmit_tx_.free();
  vol_obj_near_tx_.free();
  vol_obj_far_tx_.free();
  vol_dummy_fb_tx_.free();

  prop_scatter_ptr_ = dummy_3d_tx_;
  prop_extinct_ptr_ = dummy_3d_tx_;
  prop_emissive_ptr_ = dummy_3d_tx_;
  scatter_lit_ptr_ = dummy_3d_tx_;
  integrated_scatter_ptr_ = dummy_3d_tx_;
  integrated_transmit_ptr_ = dummy_3d_tx_;
  near_depth_ptr_ = dummy_depth_tx_;
  far_depth_ptr_ = dummy_depth_tx_;
}

void VolumeModule::begin_sync(const Scene *scene,
                              World *world,
                              MaterialModule &materials,
                              ProbeModule &probes,
                              LightModule &lights,
                              int2 render_extent,
                              bool deferred_compile)
{
  objects_.clear();
  has_world_volume_ = false;

  const eGPUTextureUsage dummy_3d_usage = GPU_TEXTURE_USAGE_SHADER_READ |
                                         GPU_TEXTURE_USAGE_SHADER_WRITE;
  if (dummy_3d_tx_.ensure_3d(gpu::TextureFormat::SFLOAT_16_16_16_16, int3(1), dummy_3d_usage)) {
    dummy_3d_tx_.clear(float4(0.0f));
  }
  if (dummy_depth_tx_.ensure_2d(gpu::TextureFormat::SFLOAT_32_DEPTH,
                                int2(1),
                                GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_ATTACHMENT))
  {
    dummy_depth_tx_.clear(float4(1.0f));
  }

  prop_scatter_ptr_ = dummy_3d_tx_;
  prop_extinct_ptr_ = dummy_3d_tx_;
  prop_emissive_ptr_ = dummy_3d_tx_;
  scatter_lit_ptr_ = dummy_3d_tx_;
  integrated_scatter_ptr_ = dummy_3d_tx_;
  integrated_transmit_ptr_ = dummy_3d_tx_;
  near_depth_ptr_ = dummy_depth_tx_;
  far_depth_ptr_ = dummy_depth_tx_;
  resolve_depth_ptr_ = dummy_depth_tx_;

  tile_size_ = std::clamp(scene ? scene->eevee.volumetric_tile_size : 8, 2, 16);
  volume_samples_ = std::clamp(scene ? scene->eevee.volumetric_samples : 64, 8, 256);
  shadow_steps_ = std::clamp(scene ? scene->eevee.volumetric_shadow_samples : 16, 1, 32);
  depth_distrib_ = scene ? std::clamp(scene->eevee.volumetric_sample_distribution, 0.0f, 1.0f) :
                           0.8f;
  vol_start_ = scene ? scene->eevee.volumetric_start : 0.1f;
  vol_end_ = scene ? scene->eevee.volumetric_end : 100.0f;
  clamp_params_ = scene ? float3(scene->eevee.volumetric_light_clamp,
                                 scene->eevee.clamp_volume_direct,
                                 scene->eevee.clamp_volume_indirect) :
                          float3(0.0f);

  volume_flags_ = 0;
  if (scene == nullptr || (scene->eevee.flag & SCE_EEVEE_VOLUMETRIC_LIGHTS) != 0) {
    volume_flags_ |= GOO_VOL_FLAG_LIGHTS;
  }
  if (scene != nullptr && (scene->eevee.flag & SCE_EEVEE_VOLUMETRIC_SHADOWS) != 0) {
    volume_flags_ |= GOO_VOL_FLAG_SHADOWS;
  }
  if (scene != nullptr && (scene->eevee.flag & SCE_EEVEE_VOLUME_CUSTOM_RANGE) != 0) {
    volume_flags_ |= GOO_VOL_FLAG_CUSTOM_RANGE;
  }

  const int2 ext = math::max(render_extent, int2(1));
  froxel_size_ = int3(math::max(math::divide_ceil(ext, int2(tile_size_)), int2(1)),
                      volume_samples_);

  ShaderModule &sh = ShaderModule::get();
  const GPUSamplerState nearest{GPU_SAMPLER_FILTERING_DEFAULT};
  const GPUSamplerState lin{GPU_SAMPLER_FILTERING_LINEAR};

  clear_ps_.init();
  clear_ps_.state_set(DRW_STATE_NO_DRAW);
  clear_ps_.shader_set(sh.volume_clear.get());
  clear_ps_.bind_ubo(GOO_VOL_UBO_SLOT, vol_buf_);
  clear_ps_.bind_image("vol_scatter_img", &prop_scatter_ptr_);
  clear_ps_.bind_image("vol_extinct_img", &prop_extinct_ptr_);
  clear_ps_.bind_image("vol_emissive_img", &prop_emissive_ptr_);
  clear_ps_.barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS | GPU_BARRIER_TEXTURE_FETCH);
  clear_ps_.dispatch(&froxel_dispatch_);

  if (world != nullptr && world->nodetree != nullptr) {
    GPUMaterial *world_vol_mat = materials.world_volume_get(world, deferred_compile);
    if (world_vol_mat != nullptr) {
      has_world_volume_ = true;
      world_ps_.init();
      world_ps_.state_set(DRW_STATE_WRITE_COLOR);
      world_ps_.material_set(*DRW_manager_get(), world_vol_mat, deferred_compile);
      world_ps_.bind_ubo(GOO_VOL_UBO_SLOT, vol_buf_);
      world_ps_.bind_texture(RBUFS_UTILITY_TEX_SLOT, sh.utility_tx);
      world_ps_.bind_texture("vol_obj_near_tx", &near_depth_ptr_, nearest);
      world_ps_.bind_texture("vol_obj_far_tx", &far_depth_ptr_, nearest);
      world_ps_.bind_image("vol_scatter_img", &prop_scatter_ptr_);
      world_ps_.bind_image("vol_extinct_img", &prop_extinct_ptr_);
      world_ps_.bind_image("vol_emissive_img", &prop_emissive_ptr_);
      world_ps_.push_constant("sample_index", &unf_sample_index_);
      world_ps_.draw_procedural(GPU_PRIM_TRIS, 1, 3);
    }
  }

  scatter_ps_.init();
  scatter_ps_.state_set(DRW_STATE_NO_DRAW);
  scatter_ps_.shader_set(sh.volume_scatter.get());
  scatter_ps_.bind_ubo(GOO_VOL_UBO_SLOT, vol_buf_);
  scatter_ps_.bind_ubo(GOO_LIGHT_UBO_SLOT, lights.light_buf());
  scatter_ps_.bind_ubo(GOO_SHADOW_UBO_SLOT, lights.shadow_buf());
  probes.bind_resources(scatter_ps_);
  scatter_ps_.bind_texture(RBUFS_UTILITY_TEX_SLOT, sh.utility_tx);
  scatter_ps_.bind_texture("prop_scatter_tx", &prop_scatter_ptr_, nearest);
  scatter_ps_.bind_texture("prop_extinct_tx", &prop_extinct_ptr_, lin);
  scatter_ps_.bind_texture("prop_emissive_tx", &prop_emissive_ptr_, nearest);
  scatter_ps_.bind_texture(GOO_SHADOW_TEX_SLOT, lights.shadow_tx_ptr(), nearest);
  scatter_ps_.bind_texture(GOO_SHADOW_ID_TEX_SLOT, lights.shadow_id_tx_ptr(), nearest);
  scatter_ps_.bind_image("out_scatter_lit_img", &scatter_lit_ptr_);
  scatter_ps_.push_constant("sample_index", &unf_sample_index_);
  scatter_ps_.barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS | GPU_BARRIER_TEXTURE_FETCH);
  scatter_ps_.dispatch(&froxel_dispatch_);

  integrate_ps_.init();
  integrate_ps_.state_set(DRW_STATE_NO_DRAW);
  integrate_ps_.shader_set(sh.volume_integrate.get());
  integrate_ps_.bind_ubo(GOO_VOL_UBO_SLOT, vol_buf_);
  integrate_ps_.bind_texture("prop_extinct_tx", &prop_extinct_ptr_, nearest);
  integrate_ps_.bind_texture("scatter_lit_tx", &scatter_lit_ptr_, nearest);
  integrate_ps_.bind_image("out_integrated_scatter_img", &integrated_scatter_ptr_);
  integrate_ps_.bind_image("out_integrated_transmit_img", &integrated_transmit_ptr_);
  integrate_ps_.barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS | GPU_BARRIER_TEXTURE_FETCH);
  integrate_ps_.dispatch(&integrate_dispatch_);

  resolve_ps_.init();
  resolve_ps_.state_set(DRW_STATE_NO_DRAW);
  resolve_ps_.shader_set(sh.volume_resolve.get());
  resolve_ps_.bind_ubo(GOO_VOL_UBO_SLOT, vol_buf_);
  resolve_ps_.bind_texture("integrated_scatter_tx", &integrated_scatter_ptr_, lin);
  resolve_ps_.bind_texture("integrated_transmit_tx", &integrated_transmit_ptr_, lin);
  resolve_ps_.bind_texture("depth_tx", &resolve_depth_ptr_, nearest);
  resolve_ps_.bind_image("color_img", &resolve_color_ptr_);
  resolve_ps_.barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS | GPU_BARRIER_TEXTURE_FETCH |
                      GPU_BARRIER_FRAMEBUFFER);
  resolve_ps_.dispatch(&resolve_dispatch_);
}

void VolumeModule::object_sync(Manager &manager,
                               ProbeModule &probes,
                               Object *ob,
                               ResourceHandleRange handle,
                               gpu::Batch *geom,
                               GPUMaterial *vol_gpumat,
                               bool deferred_compile)
{
  if (ob == nullptr || geom == nullptr || vol_gpumat == nullptr ||
      int(objects_.size()) >= GOO_MAX_VOLUME_OBJECTS)
  {
    return;
  }

  auto entry = std::make_unique<VolumeObjectEntry>();
  entry->obj_to_world = ob->object_to_world();
  entry->world_to_obj = math::invert(entry->obj_to_world);

  const std::optional<Bounds<float3>> bounds = BKE_object_boundbox_get(ob);
  if (bounds.has_value()) {
    entry->bounds_min = bounds->min;
    entry->bounds_max = bounds->max;
  }
  const float3 size = math::max(entry->bounds_max - entry->bounds_min, float3(1e-5f));
  const float3 mul = 1.0f / size;
  const float3 add = -entry->bounds_min * mul;
  entry->orco_mul = float4(mul, 0.0f);
  entry->orco_add = float4(add, 0.0f);

  ShaderModule &sh = ShaderModule::get();
  const GPUSamplerState nearest{GPU_SAMPLER_FILTERING_DEFAULT};

  entry->near_depth_ps.init();
  PassMain::Sub &near_sub = entry->near_depth_ps.sub("VolNear");
  near_sub.state_set(DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_LESS | DRW_STATE_CULL_BACK);
  near_sub.shader_set(sh.shadow_mesh.get());
  probes.bind_ubo(near_sub);
  near_sub.draw(geom, handle);

  entry->far_depth_ps.init();
  PassMain::Sub &far_sub = entry->far_depth_ps.sub("VolFar");
  far_sub.state_set(DRW_STATE_WRITE_DEPTH | DRW_STATE_DEPTH_GREATER | DRW_STATE_CULL_FRONT);
  far_sub.shader_set(sh.shadow_mesh.get());
  probes.bind_ubo(far_sub);
  far_sub.draw(geom, handle);

  entry->eval_ps.init();
  entry->eval_ps.state_set(DRW_STATE_WRITE_COLOR);
  entry->eval_ps.material_set(manager, vol_gpumat, deferred_compile);
  entry->eval_ps.bind_ubo(GOO_VOL_UBO_SLOT, vol_buf_);
  entry->eval_ps.bind_texture(RBUFS_UTILITY_TEX_SLOT, sh.utility_tx);
  entry->eval_ps.bind_texture("vol_obj_near_tx", &near_depth_ptr_, nearest);
  entry->eval_ps.bind_texture("vol_obj_far_tx", &far_depth_ptr_, nearest);
  entry->eval_ps.bind_image("vol_scatter_img", &prop_scatter_ptr_);
  entry->eval_ps.bind_image("vol_extinct_img", &prop_extinct_ptr_);
  entry->eval_ps.bind_image("vol_emissive_img", &prop_emissive_ptr_);
  entry->eval_ps.push_constant("sample_index", &unf_sample_index_);
  entry->eval_ps.draw_procedural(GPU_PRIM_TRIS, 1, 3);

  objects_.append(std::move(entry));
}

void VolumeModule::render(Manager &manager,
                          View &view,
                          gpu::Texture *depth_tx,
                          gpu::Texture *color_tx,
                          int sample_index,
                          int2 render_extent)
{
  if (!enabled() || depth_tx == nullptr || color_tx == nullptr) {
    if (!enabled()) {
      free_render_targets();
    }
    return;
  }

  const int2 ext = math::max(render_extent, int2(1));
  ensure_render_targets(ext);

  const float4x4 &wininv = view.wininv();
  const bool is_persp = view.is_persp();
  const float cam_near = -math::project_point(wininv, float3(0.0f, 0.0f, -1.0f)).z;
  const float cam_far = -math::project_point(wininv, float3(0.0f, 0.0f, 1.0f)).z;

  const bool custom_range = (volume_flags_ & GOO_VOL_FLAG_CUSTOM_RANGE) != 0;
  float depth_near = custom_range ? std::max(vol_start_, cam_near - 1e-4f) : cam_near;
  float depth_far = custom_range ? std::min(vol_end_, cam_far + 1e-4f) :
                                   (is_persp ? std::min(std::max(vol_end_, depth_near + 0.1f),
                                                        cam_far) :
                                               cam_far);
  if (is_persp) {
    depth_near = std::max(depth_near, 1e-3f);
  }
  depth_far = std::max(depth_far, depth_near + 0.05f);

  /* When only bounded object volumes exist (no world volume), tighten slices around them. */
  if (!has_world_volume_ && !objects_.is_empty()) {
    float obj_min_dist = std::numeric_limits<float>::max();
    float obj_max_dist = std::numeric_limits<float>::lowest();
    const float4x4 vm = view.viewmat();
    for (const auto &entry : objects_) {
      const float3 &mn = entry->bounds_min;
      const float3 &mx = entry->bounds_max;
      for (int c = 0; c < 8; c++) {
        const float3 lp((c & 1) ? mx.x : mn.x, (c & 2) ? mx.y : mn.y, (c & 4) ? mx.z : mn.z);
        const float3 vp = math::transform_point(vm, math::transform_point(entry->obj_to_world, lp));
        const float dist = -vp.z;
        obj_min_dist = std::min(obj_min_dist, dist);
        obj_max_dist = std::max(obj_max_dist, dist);
      }
    }
    depth_near = std::max(depth_near, obj_min_dist - 0.05f);
    depth_far = std::min(depth_far, std::max(obj_max_dist + 0.05f, depth_near + 0.05f));
    depth_far = std::max(depth_far, depth_near + 0.05f);
  }

  const float2 coord_scale = float2(ext) / float2(int2(tile_size_) * froxel_size_.xy());
  const float3 inv_froxel = 1.0f / float3(froxel_size_);

  vol_buf_.obj_to_world = float4x4::identity();
  vol_buf_.world_to_obj = float4x4::identity();
  vol_buf_.orco_mul = float4(1.0f, 1.0f, 1.0f, 0.0f);
  vol_buf_.orco_add = float4(0.0f, 0.0f, 0.0f, 0.0f);
  vol_buf_.froxel_size = int4(froxel_size_.x, froxel_size_.y, froxel_size_.z, volume_flags_);
  vol_buf_.inv_froxel_size = float4(inv_froxel.x, inv_froxel.y, inv_froxel.z, float(shadow_steps_));
  vol_buf_.depth_params = float4(depth_near, depth_far, depth_distrib_, is_persp ? 1.0f : 0.0f);
  vol_buf_.coord_params = float4(coord_scale.x, coord_scale.y, 1.0f / float(ext.x), 1.0f / float(ext.y));
  vol_buf_.clamp_params = float4(clamp_params_.x, clamp_params_.y, clamp_params_.z, 0.0f);
  vol_buf_.push_update();

  unf_sample_index_ = sample_index;
  resolve_depth_ptr_ = depth_tx;
  resolve_color_ptr_ = color_tx;

  /* Clear 3D froxel property textures first. */
  manager.submit(clear_ps_, view);

  /* World & Object volume froxel evaluation into vol_scatter/extinct/emissive. */
  vol_eval_fb_.ensure(GPU_ATTACHMENT_NONE, GPU_ATTACHMENT_TEXTURE(vol_dummy_fb_tx_));
  if (has_world_volume_) {
    vol_eval_fb_.bind();
    manager.submit(world_ps_, view);
    GPU_memory_barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS | GPU_BARRIER_TEXTURE_FETCH);
  }

  if (!objects_.is_empty()) {
    float4x4 offset_mat = float4x4::identity();
    offset_mat[0][0] = coord_scale.x;
    offset_mat[1][1] = coord_scale.y;
    offset_mat[3][0] = coord_scale.x - 1.0f;
    offset_mat[3][1] = coord_scale.y - 1.0f;
    froxel_view_.visibility_test(false);
    froxel_view_.sync(view.viewmat(), offset_mat * view.winmat());

    for (const auto &entry : objects_) {
      vol_obj_near_fb_.ensure(GPU_ATTACHMENT_TEXTURE(vol_obj_near_tx_));
      vol_obj_near_fb_.bind();
      GPU_framebuffer_clear_depth(vol_obj_near_fb_, 1.0f);
      manager.submit(entry->near_depth_ps, froxel_view_);

      vol_obj_far_fb_.ensure(GPU_ATTACHMENT_TEXTURE(vol_obj_far_tx_));
      vol_obj_far_fb_.bind();
      GPU_framebuffer_clear_depth(vol_obj_far_fb_, 0.0f);
      manager.submit(entry->far_depth_ps, froxel_view_);
      GPU_memory_barrier(GPU_BARRIER_FRAMEBUFFER | GPU_BARRIER_TEXTURE_FETCH);

      vol_buf_.obj_to_world = entry->obj_to_world;
      vol_buf_.world_to_obj = entry->world_to_obj;
      vol_buf_.orco_mul = entry->orco_mul;
      vol_buf_.orco_add = entry->orco_add;
      vol_buf_.froxel_size.w = volume_flags_ | GOO_VOL_FLAG_IS_OBJECT;
      vol_buf_.push_update();

      vol_eval_fb_.bind();
      manager.submit(entry->eval_ps, view);
      GPU_memory_barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS | GPU_BARRIER_TEXTURE_FETCH);
    }

    vol_buf_.obj_to_world = float4x4::identity();
    vol_buf_.world_to_obj = float4x4::identity();
    vol_buf_.froxel_size.w = volume_flags_;
    vol_buf_.push_update();
  }

  manager.submit(scatter_ps_, view);
  manager.submit(integrate_ps_, view);
  manager.submit(resolve_ps_, view);
  GPU_memory_barrier(GPU_BARRIER_SHADER_IMAGE_ACCESS | GPU_BARRIER_TEXTURE_FETCH);
}

}  // namespace blender::goo
