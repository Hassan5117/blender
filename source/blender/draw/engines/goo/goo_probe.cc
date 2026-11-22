/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 */

#include <algorithm>
#include <cmath>

#include "BLI_math_geom.h"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"

#include "DNA_lightprobe_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "GPU_framebuffer.hh"
#include "GPU_state.hh"
#include "GPU_texture.hh"

#include "goo_defines.hh"
#include "goo_probe.hh"
#include "goo_shader.hh"

namespace blender::goo {

static float4x4 goo_cubeface_mat(int face)
{
  switch (face) {
    default:
    case 0: /* +X */
      return float4x4({0.0f, 0.0f, -1.0f, 0.0f},
                      {0.0f, -1.0f, 0.0f, 0.0f},
                      {-1.0f, 0.0f, 0.0f, 0.0f},
                      {0.0f, 0.0f, 0.0f, 1.0f});
    case 1: /* -X */
      return float4x4({0.0f, 0.0f, 1.0f, 0.0f},
                      {0.0f, -1.0f, 0.0f, 0.0f},
                      {1.0f, 0.0f, 0.0f, 0.0f},
                      {0.0f, 0.0f, 0.0f, 1.0f});
    case 2: /* +Y */
      return float4x4({1.0f, 0.0f, 0.0f, 0.0f},
                      {0.0f, 0.0f, -1.0f, 0.0f},
                      {0.0f, 1.0f, 0.0f, 0.0f},
                      {0.0f, 0.0f, 0.0f, 1.0f});
    case 3: /* -Y */
      return float4x4({1.0f, 0.0f, 0.0f, 0.0f},
                      {0.0f, 0.0f, 1.0f, 0.0f},
                      {0.0f, -1.0f, 0.0f, 0.0f},
                      {0.0f, 0.0f, 0.0f, 1.0f});
    case 4: /* +Z */
      return float4x4({1.0f, 0.0f, 0.0f, 0.0f},
                      {0.0f, -1.0f, 0.0f, 0.0f},
                      {0.0f, 0.0f, -1.0f, 0.0f},
                      {0.0f, 0.0f, 0.0f, 1.0f});
    case 5: /* -Z */
      return float4x4({-1.0f, 0.0f, 0.0f, 0.0f},
                      {0.0f, -1.0f, 0.0f, 0.0f},
                      {0.0f, 0.0f, 1.0f, 0.0f},
                      {0.0f, 0.0f, 0.0f, 1.0f});
  }
}

void ProbeModule::ensure_static_textures(int env_res, int env_layers)
{
  const eGPUTextureUsage mip_usage = GPU_TEXTURE_USAGE_SHADER_READ | GPU_TEXTURE_USAGE_SHADER_WRITE;

  if (dummy_env_tx_.ensure_2d_array(
          gpu::TextureFormat::SFLOAT_16_16_16_16, int2(1), 1, mip_usage, nullptr, 1))
  {
    float4 zero(0.0f);
    GPU_texture_clear(dummy_env_tx_, GPU_DATA_FLOAT, &zero);
    GPU_texture_mipmap_mode(dummy_env_tx_, true, false);
  }

  if (dummy_planar_tx_.ensure_2d_array(
          gpu::TextureFormat::SFLOAT_16_16_16_16, int2(1), 1, mip_usage, nullptr, 1))
  {
    float4 zero(0.0f);
    GPU_texture_clear(dummy_planar_tx_, GPU_DATA_FLOAT, &zero);
    GPU_texture_mipmap_mode(dummy_planar_tx_, true, false);
  }

  const bool resized = env_probe_tx_.ensure_2d_array(gpu::TextureFormat::SFLOAT_16_16_16_16,
                                                     int2(env_res),
                                                     std::max(1, env_layers),
                                                     mip_usage,
                                                     nullptr,
                                                     GOO_ENV_MIPS);
  if (resized) {
    env_probe_tx_.ensure_mip_views();
    for (int m = 0; m < env_probe_tx_.mip_count(); m++) {
      float4 zero(0.0f);
      GPU_texture_clear(env_probe_tx_.mip_view(m), GPU_DATA_FLOAT, &zero);
    }
    GPU_texture_mipmap_mode(env_probe_tx_, true, true);
    static_probes_captured_ = false;
    probes_dirty_ = true;
  }
  else {
    env_probe_tx_.ensure_mip_views();
  }

  if (env_probe_ptr_ == nullptr) {
    env_probe_ptr_ = env_probe_tx_;
  }
  if (planar_ptr_ == nullptr) {
    planar_ptr_ = dummy_planar_tx_;
  }
}

void ProbeModule::init_compute_passes()
{
  ShaderModule &sh = ShaderModule::get();
  const GPUSamplerState lin_sampler{GPU_SAMPLER_FILTERING_LINEAR};

  env_remap_ps_.init();
  env_remap_ps_.state_set(DRW_STATE_NO_DRAW);
  env_remap_ps_.shader_set(sh.env_remap.get());
  env_remap_ps_.bind_texture("capture_cube_tx", &unf_src_tx_, lin_sampler);
  env_remap_ps_.bind_image("env_mip0_img", &unf_dst_img_);
  env_remap_ps_.push_constant("dst_layer", &unf_dst_layer_);
  env_remap_ps_.push_constant("dst_res", &unf_dst_res_);
  env_remap_ps_.push_constant("indirect_clamp", &unf_indirect_clamp_);
  env_remap_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
  env_remap_ps_.dispatch(&unf_remap_dispatch_);

  env_convolve_ps_.init();
  env_convolve_ps_.state_set(DRW_STATE_NO_DRAW);
  env_convolve_ps_.shader_set(sh.env_convolve.get());
  env_convolve_ps_.bind_texture("env_src_tx", &unf_src_tx_, lin_sampler);
  env_convolve_ps_.bind_image("env_dst_img", &unf_dst_img_);
  env_convolve_ps_.push_constant("layer", &unf_dst_layer_);
  env_convolve_ps_.push_constant("src_res", &unf_src_res_);
  env_convolve_ps_.push_constant("dst_res", &unf_dst_res_);
  env_convolve_ps_.push_constant("target_roughness", &unf_target_roughness_);
  env_convolve_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
  env_convolve_ps_.dispatch(&unf_convolve_dispatch_);

  cube_to_sh_ps_.init();
  cube_to_sh_ps_.state_set(DRW_STATE_NO_DRAW);
  cube_to_sh_ps_.shader_set(sh.cube_to_sh.get());
  cube_to_sh_ps_.bind_texture("capture_cube_tx", &unf_src_tx_, lin_sampler);
  cube_to_sh_ps_.bind_ssbo("out_sh_buf", volume_sh_buf_);
  cube_to_sh_ps_.push_constant("dst_index", &unf_dst_index_);
  cube_to_sh_ps_.push_constant("grid_dim", &unf_grid_dim_);
  cube_to_sh_ps_.push_constant("indirect_clamp", &unf_indirect_clamp_);
  cube_to_sh_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_STORAGE);
  cube_to_sh_ps_.dispatch(int3(1, 1, 1));

