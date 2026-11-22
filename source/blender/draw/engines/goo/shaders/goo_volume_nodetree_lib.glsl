/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Goo closure & builtin stubs for Volume Froxel evaluation shaders.
 * Avoids pulling surface/pass/SSR create-infos so volume passes keep slot 2 for goo_vol_buf.
 */

#pragma once

#include "infos/goo_effects_infos.hh"

SHADER_LIBRARY_CREATE_INFO(goo_volume_ubo)
SHADER_LIBRARY_CREATE_INFO(UtilityTexture)
SHADER_LIBRARY_CREATE_INFO(draw_view)

#include "draw_shader_shared.hh"
#include "draw_view_lib.glsl"
#include "eevee_utility_tx.bsl.hh"
#include "goo_defines.hh"
#include "gpu_shader_codegen_lib.glsl"
#include "gpu_shader_math_base_lib.glsl"
#include "gpu_shader_math_safe_lib.glsl"
#include "gpu_shader_math_vector_reduce_lib.glsl"
#include "gpu_shader_math_vector_safe_lib.glsl"
#include "gpu_shader_utildefines_lib.glsl"

#define Closure float

ViewMatrices view_matrices_get()
{
  return drw_view();
}

float derivative_scale_get()
{
  return 1.0f;
}

float texture_lod_bias_get()
{
  return 0.0f;
}

float3 coordinate_camera(float3 P)
{
  float3 vP = drw_point_world_to_view(P);
  vP.z = -vP.z;
  return vP;
}

float3 coordinate_screen(float3 P)
{
  return float3(drw_point_world_to_screen(P).xy, 0.0f);
}

float3 coordinate_reflect(float3 P, float3 N)
{
  return -reflect(drw_world_incident_vector(P), N);
}

float3 coordinate_incoming(float3 P)
{
  return drw_world_incident_vector(P);
}

float attr_load_temperature_post(float attr)
{
  return attr;
}

float4 attr_load_color_post(float4 attr)
{
  return attr;
}

float4 attr_load_uniform(float4 attr, const uint /*attr_hash*/)
{
  return attr;
}

void scene_time_uniforms(float &seconds, float &frame)
{
  seconds = 0.0f;
  frame = 0.0f;
}

float ambient_occlusion_eval(float3 /*normal*/,
                             float /*max_distance*/,
                             const float /*inverted*/,
                             const float /*sample_count*/)
{
  return 1.0f;
}

void raycast_eval(float3 /*position*/,
                  float3 /*direction*/,
                  float /*length*/,
                  bool /*only_local*/,
                  bool &is_hit,
                  bool &is_self_hit,
                  float &hit_distance,
                  float3 &hit_position,
                  float3 &hit_normal)
{
  is_hit = false;
  is_self_hit = false;
  hit_distance = 0.0f;
  hit_position = float3(0.0f);
  hit_normal = float3(0.0f);
}

/* Volume closure accumulators. */
float3 g_goo_vol_scattering = float3(0.0f);
float3 g_goo_vol_absorption = float3(0.0f);
float3 g_goo_vol_emission = float3(0.0f);
float g_goo_vol_anisotropy = 0.0f;
float g_goo_vol_aniso_weight = 0.0f;

void closure_weights_reset(float /*closure_rand*/)
{
  g_goo_vol_scattering = float3(0.0f);
  g_goo_vol_absorption = float3(0.0f);
  g_goo_vol_emission = float3(0.0f);
  g_goo_vol_anisotropy = 0.0f;
  g_goo_vol_aniso_weight = 0.0f;
}

Closure closure_eval(ClosureVolumeScatter volume_scatter)
{
  float3 scat = max(volume_scatter.scattering, float3(0.0f)) * max(volume_scatter.weight, 0.0f);
  float w = max(scat.r, max(scat.g, scat.b));
  g_goo_vol_scattering += scat;
  g_goo_vol_anisotropy += clamp(volume_scatter.anisotropy, -0.99f, 0.99f) * w;
  g_goo_vol_aniso_weight += w;
  return Closure(0);
}

Closure closure_eval(ClosureVolumeAbsorption volume_absorption)
{
  g_goo_vol_absorption +=
      max(volume_absorption.absorption, float3(0.0f)) * max(volume_absorption.weight, 0.0f);
  return Closure(0);
}

Closure closure_eval(ClosureEmission emission)
{
  g_goo_vol_emission += max(emission.emission, float3(0.0f)) * max(emission.weight, 0.0f);
  return Closure(0);
}

Closure closure_eval(ClosureVolumeScatter volume_scatter,
                     ClosureVolumeAbsorption volume_absorption,
                     ClosureEmission emission)
{
  closure_eval(volume_scatter);
  closure_eval(volume_absorption);
  closure_eval(emission);
  return Closure(0);
}

Closure closure_eval(ClosureDiffuse /*diffuse*/)
{
  return Closure(0);
}
Closure closure_eval(ClosureSubsurface /*subsurface*/)
{
  return Closure(0);
}
Closure closure_eval(ClosureTranslucent /*translucent*/)
{
  return Closure(0);
}
Closure closure_eval(ClosureReflection /*reflection*/)
{
  return Closure(0);
}
Closure closure_eval(ClosureRefraction /*refraction*/)
{
  return Closure(0);
}
Closure closure_eval(ClosureThinRefraction /*refraction*/)
{
  return Closure(0);
}
Closure closure_eval(ClosureTransparency /*transparency*/)
{
  return Closure(0);
}
Closure closure_eval(ClosureHair /*hair*/)
{
  return Closure(0);
}
Closure closure_eval(ClosureReflection /*r*/, ClosureRefraction /*t*/)
{
  return Closure(0);
}
Closure closure_eval(ClosureDiffuse /*d*/, ClosureReflection /*r*/)
{
  return Closure(0);
}
Closure closure_eval(ClosureReflection /*r*/, ClosureReflection /*c*/)
{
  return Closure(0);
}
Closure closure_eval(ClosureDiffuse /*d*/, ClosureReflection /*r*/, ClosureReflection /*c*/)
{
  return Closure(0);
}
Closure closure_eval(
    ClosureDiffuse /*d*/, ClosureReflection /*r*/, ClosureReflection /*c*/, ClosureRefraction /*t*/)
{
  return Closure(0);
}
Closure closure_add(Closure /*c1*/, Closure /*c2*/)
{
  return Closure(0);
}
Closure closure_mix(Closure /*c1*/, Closure /*c2*/, float /*fac*/)
{
  return Closure(0);
}
float4 closure_to_rgba(Closure /*cl*/)
{
  return float4(0.0f);
}
