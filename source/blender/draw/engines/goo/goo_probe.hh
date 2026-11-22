/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * World IBL (octahedral + SH L2), LookDev parameters, and Light Probes (Sphere, Planar, Volume).
 */

#pragma once

#include "BLI_vector.hh"

#include "DRW_gpu_wrapper.hh"
#include "draw_manager.hh"
#include "draw_pass.hh"
#include "draw_view.hh"

#include "goo_lookdev.hh"
#include "goo_shader_shared.hh"

namespace blender {
struct Object;
struct Scene;
}  // namespace blender

namespace blender::goo {

using namespace draw;

class ProbeModule {
 public:
  struct SyncedSphereProbe {
    float4x4 object_to_world = float4x4::identity();
    float4x4 world_to_influence = float4x4::identity();
    float4x4 world_to_parallax = float4x4::identity();
    float3 location = float3(0.0f);
    float falloff = 0.2f;
    int influence_shape = GOO_PROBE_SHAPE_ELLIPSOID;
    int parallax_shape = GOO_PROBE_SHAPE_ELLIPSOID;
    bool custom_parallax = false;
    float intensity = 1.0f;
    float parallax_distance = 2.5f;
    float distinf = 2.5f;
    float volume = 1.0f;
    float clip_start = 0.8f;
    float clip_end = 20.0f;
  };

  struct SyncedPlanarProbe {
    float4x4 plane_to_world = float4x4::identity();
    float4x4 plane_to_world_ortho = float4x4::identity();
    float4x4 world_to_plane = float4x4::identity();
    float4x4 refl_mat = float4x4::identity();
    float3 plane_loc = float3(0.0f);
    float3 normal = float3(0.0f, 0.0f, 1.0f);
    float falloff = 0.5f;
    float parallax_distance = 2.5f;
    float intensity = 1.0f;
    float clip_offset = 0.001f;
  };

  struct SyncedVolumeProbe {
    float4x4 object_to_world = float4x4::identity();
    float4x4 world_to_volume = float4x4::identity();
    int3 res = int3(4);
    int offset = 1;
    float falloff = 0.2f;
    float intensity = 1.0f;
    float normal_bias = 0.3f;
    float view_bias = 0.0f;
    float clip_start = 0.05f;
    float clip_end = 20.0f;
  };

 private:
  UniformBuffer<GooProbeBlock> probe_buf_ = {"GooProbeBlock"};
  StorageArrayBuffer<GooSphereProbeData, GOO_MAX_SPHERE_PROBES> sphere_buf_ = {
      "GooSphereProbeData"};
  StorageArrayBuffer<GooPlanarProbeData, GOO_MAX_PLANAR_PROBES> planar_buf_ = {
      "GooPlanarProbeData"};
  StorageArrayBuffer<GooVolumeProbeData, GOO_MAX_VOLUME_PROBES> volume_buf_ = {
      "GooVolumeProbeData"};
  StorageArrayBuffer<GooSHL2, GOO_MAX_VOLUME_SH_CELLS> volume_sh_buf_ = {"GooVolumeSHL2"};

  Texture env_probe_tx_ = {"goo.env_probe_tx"};
  Texture planar_tx_ = {"goo.planar_tx"};
  Texture capture_cube_tx_ = {"goo.capture_cube_tx"};
  Texture capture_depth_tx_ = {"goo.capture_depth_tx"};
  Texture planar_depth_tx_ = {"goo.planar_depth_tx"};

  Texture dummy_env_tx_ = {"goo.dummy_env_tx"};
  Texture dummy_planar_tx_ = {"goo.dummy_planar_tx"};

  gpu::Texture *env_probe_ptr_ = nullptr;
  gpu::Texture *planar_ptr_ = nullptr;

  Framebuffer capture_fb_ = {"goo.probe_capture_fb"};
  Framebuffer planar_fb_ = {"goo.probe_planar_fb"};
  View capture_view_ = {"goo.probe_capture_view"};