  planar_downsample_ps_.init();
  planar_downsample_ps_.state_set(DRW_STATE_NO_DRAW);
  planar_downsample_ps_.shader_set(sh.color_downsample_array.get());
  planar_downsample_ps_.bind_texture("src_tx", &unf_src_tx_, lin_sampler);
  planar_downsample_ps_.bind_image("dst_img", &unf_dst_img_);
  planar_downsample_ps_.push_constant("src_extent", &unf_planar_src_ext_);
  planar_downsample_ps_.push_constant("dst_extent", &unf_planar_dst_ext_);
  planar_downsample_ps_.push_constant("layer", &unf_planar_layer_);
  planar_downsample_ps_.barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS |
                                GPU_BARRIER_FRAMEBUFFER);
  planar_downsample_ps_.dispatch(&unf_planar_dispatch_);
}

void ProbeModule::begin_sync(const Scene *scene,
                             const LookdevParameters &lookdev_params,
                             bool has_world_shader,
                             bool use_sky)
{
  spheres_.clear();
  planars_.clear();
  volumes_.clear();

  const int raw_cube_res = scene ? std::clamp(scene->eevee.gi_cubemap_resolution, 64, 1024) : 512;
  env_oct_res_ = std::clamp(raw_cube_res / 2, 128, 512);
  /* Ensure power of two >= 128 so GOO_ENV_MIPS (6) mips divide cleanly down to >= 4px. */
  int pot = 128;
  while (pot * 2 <= env_oct_res_ && pot < 512) {
    pot *= 2;
  }
  env_oct_res_ = pot;

  indirect_intensity_ = scene ? std::max(scene->eevee.indirect_light_intensity, 0.0f) : 1.0f;
  indirect_clamp_ = scene ? std::max(scene->eevee.clamp_surface_indirect, 0.0f) : 0.0f;
  has_world_ibl_ = has_world_shader && use_sky;

  int ssr_flags = 0;
  float4 ssr_params(0.25f, 0.5f, 0.2f, 0.075f);
  float firefly = 10.0f;
  if (scene != nullptr) {
    if ((scene->eevee.flag & SCE_EEVEE_SSR_ENABLED) != 0) {
      ssr_flags |= GOO_SSR_FLAG_ENABLED;
    }
    if ((scene->eevee.flag & SCE_EEVEE_SSR_REFRACTION) != 0) {
      ssr_flags |= GOO_SSR_FLAG_REFRACTION;
    }
    if ((scene->eevee.flag & SCE_EEVEE_SSR_HALF_RESOLUTION) != 0) {
      ssr_flags |= GOO_SSR_FLAG_HALFRES;
    }
    ssr_params = float4(std::clamp(scene->eevee.ssr_quality, 0.0f, 1.0f),
                        std::clamp(scene->eevee.ssr_max_roughness, 0.0f, 1.0f),
                        std::max(scene->eevee.ssr_thickness, 1e-4f),
                        std::clamp(scene->eevee.ssr_border_fade, 0.0f, 0.5f));
    firefly = std::max(scene->eevee.ssr_firefly_fac, 0.0f);
  }

  const bool flags_changed = assign_if_different(prev_ssr_flags_, ssr_flags);
  const bool params_changed = assign_if_different(prev_ssr_params_, ssr_params);
  ssr_changed_ = flags_changed || params_changed;

  const bool is_lookdev_bg = !lookdev_params.show_scene_world;
  const float bg_opacity = is_lookdev_bg ? std::clamp(lookdev_params.background_opacity, 0.0f, 1.0f) :
                                           1.0f;
  const float bg_blur_lod = is_lookdev_bg ?
                                (std::clamp(lookdev_params.blur, 0.0f, 1.0f) *
                                 float(GOO_ENV_MIPS - 1)) :
                                0.0f;

  probe_buf_.capture_clip_plane = float4(0.0f, 0.0f, 1.0f, 0.0f);
  probe_buf_.ssr_params = ssr_params;
  probe_buf_.indirect_params = float4(
      firefly, indirect_intensity_, indirect_clamp_, float(GOO_ENV_MIPS));
  probe_buf_.lookdev_params = float4(
      bg_opacity, bg_blur_lod, is_lookdev_bg ? 1.0f : 0.0f, has_world_ibl_ ? 1.0f : 0.0f);
  probe_buf_.sss_params = float4(
      float(scene ? std::clamp(scene->eevee.sss_samples, 1, 32) : 7),
      scene ? std::clamp(scene->eevee.sss_jitter_threshold, 0.0f, 1.0f) : 0.3f,
      0.0f,
      0.0f);
  probe_buf_.sphere_probe_count = 0;
  probe_buf_.planar_probe_count = 0;
  probe_buf_.volume_probe_count = 0;
  probe_buf_.capture_flags = 0;
  probe_buf_.ssr_flags = ssr_flags;
  probe_buf_.env_oct_res = env_oct_res_;

  const int cur_layers = env_probe_tx_.gpu_texture() ?
                             std::max(1, GPU_texture_depth(env_probe_tx_)) :
                             1;
  ensure_static_textures(env_oct_res_, cur_layers);

  init_compute_passes();
}

