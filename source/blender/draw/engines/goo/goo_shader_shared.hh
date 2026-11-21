/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Host/device shared structures for Goo lighting and shadow maps.
 */

#pragma once

#include "GPU_shader_shared_utils.hh"

#include "goo_defines.hh"

#ifndef GPU_SHADER
namespace blender::goo {
#endif

struct [[host_shared]] GooLightData {
  /** xyz: world position (point/spot/area) or normalized dir towards sun; w: float(type). */
  float4 position_type;
  /** rgb: irradiance (sun) or radiant intensity (point/spot/area); a: radius. */
  float4 color_energy;
  /** xyz: normalized forward direction (-z_axis); w: cos(spotsize * 0.5). */
  float4 direction_spot_cos;
  /** x: spotblend_scale; y: shadow_slice_start (-1 if none); z: shadow_slice_count; w: diff_fac. */
  float4 spot_shadow;
  /** x: spec_fac; y: shadow_far; z: shadow_near; w: unused. */
  float4 extra;
  /** Light group bitmask (128 bits, bit 0 of `.w` is default group 0). */
  int4 light_group_bits;
  /** x: contact_dist (<=0 disabled); y: contact_bias; z: contact_thickness; w: unused. */
  float4 contact_shadow;
};

struct [[host_shared]] GooLightBlock {
  struct GooLightData lights[GOO_MAX_LIGHTS];
  float4 world_ambient;
  /** x: gtao_distance; y: gtao_factor; z: gtao_quality; w: float(gtao_flags). */
  float4 gtao_params;
  /** x: float(sample_index); y: temporal_rand_u; z: temporal_rand_v; w: background_alpha (0 if film_transparent). */
  float4 sample_params;
  int light_count;
  int shadow_slice_count;
  int _pad0;
  int _pad1;
};

struct [[host_shared]] GooShadowSlice {
  float4x4 vp;
  /** x: normal_bias; y: depth_bias; z: unused; w: texel_size. */
  float4 params;
};

struct [[host_shared]] GooShadowBlock {
  struct GooShadowSlice slices[GOO_MAX_SHADOW_SLICES];
};

struct [[host_shared]] GooPassBlock {
  int normal_id;
  int position_id;
  int diff_dir_id;
  int diff_col_id;

  int gloss_dir_id;
  int gloss_col_id;
  int emit_id;
  int env_id;

  int shadow_id;
  int ao_id;
  int aov_color_id;
  int aov_color_len;

  int depth_id;
  int mist_id;
  int aov_value_id;
  int aov_value_len;

  int color_layer_count;
  int value_layer_count;
  int crypto_flags;
  int crypto_levels;

  /** x: mist_scale, y: mist_bias, z: mist_exponent, w: env_fallback_enabled (>0.5 if no world shader). */
  float4 mist_params;
  float4 env_fallback;
  uint4 aov_hash[GOO_AOV_MAX / 4];
};

struct [[host_shared]] GooSHL2 {
  float4 sh[9];
};

struct [[host_shared]] GooSphereProbeData {
  float4x4 world_to_influence;
  float4x4 world_to_parallax;
  /** xyz: world location; w: falloff (0.001..1.0). */
  float4 location_falloff;
  /** x: layer; y: influence_shape; z: parallax_shape; w: custom_parallax (!= 0). */
  int4 params;
  /** x: intensity; y: parallax_distance; z: distinf; w: volume_rank. */
  float4 extra;
};

struct [[host_shared]] GooPlanarProbeData {
  float4x4 world_to_plane;
  float4x4 ref_vp;
  /** xyz: plane world normal; w: falloff. */
  float4 normal_falloff;
  /** x: float(layer_id); y: parallax_distance; z: intensity; w: unused. */
  float4 params;
};

struct [[host_shared]] GooVolumeProbeData {
  float4x4 world_to_volume;
  /** xyz: resolution (nx, ny, nz); w: cell offset into `goo_volume_sh_buf` (>= 1). */
  int4 res_offset;
  /** x: falloff; y: intensity; z: normal_bias; w: view_bias. */
  float4 params;
};

struct [[host_shared]] GooProbeBlock {
  float4 capture_clip_plane;
  /** x: ssr_quality; y: ssr_max_roughness; z: ssr_thickness; w: ssr_border_fade. */
  float4 ssr_params;
  /** x: ssr_firefly_fac; y: indirect_light_intensity; z: clamp_surface_indirect; w: float(GOO_ENV_MIPS). */
  float4 indirect_params;
  /** x: bg_opacity; y: bg_blur_lod; z: is_lookdev_bg; w: has_world_ibl. */
  float4 lookdev_params;
  int sphere_probe_count;
  int planar_probe_count;
  int volume_probe_count;
  int capture_flags;
  int ssr_flags;
  int env_oct_res;
  int _pad0;
  int _pad1;
};

#ifndef GPU_SHADER
}  // namespace blender::goo
#endif