  PassSimple env_remap_ps_ = {"Goo.EnvRemap"};
  PassSimple env_convolve_ps_ = {"Goo.EnvConvolve"};
  PassSimple cube_to_sh_ps_ = {"Goo.CubeToSH"};
  PassSimple planar_downsample_ps_ = {"Goo.PlanarDownsample"};

  /* Pointer-bound uniforms for static PassSimples. */
  gpu::Texture *unf_src_tx_ = nullptr;
  gpu::Texture *unf_dst_img_ = nullptr;
  int unf_dst_layer_ = 0;
  int unf_src_res_ = GOO_ENV_OCT_RES;
  int unf_dst_res_ = GOO_ENV_OCT_RES;
  int unf_dst_index_ = 0;
  int unf_grid_dim_ = 32;
  float unf_indirect_clamp_ = 0.0f;
  float unf_target_roughness_ = 0.0f;
  int2 unf_planar_src_ext_ = int2(1);
  int2 unf_planar_dst_ext_ = int2(1);
  int unf_planar_layer_ = 0;
  int3 unf_remap_dispatch_ = int3(1);
  int3 unf_convolve_dispatch_ = int3(1);
  int3 unf_planar_dispatch_ = int3(1);

  Vector<SyncedSphereProbe> spheres_;
  Vector<SyncedPlanarProbe> planars_;
  Vector<SyncedVolumeProbe> volumes_;

  int env_oct_res_ = GOO_ENV_OCT_RES;
  float indirect_intensity_ = 1.0f;
  float indirect_clamp_ = 0.0f;
  bool has_world_ibl_ = false;
  bool static_probes_captured_ = false;
  bool probes_dirty_ = true;
  int prev_probe_hash_ = 0;
  int prev_ssr_flags_ = 0;
  float4 prev_ssr_params_ = float4(0.0f);
  bool ssr_changed_ = false;

  void ensure_static_textures(int env_res, int env_layers);
  void init_compute_passes();
  void dispatch_cubemap_to_oct(Manager &manager, gpu::Texture *cube_tx, int dst_layer);
  void dispatch_cubemap_to_sh(Manager &manager, gpu::Texture *cube_tx, int dst_index, int grid_dim);

 public:
  void begin_sync(const Scene *scene,
                  const LookdevParameters &lookdev_params,
                  bool has_world_shader,
                  bool use_sky);
  void probe_sync(const Object *ob);
  void end_sync();

  /** Capture world cubemap/SH, sphere reflection probes, and irradiance volume probes. */
  void capture_world_and_static_probes(Manager &manager,
                                       PassSimple *world_capture_pass,
                                       PassMain &prepass_ps,
                                       PassMain &surface_ps,
                                       PassMain &blend_ps,
                                       bool has_blend_draws,
                                       bool has_world_shader,
                                       bool use_sky,
                                       float4 world_clear_color,
                                       bool force_recapture);

  /** Capture view-dependent planar reflection probes for the active camera view and sample. */
  void capture_planar_probes(Manager &manager,
                             const View &camera_view,
                             int2 render_extent,
                             PassSimple *world_capture_pass,
                             PassMain &prepass_ps,
                             PassMain &surface_ps,
                             PassMain &blend_ps,
                             bool has_blend_draws,
                             bool has_world_shader,
                             bool use_sky);

  void mark_dirty()
  {
    probes_dirty_ = true;
  }

  void set_sss_active(bool active)
  {
    probe_buf_.sss_params.z = active ? 1.0f : 0.0f;
    probe_buf_.push_update();
  }

  template<typename PassType> void bind_ubo(PassType &pass)
  {
    pass.bind_ubo(GOO_PROBE_UBO_SLOT, probe_buf_);
  }