void ProbeModule::probe_sync(const Object *ob)
{
  if (ob == nullptr || ob->type != OB_LIGHTPROBE || ob->data == nullptr) {
    return;
  }
  const LightProbe &lp = DRW_object_get_data_for_drawing<const LightProbe>(*ob);
  const float4x4 &obmat = ob->object_to_world();

  switch (lp.type) {
    case LIGHTPROBE_TYPE_SPHERE: {
      if (spheres_.size() >= GOO_MAX_SPHERE_PROBES) {
        return;
      }
      SyncedSphereProbe sp;
      sp.object_to_world = obmat;
      const float distinf = std::max(lp.distinf, 1e-3f);
      const bool custom_par = (lp.flag & LIGHTPROBE_FLAG_CUSTOM_PARALLAX) != 0;
      const float distpar = custom_par ? std::max(lp.distpar, distinf) : distinf;
      const float4x4 inf_world = math::scale(obmat, float3(distinf));
      const float4x4 par_world = math::scale(obmat, float3(distpar));
      sp.world_to_influence = math::invert(inf_world);
      sp.world_to_parallax = math::invert(par_world);
      sp.location = obmat.location();
      sp.falloff = std::clamp(lp.falloff, 1e-3f, 1.0f);
      sp.influence_shape = (lp.attenuation_type == LIGHTPROBE_SHAPE_BOX) ?
                               GOO_PROBE_SHAPE_BOX :
                               GOO_PROBE_SHAPE_ELLIPSOID;
      sp.parallax_shape = (custom_par ? lp.parallax_type : lp.attenuation_type) ==
                                  LIGHTPROBE_SHAPE_BOX ?
                              GOO_PROBE_SHAPE_BOX :
                              GOO_PROBE_SHAPE_ELLIPSOID;
      sp.custom_parallax = custom_par;
      sp.intensity = std::max(lp.intensity, 0.0f);
      sp.parallax_distance = distpar;
      sp.distinf = distinf;
      sp.volume = fabsf(math::determinant(inf_world));
      sp.clip_start = std::max(lp.clipsta, 0.01f);
      sp.clip_end = std::max(lp.clipend, sp.clip_start + 1.0f);
      spheres_.append(sp);
      break;
    }
    case LIGHTPROBE_TYPE_PLANE: {
      if (planars_.size() >= GOO_MAX_PLANAR_PROBES) {
        return;
      }
      SyncedPlanarProbe pp;
      float4x4 p2w = obmat;
      p2w.z_axis() = math::normalize(float3(p2w.z_axis())) * std::max(lp.distinf, 1e-3f);
      pp.plane_to_world = p2w;
      pp.world_to_plane = math::invert(p2w);

      float4x4 ortho = float4x4::identity();
      ortho.x_axis().xyz() = math::normalize(float3(obmat.x_axis()));
      ortho.y_axis().xyz() = math::normalize(float3(obmat.y_axis()));
      ortho.z_axis().xyz() = math::normalize(float3(obmat.z_axis()));
      ortho.location() = obmat.location();
      pp.plane_to_world_ortho = ortho;
      pp.refl_mat = ortho * math::from_scale<float4x4>(float3(1.0f, 1.0f, -1.0f)) *
                    math::invert(ortho);
      pp.plane_loc = ortho.location();
      pp.normal = float3(ortho.z_axis());
      pp.falloff = std::clamp(lp.falloff, 1e-3f, 1.0f);
      pp.parallax_distance = std::max(lp.distpar, 0.0f);
      pp.intensity = std::max(lp.intensity, 0.0f);
      pp.clip_offset = std::max(lp.clipsta, 1e-4f);
      planars_.append(pp);
      break;
    }
    case LIGHTPROBE_TYPE_VOLUME: {
      if (volumes_.size() >= GOO_MAX_VOLUME_PROBES) {
        return;
      }
      SyncedVolumeProbe vp;
      vp.object_to_world = obmat;
      vp.world_to_volume = math::invert(obmat);
      vp.res = int3(std::max(lp.grid_resolution_x, 1),
                    std::max(lp.grid_resolution_y, 1),
                    std::max(lp.grid_resolution_z, 1));
      vp.falloff = std::clamp(lp.falloff, 1e-3f, 1.0f);
      vp.intensity = std::max(lp.intensity, 0.0f);
      vp.normal_bias = lp.grid_normal_bias;
      vp.view_bias = lp.grid_view_bias;
      vp.clip_start = std::min(std::max(lp.clipsta, 0.02f), 0.1f);
      vp.clip_end = std::max(lp.clipend, 20.0f);
      volumes_.append(vp);
      break;
    }
    default:
      break;
  }
}

