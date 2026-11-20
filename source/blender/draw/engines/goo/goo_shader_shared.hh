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
};

struct [[host_shared]] GooLightBlock {
  struct GooLightData lights[GOO_MAX_LIGHTS];
  float4 world_ambient;
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

#ifndef GPU_SHADER
}  // namespace blender::goo
#endif