  template<typename PassType> void bind_resources(PassType &pass)
  {
    const GPUSamplerState mip_sampler{GPU_SAMPLER_FILTERING_LINEAR | GPU_SAMPLER_FILTERING_MIPMAP};
    pass.bind_ubo(GOO_PROBE_UBO_SLOT, probe_buf_);
    pass.bind_ssbo(GOO_SPHERE_PROBE_BUF_SLOT, sphere_buf_);
    pass.bind_ssbo(GOO_PLANAR_PROBE_BUF_SLOT, planar_buf_);
    pass.bind_ssbo(GOO_VOLUME_PROBE_BUF_SLOT, volume_buf_);
    pass.bind_ssbo(GOO_VOLUME_SH_BUF_SLOT, volume_sh_buf_);
    pass.bind_texture(GOO_ENV_PROBE_TEX_SLOT, &env_probe_ptr_, mip_sampler);
    pass.bind_texture(GOO_PLANAR_TEX_SLOT, &planar_ptr_, mip_sampler);
  }

  template<typename PassType> void bind_dummy_resources(PassType &pass)
  {
    const GPUSamplerState lin_sampler{GPU_SAMPLER_FILTERING_LINEAR | GPU_SAMPLER_FILTERING_MIPMAP};
    pass.bind_ubo(GOO_PROBE_UBO_SLOT, probe_buf_);
    pass.bind_ssbo(GOO_SPHERE_PROBE_BUF_SLOT, sphere_buf_);
    pass.bind_ssbo(GOO_PLANAR_PROBE_BUF_SLOT, planar_buf_);
    pass.bind_ssbo(GOO_VOLUME_PROBE_BUF_SLOT, volume_buf_);
    pass.bind_ssbo(GOO_VOLUME_SH_BUF_SLOT, volume_sh_buf_);
    pass.bind_texture(GOO_ENV_PROBE_TEX_SLOT, dummy_env_tx_, lin_sampler);
    pass.bind_texture(GOO_PLANAR_TEX_SLOT, dummy_planar_tx_, lin_sampler);
  }

  /**
   * Bind resources for world background pass (`goo_sample_env_probe` may be read when blurred LookDev
   * background is active, while `planar` stays bound to dummy).
   */
  template<typename PassType> void bind_world_resources(PassType &pass)
  {
    const GPUSamplerState mip_sampler{GPU_SAMPLER_FILTERING_LINEAR | GPU_SAMPLER_FILTERING_MIPMAP};
    pass.bind_ubo(GOO_PROBE_UBO_SLOT, probe_buf_);
    pass.bind_ssbo(GOO_SPHERE_PROBE_BUF_SLOT, sphere_buf_);
    pass.bind_ssbo(GOO_PLANAR_PROBE_BUF_SLOT, planar_buf_);
    pass.bind_ssbo(GOO_VOLUME_PROBE_BUF_SLOT, volume_buf_);
    pass.bind_ssbo(GOO_VOLUME_SH_BUF_SLOT, volume_sh_buf_);
    pass.bind_texture(GOO_ENV_PROBE_TEX_SLOT, &env_probe_ptr_, mip_sampler);
    pass.bind_texture(GOO_PLANAR_TEX_SLOT, dummy_planar_tx_, mip_sampler);
  }

  gpu::UniformBuf *probe_buf()
  {
    return probe_buf_;
  }
  const GooProbeBlock &probe_block() const
  {
    return probe_buf_;
  }
  bool ssr_enabled() const
  {
    return (probe_buf_.ssr_flags & GOO_SSR_FLAG_ENABLED) != 0;
  }
  bool ssr_refraction_enabled() const
  {
    return (probe_buf_.ssr_flags & (GOO_SSR_FLAG_ENABLED | GOO_SSR_FLAG_REFRACTION)) ==
           (GOO_SSR_FLAG_ENABLED | GOO_SSR_FLAG_REFRACTION);
  }
  bool ssr_state_changed() const
  {
    return ssr_changed_;
  }
  bool has_probes() const
  {
    return !spheres_.is_empty() || !planars_.is_empty() || !volumes_.is_empty();
  }
};

}  // namespace blender::goo