void ProbeModule::end_sync()
{
  /* Sort sphere probes smallest-volume first so nested local probes take priority. */
  std::stable_sort(spheres_.begin(),
                   spheres_.end(),
                   [](const SyncedSphereProbe &a, const SyncedSphereProbe &b) {
                     return a.volume < b.volume;
                   });

  for (int i : spheres_.index_range()) {
    const SyncedSphereProbe &src = spheres_[i];
    GooSphereProbeData &dst = sphere_buf_[i];
    dst.world_to_influence = src.world_to_influence;
    dst.world_to_parallax = src.world_to_parallax;
    dst.location_falloff = float4(src.location, src.falloff);
    dst.params = int4(i + 1, src.influence_shape, src.parallax_shape, src.custom_parallax ? 1 : 0);
    dst.extra = float4(src.intensity, src.parallax_distance, src.distinf, src.volume);
  }
  probe_buf_.sphere_probe_count = int(spheres_.size());

  /* Assign volume SH buffer offsets, reserving slot 0 for the World SH. */
  int sh_offset = 1;
  int total_volume_count = 0;
  for (int i : volumes_.index_range()) {
    SyncedVolumeProbe &src = volumes_[i];
    const int remaining = GOO_MAX_VOLUME_SH_CELLS - sh_offset;
    if (remaining <= 0) {
      break;
    }
    int3 res = src.res;
    while (res.x * res.y * res.z > remaining && (res.x > 1 || res.y > 1 || res.z > 1)) {
      if (res.x >= res.y && res.x >= res.z && res.x > 1) {
        res.x--;
      }
      else if (res.y >= res.z && res.y > 1) {
        res.y--;
      }
      else if (res.z > 1) {
        res.z--;
      }
    }
    src.res = res;
    src.offset = sh_offset;
    sh_offset += res.x * res.y * res.z;

    GooVolumeProbeData &dst = volume_buf_[total_volume_count++];
    dst.world_to_volume = src.world_to_volume;
    dst.res_offset = int4(src.res.x, src.res.y, src.res.z, src.offset);
    dst.params = float4(src.falloff, src.intensity, src.normal_bias, src.view_bias);
  }
  volumes_.resize(total_volume_count);
  probe_buf_.volume_probe_count = total_volume_count;
  probe_buf_.planar_probe_count = int(planars_.size());

  ensure_static_textures(env_oct_res_, 1 + int(spheres_.size()));

  const int cur_hash = int(spheres_.size()) * 131 + int(planars_.size()) * 17 +
                         total_volume_count * 7 + env_oct_res_;
  if (cur_hash != prev_probe_hash_) {
    prev_probe_hash_ = cur_hash;
    probes_dirty_ = true;
  }

  probe_buf_.push_update();
  sphere_buf_.push_update();
  planar_buf_.push_update();
  volume_buf_.push_update();
  volume_sh_buf_.push_update();
}

void ProbeModule::dispatch_cubemap_to_oct(Manager &manager, gpu::Texture *cube_tx, int dst_layer)
{
  unf_src_tx_ = cube_tx;
  unf_dst_img_ = env_probe_tx_.mip_view(0);
  unf_dst_layer_ = dst_layer;
  unf_dst_res_ = env_oct_res_;
  unf_indirect_clamp_ = indirect_clamp_;
  unf_remap_dispatch_ = int3(math::divide_ceil(int2(env_oct_res_), int2(8)), 1);
  manager.submit(env_remap_ps_);

  unf_src_tx_ = env_probe_tx_.mip_view(0);
  unf_src_res_ = env_oct_res_;
  for (int m = 1; m < GOO_ENV_MIPS; m++) {
    const int dst_res = std::max(env_oct_res_ >> m, 1);
    const float mip_frac = float(m) / float(GOO_ENV_MIPS - 1);
    unf_dst_img_ = env_probe_tx_.mip_view(m);
    unf_dst_res_ = dst_res;
    unf_target_roughness_ = mip_frac * mip_frac;
    unf_convolve_dispatch_ = int3(math::divide_ceil(int2(dst_res), int2(8)), 1);
    manager.submit(env_convolve_ps_);
  }
  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
}

void ProbeModule::dispatch_cubemap_to_sh(Manager &manager,
                                         gpu::Texture *cube_tx,
                                         int dst_index,
                                         int grid_dim)
{
  unf_src_tx_ = cube_tx;
  unf_dst_index_ = dst_index;
  unf_grid_dim_ = grid_dim;
  unf_indirect_clamp_ = indirect_clamp_;
  manager.submit(cube_to_sh_ps_);
  GPU_memory_barrier(GPU_BARRIER_SHADER_STORAGE);
}

void ProbeModule::capture_world_and_static_probes(Manager &manager,
                                                  PassSimple *world_capture_pass,
                                                  PassMain &prepass_ps,
                                                  PassMain &surface_ps,
                                                  PassMain &blend_ps,
                                                  bool has_blend_draws,
                                                  bool has_world_shader,
                                                  bool use_sky,
                                                  float4 world_clear_color,
                                                  bool force_recapture)
{
  if (!force_recapture && static_probes_captured_ && !probes_dirty_) {
    return;
  }

  ensure_static_textures(env_oct_res_, 1 + int(spheres_.size()));

  /* Avoid read-write hazards: while capturing world/static probes, shaders read dummy env/planar. */
  env_probe_ptr_ = dummy_env_tx_;
  planar_ptr_ = dummy_planar_tx_;

  const eGPUTextureUsage cube_usage = GPU_TEXTURE_USAGE_ATTACHMENT | GPU_TEXTURE_USAGE_SHADER_READ;
  const int world_cube_res = std::max(env_oct_res_, 128);
  capture_cube_tx_.ensure_cube(gpu::TextureFormat::SFLOAT_16_16_16_16, world_cube_res, cube_usage);
  capture_depth_tx_.ensure_2d(
      gpu::TextureFormat::SFLOAT_32_DEPTH, int2(world_cube_res), cube_usage);

  capture_view_.visibility_test(false);

  /* 1. Capture World cubemap (layer 0) and World SH (slot 0). */
  if (has_world_shader && use_sky && world_capture_pass != nullptr) {
    probe_buf_.capture_flags = GOO_CAPTURE_WORLD | GOO_CAPTURE_NO_PASS_WRITE | GOO_CAPTURE_NO_SSR;
    probe_buf_.push_update();

    float4x4 winmat;
    perspective_m4(winmat.ptr(), -0.1f, 0.1f, -0.1f, 0.1f, 0.1f, 100.0f);

    for (int face = 0; face < 6; face++) {
      float4x4 viewmat = goo_cubeface_mat(face);
      capture_view_.sync(viewmat, winmat);
      capture_fb_.ensure(GPU_ATTACHMENT_TEXTURE(capture_depth_tx_),
                         GPU_ATTACHMENT_TEXTURE_CUBEFACE(capture_cube_tx_, face));
      capture_fb_.bind();
      GPU_framebuffer_clear_color_depth(capture_fb_, double4(0.0), 1.0f);
      manager.submit(*world_capture_pass, capture_view_);
    }

    dispatch_cubemap_to_oct(manager, capture_cube_tx_, 0);
    dispatch_cubemap_to_sh(manager, capture_cube_tx_, 0, 32);
  }
  else {
    for (int face = 0; face < 6; face++) {
      capture_fb_.ensure(GPU_ATTACHMENT_NONE,
                         GPU_ATTACHMENT_TEXTURE_CUBEFACE(capture_cube_tx_, face));
      capture_fb_.bind();
      GPU_framebuffer_clear_color(capture_fb_, double4(world_clear_color));
    }
    dispatch_cubemap_to_oct(manager, capture_cube_tx_, 0);
    dispatch_cubemap_to_sh(manager, capture_cube_tx_, 0, 16);
  }

  /* Now World IBL (layer 0) and World SH (slot 0) are ready for Sphere/Volume scene captures! */
  env_probe_ptr_ = env_probe_tx_;

  /* 2. Capture Reflection Sphere Probes (layers 1..N). */
  if (!spheres_.is_empty()) {
    probe_buf_.capture_flags = GOO_CAPTURE_SPHERE | GOO_CAPTURE_NO_PASS_WRITE | GOO_CAPTURE_NO_SSR;
    probe_buf_.push_update();

    for (int i : spheres_.index_range()) {
      const SyncedSphereProbe &sp = spheres_[i];
      float4x4 winmat;
      const float near = sp.clip_start;
      const float far = sp.clip_end;
      perspective_m4(winmat.ptr(), -near, near, -near, near, near, far);

      for (int face = 0; face < 6; face++) {
        float4x4 viewmat = goo_cubeface_mat(face) * math::from_location<float4x4>(-sp.location);
        capture_view_.sync(viewmat, winmat);
        capture_fb_.ensure(GPU_ATTACHMENT_TEXTURE(capture_depth_tx_),
                           GPU_ATTACHMENT_TEXTURE_CUBEFACE(capture_cube_tx_, face));
        capture_fb_.bind();
        GPU_framebuffer_clear_color_depth(capture_fb_, double4(world_clear_color), 1.0f);

        manager.submit(prepass_ps, capture_view_);
        manager.submit(surface_ps, capture_view_);
        if (has_world_shader && use_sky && world_capture_pass != nullptr) {
          manager.submit(*world_capture_pass, capture_view_);
        }
        if (has_blend_draws) {
          manager.submit(blend_ps, capture_view_);
        }
      }

      dispatch_cubemap_to_oct(manager, capture_cube_tx_, i + 1);
    }
  }

  /* 3. Capture Irradiance Volume Probe grids into SH cells (offsets >= 1). */
  if (!volumes_.is_empty()) {
    const int vol_cube_res = 32;
    capture_cube_tx_.ensure_cube(gpu::TextureFormat::SFLOAT_16_16_16_16, vol_cube_res, cube_usage);
    capture_depth_tx_.ensure_2d(
        gpu::TextureFormat::SFLOAT_32_DEPTH, int2(vol_cube_res), cube_usage);

    probe_buf_.capture_flags = GOO_CAPTURE_VOLUME | GOO_CAPTURE_NO_PASS_WRITE | GOO_CAPTURE_NO_SSR;
    probe_buf_.push_update();

    for (const SyncedVolumeProbe &vp : volumes_) {
      float4x4 winmat;
      const float near = vp.clip_start;
      const float far = vp.clip_end;
      perspective_m4(winmat.ptr(), -near, near, -near, near, near, far);

      for (int z = 0; z < vp.res.z; z++) {
        const float lz = (vp.res.z > 1) ? ((float(z) / float(vp.res.z - 1)) * 2.0f - 1.0f) : 0.0f;
        for (int y = 0; y < vp.res.y; y++) {
          const float ly = (vp.res.y > 1) ? ((float(y) / float(vp.res.y - 1)) * 2.0f - 1.0f) :
                                            0.0f;
          for (int x = 0; x < vp.res.x; x++) {
            const float lx = (vp.res.x > 1) ? ((float(x) / float(vp.res.x - 1)) * 2.0f - 1.0f) :
                                              0.0f;
            const float3 sample_pos = math::transform_point(vp.object_to_world,
                                                            float3(lx, ly, lz));

            for (int face = 0; face < 6; face++) {
              float4x4 viewmat = goo_cubeface_mat(face) * math::from_location<float4x4>(-sample_pos);
              capture_view_.sync(viewmat, winmat);
              capture_fb_.ensure(GPU_ATTACHMENT_TEXTURE(capture_depth_tx_),
                                 GPU_ATTACHMENT_TEXTURE_CUBEFACE(capture_cube_tx_, face));
              capture_fb_.bind();
              GPU_framebuffer_clear_color_depth(capture_fb_, double4(world_clear_color), 1.0f);

              manager.submit(prepass_ps, capture_view_);
              manager.submit(surface_ps, capture_view_);
              if (has_world_shader && use_sky && world_capture_pass != nullptr) {
                manager.submit(*world_capture_pass, capture_view_);
              }
              if (has_blend_draws) {
                manager.submit(blend_ps, capture_view_);
              }
            }

            const int cell_idx = vp.offset + (z * vp.res.y + y) * vp.res.x + x;
            dispatch_cubemap_to_sh(manager, capture_cube_tx_, cell_idx, 16);
          }
        }
      }
    }
  }

  probe_buf_.capture_flags = 0;
  probe_buf_.push_update();
  static_probes_captured_ = true;
  probes_dirty_ = false;
}

void ProbeModule::capture_planar_probes(Manager &manager,
                                        const View &camera_view,
                                        int2 render_extent,
                                        PassSimple * /*world_capture_pass*/,
                                        PassMain &prepass_ps,
                                        PassMain &surface_ps,
                                        PassMain &blend_ps,
                                        bool has_blend_draws,
                                        bool /*has_world_shader*/,
                                        bool /*use_sky*/)
{
  if (planars_.is_empty()) {
    planar_ptr_ = dummy_planar_tx_;
    probe_buf_.planar_probe_count = 0;
    probe_buf_.capture_flags = 0;
    probe_buf_.push_update();
    return;
  }

  const int2 planar_ext = math::max(render_extent / 2, int2(64));
  const int layer_count = int(planars_.size());
  const eGPUTextureUsage planar_usage = GPU_TEXTURE_USAGE_ATTACHMENT | GPU_TEXTURE_USAGE_SHADER_READ |
                                        GPU_TEXTURE_USAGE_SHADER_WRITE;

  planar_tx_.ensure_2d_array(gpu::TextureFormat::SFLOAT_16_16_16_16,
                             planar_ext,
                             layer_count,
                             planar_usage,
                             nullptr,
                             GOO_PLANAR_MIPS);
  planar_tx_.ensure_mip_views();
  planar_tx_.ensure_layer_views();
  GPU_texture_mipmap_mode(planar_tx_, true, true);

  planar_depth_tx_.ensure_2d_array(
      gpu::TextureFormat::SFLOAT_32_DEPTH, planar_ext, layer_count, planar_usage);
  planar_depth_tx_.ensure_layer_views();

  /* Avoid Metal read/write hazard while rendering planar captures. */
  planar_ptr_ = dummy_planar_tx_;
  capture_view_.visibility_test(false);

  const float4x4 flip_y = math::from_scale<float4x4>(float3(1.0f, -1.0f, 1.0f));
  const float3 view_loc = camera_view.location();
  const float3 view_fwd = camera_view.forward();

  for (int i : planars_.index_range()) {
    const SyncedPlanarProbe &pp = planars_[i];
    float3 n = pp.normal;
    const float3 view_vec = camera_view.is_persp() ? (view_loc - pp.plane_loc) : view_fwd;
    if (math::dot(view_vec, n) < 0.0f) {
      n = -n;
    }

    const float4x4 refl_viewmat = flip_y * camera_view.viewmat() * pp.refl_mat;
    float4x4 refl_winmat = camera_view.winmat();
    refl_winmat[2][1] = -refl_winmat[2][1];
    capture_view_.sync(refl_viewmat, refl_winmat);

    GooPlanarProbeData &dst = planar_buf_[i];
    dst.world_to_plane = pp.world_to_plane;
    dst.ref_vp = refl_winmat * refl_viewmat;
    dst.normal_falloff = float4(n, pp.falloff);
    dst.params = float4(float(i), pp.parallax_distance, pp.intensity, 0.0f);

    /* gl_ClipDistance[0] keeps >= 0. Subtracting clip_offset clips the reflector plane at Z=0
     * while capturing reflected geometry above it (Z >= clip_offset). */
    probe_buf_.capture_clip_plane = float4(
        n, -(math::dot(n, pp.plane_loc) + std::max(pp.clip_offset, 1e-4f)));
    probe_buf_.capture_flags = GOO_CAPTURE_PLANAR | GOO_CAPTURE_CLIP_PLANE |
                               GOO_CAPTURE_NO_PASS_WRITE | GOO_CAPTURE_NO_SSR;
    probe_buf_.push_update();

    planar_fb_.ensure(GPU_ATTACHMENT_TEXTURE_LAYER(planar_depth_tx_, i),
                      GPU_ATTACHMENT_TEXTURE_LAYER(planar_tx_.mip_view(0), i));
    planar_fb_.bind();
    GPU_framebuffer_clear_color_depth(planar_fb_, double4(0.0), 1.0f);

    manager.submit(prepass_ps, capture_view_);
    manager.submit(surface_ps, capture_view_);
    if (has_blend_draws) {
      manager.submit(blend_ps, capture_view_);
    }

    int2 cur_ext = planar_ext;
    for (int m = 1; m < GOO_PLANAR_MIPS; m++) {
      const int2 dst_ext = math::max(cur_ext / 2, int2(1));
      unf_src_tx_ = planar_tx_.mip_view(m - 1);
      unf_dst_img_ = planar_tx_.mip_view(m);
      unf_planar_src_ext_ = cur_ext;
      unf_planar_dst_ext_ = dst_ext;
      unf_planar_layer_ = i;
      unf_planar_dispatch_ = int3(math::divide_ceil(dst_ext, int2(8)), 1);
      manager.submit(planar_downsample_ps_);
      cur_ext = dst_ext;
    }
  }

  planar_buf_.push_update();
  probe_buf_.capture_flags = 0;
  probe_buf_.planar_probe_count = layer_count;
  probe_buf_.push_update();

  GPU_memory_barrier(GPU_BARRIER_TEXTURE_FETCH | GPU_BARRIER_SHADER_IMAGE_ACCESS);
  planar_ptr_ = planar_tx_;
}

}  // namespace blender::goo
