/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/**
 * Goo closure evaluation for Blender 5.x node GLSL.
 *
 * In 5.x, BSDF nodes call `closure_eval(...)` for its side effect and return `Closure(0)`; mixing is
 * done by codegen through closure weights. So, like EEVEE-Next, Goo accumulates into globals:
 *   - `closure_eval()` lights each closure immediately (legacy EEVEE style "forward" evaluation),
 *   - `closure_to_rgba()` (the Shader-to-RGB node, Goo's core NPR tool) returns what has been
 *     accumulated so far and resets the accumulators,
 *   - the surface shader outputs whatever is accumulated after `nodetree_surface()`.
 *
 * Lighting here is a stand-in (Lambert + Blinn-Phong over a small light list and a flat world
 * ambient). It will be replaced by Goo 4.4's `lights_lib` / shadow / probe code in phase 2; the
 * closure plumbing above is the part this file exists to prove.
 */

#pragma once

#include "infos/goo_material_infos.hh"

SHADER_LIBRARY_CREATE_INFO(goo_lighting)
SHADER_LIBRARY_CREATE_INFO(UtilityTexture)
SHADER_LIBRARY_CREATE_INFO(draw_view)
SHADER_LIBRARY_CREATE_INFO(draw_modelmat)
SHADER_LIBRARY_CREATE_INFO(draw_object_infos)

#include "draw_model_lib.glsl"
#include "draw_object_infos_lib.glsl"
#include "draw_view_lib.glsl"
#include "eevee_bxdf_lut_lib.bsl.hh"
#include "eevee_utility_tx.bsl.hh"
#include "gpu_shader_codegen_lib.glsl"
#include "gpu_shader_math_base_lib.glsl"
#include "gpu_shader_math_safe_lib.glsl"
#include "gpu_shader_math_vector_reduce_lib.glsl"
#include "gpu_shader_math_vector_safe_lib.glsl"
#include "gpu_shader_utildefines_lib.glsl"

#define Closure float

/* -------------------------------------------------------------------- */
/** \name Engine hooks expected by node GLSL
 * \{ */

ObjectMatrices object_matrices_get()
{
  ObjectMatrices m;
#ifdef MAT_GEOM_WORLD
  m.model = float4x4(1.0f);
  m.model_inverse = float4x4(1.0f);
#else
  m.model = drw_modelmat();
  m.model_inverse = drw_modelinv();
#endif
  return m;
}

ObjectInfos object_infos_get()
{
#ifdef MAT_GEOM_WORLD
  ObjectInfos info;
  info.orco_add = float3(0.0f);
  info.orco_mul = float3(1.0f);
  info.ob_color = float4(1.0f);
  info.ob_index = 0.0f;
  info.ob_random = 0.0f;
  info.object_attrs_offset = 0u;
  info.object_attrs_len = 0u;
  return info;
#else
  return drw_object_infos();
#endif
}

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

float ambient_occlusion_eval(float3 /*normal*/,
                             float /*max_distance*/,
                             const float /*inverted*/,
                             const float /*sample_count*/)
{
  /* TODO(goo-port): Goo 4.4 GTAO. */
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

/* -------------------------------------------------------------------- */
/** \name BRDF / BSDF LUT & Fresnel helpers
 * \{ */

float f0_from_ior(float eta)
{
  return (eta - 1.0f) / (eta + 1.0f);
}

float F0_from_ior(float eta)
{
  return square(f0_from_ior(eta));
}

float F0_from_f0(float f0)
{
  return square(f0);
}

float3 F_brdf_single_scatter(float3 f0, float3 f90, eevee::lut::GGXBrdfData lut)
{
  return f0 * lut.scale + f90 * lut.bias;
}

float3 F_brdf_single_scatter(float3 f0, float3 f90, eevee::lut::GGXBsdfData lut)
{
  return f0 * lut.scale + f90 * lut.bias;
}

float3 F_brdf_multi_scatter(float3 f0, float3 f90, eevee::lut::GGXBrdfData lut)
{
  float3 FssEss = F_brdf_single_scatter(f0, f90, lut);
  float Ess = lut.scale + lut.bias;
  float Ems = 1.0f - Ess;
  float3 Favg = f0 + (f90 - f0) / 21.0f;
  return FssEss / (1.0f - Ems * Favg);
}

float3 F_brdf_multi_scatter(float3 f0, float3 f90, eevee::lut::GGXBsdfData lut)
{
  float3 FssEss = F_brdf_single_scatter(f0, f90, lut);
  float Ess = lut.scale + lut.bias;
  float Ems = 1.0f - Ess;
  float3 Favg = f0 + (f90 - f0) / 21.0f;
  return FssEss / (1.0f - Ems * Favg);
}

void brdf_f82_tint_lut(float3 F0,
                       float3 F82,
                       float cos_theta,
                       float roughness,
                       bool do_multiscatter,
                       float3 &reflectance)
{
  [[resource_table]] const UtilityTexture util_tx = resource_table_get(UtilityTexture);
  eevee::lut::GGXBrdfData lut = eevee::lut::GGXBrdfData::sample_utility_tx(
      util_tx, cos_theta, roughness);

  reflectance = do_multiscatter ? F_brdf_multi_scatter(F0, float3(1.0f), lut) :
                                  F_brdf_single_scatter(F0, float3(1.0f), lut);

  constexpr float f = 6.0f / 7.0f;
  constexpr float f5 = (f * f) * (f * f) * f;
  constexpr float f6 = (f * f) * (f * f) * (f * f);
  float3 F_schlick = mix(F0, float3(1.0f), f5);
  float3 b = F_schlick * (7.0f / f6) * (1.0f - F82);
  reflectance -= b * lut.metal_bias;
}

void bsdf_lut(float3 F0,
              float3 F90,
              float3 transmission_tint,
              float cos_theta,
              float roughness,
              float ior,
              bool do_multiscatter,
              float3 &reflectance,
              float3 &transmittance)
{
  [[resource_table]] const UtilityTexture util_tx = resource_table_get(UtilityTexture);
  if (ior == 1.0f) {
    reflectance = float3(0.0f);
    transmittance = transmission_tint;
    return;
  }

  eevee::lut::GGXBsdfData lut;
  const float f0 = f0_from_ior(ior);

  if (ior > 1.0f) {
    if (all(equal(F90, float3(1.0f)))) {
      F90 = float3(saturate(2.33f / 0.33f * f0));
    }

    eevee::lut::GGXBrdfData brdf_lut = eevee::lut::GGXBrdfData::sample_utility_tx(
        util_tx, cos_theta, roughness);
    eevee::lut::GGXBtdfGt1Data btdf_lut = eevee::lut::GGXBtdfGt1Data::sample_utility_tx(
        util_tx, cos_theta, roughness, f0);

    lut.scale = brdf_lut.scale;
    lut.bias = brdf_lut.bias;
    lut.transmission_factor = btdf_lut.transmission_factor;
  }
  else {
    lut = eevee::lut::GGXBsdfData::sample_utility_tx(util_tx, cos_theta, roughness, ior);
  }

  reflectance = F_brdf_single_scatter(F0, F90, lut);
  transmittance = (float3(1.0f) - F0) * lut.transmission_factor * transmission_tint;

  if (do_multiscatter) {
    const float real_F0 = F0_from_f0(f0);
    const float Ess = real_F0 * lut.scale + lut.bias + (1.0f - real_F0) * lut.transmission_factor;
    const float Ems = 1.0f - Ess;
    const float3 Favg = all(equal(transmission_tint, float3(0.0f))) ? F0 + (F90 - F0) / 21.0f :
                                                                      transmission_tint;
    float3 scale = 1.0f / (1.0f - Ems * Favg);
    reflectance *= scale;
    transmittance *= scale;
  }
}

float2 bsdf_lut(float cos_theta, float roughness, float ior, bool do_multiscatter)
{
  float F0 = F0_from_ior(ior);
  float3 color = float3(1.0f);
  float3 reflectance, transmittance;
  bsdf_lut(float3(F0),
           color,
           color,
           cos_theta,
           roughness,
           ior,
           do_multiscatter,
           reflectance,
           transmittance);
  return float2(reflectance.r, transmittance.r);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Coordinates & fragment displacement
 * \{ */

float3 coordinate_camera(float3 P)
{
#ifdef MAT_GEOM_WORLD
  float3 vP = drw_normal_world_to_view(P);
#else
  float3 vP = drw_point_world_to_view(P);
#endif
  vP.z = -vP.z;
  return vP;
}

float3 coordinate_screen(float3 P)
{
  float3 window = float3(0.0f);
#ifdef MAT_GEOM_WORLD
  window.xy = drw_point_view_to_screen(interp.P).xy;
#else
  window.xy = drw_point_world_to_screen(P).xy;
#endif
  return window;
}

float3 coordinate_reflect(float3 P, float3 N)
{
#ifdef MAT_GEOM_WORLD
  return N;
#else
  return -reflect(drw_world_incident_vector(P), N);
#endif
}

float3 coordinate_incoming(float3 P)
{
#ifdef MAT_GEOM_WORLD
  return -P;
#else
  return drw_world_incident_vector(P);
#endif
}

#ifdef MAT_DISPLACEMENT_BUMP
float3 nodetree_displacement();

float3 displacement_bump()
{
  constexpr float bump_filter_width = 0.1f;

  float2 dHd;
  dF_branch(dot(nodetree_displacement(), g_data.N + dF_impl(g_data.N)), bump_filter_width, dHd);

  float3 dPdx = gpu_dfdx(g_data.P) * derivative_scale_get();
  float3 dPdy = gpu_dfdy(g_data.P) * derivative_scale_get();

  float3 Rx = cross(dPdy, g_data.N);
  float3 Ry = cross(g_data.N, dPdx);

  float det = dot(dPdx, Rx);
  float3 surfgrad = dHd.x * Rx + dHd.y * Ry;

  float facing = FrontFacing ? 1.0f : -1.0f;
  return normalize(bump_filter_width * abs(det) * g_data.N - facing * sign(det) * surfgrad);
}
#endif

void fragment_displacement()
{
#ifdef MAT_DISPLACEMENT_BUMP
  g_data.N = displacement_bump();
#endif
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Attributes & scene uniforms
 * \{ */

float attr_load_temperature_post(float attr)
{
  return attr;
}

float4 attr_load_color_post(float4 attr)
{
  return attr;
}

float4 attr_load_uniform(float4 /*attr*/, const uint attr_hash)
{
  const auto &attrs_buf = buffer_get(draw_object_attributes, drw_attrs);

  ObjectInfos infos = object_infos_get();
  uint index = infos.object_attrs_offset;
  for (uint i = 0; i < infos.object_attrs_len; i++, index++) {
    ObjectAttribute attr = attrs_buf[index];
    if (attr.hash_code == attr_hash) {
      return float4(attr.data_x, attr.data_y, attr.data_z, attr.data_w);
    }
  }
  return float4(0.0f);
}

void scene_time_uniforms(float &seconds, float &frame)
{
  seconds = 0.0f;
  frame = 0.0f;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Accumulators
 * \{ */

float3 g_goo_radiance;
float3 g_goo_transmittance;
float g_goo_holdout;

void closure_weights_reset(float /*closure_rand*/)
{
  g_goo_radiance = float3(0.0f);
  g_goo_transmittance = float3(0.0f);
  g_goo_holdout = 0.0f;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Lighting (stand-in)
 * \{ */

/* Light layout (2 x float4 per light):
 *   [0] = xyz: world position (point/spot/area) or direction towards light (sun), w: type
 *   [1] = rgb: color * intensity, a: radius */
#define GOO_LIGHT_SUN 0.0f

float3 goo_light_vector(int i, float3 P, float &attenuation)
{
  float4 a = goo_lights[i * 2 + 0];
  if (a.w == GOO_LIGHT_SUN) {
    attenuation = 1.0f;
    return normalize(a.xyz);
  }
  float3 L = a.xyz - P;
  float dist_sqr = max(1e-4f, dot(L, L));
  attenuation = 1.0f / dist_sqr;
  return L * inversesqrt(dist_sqr);
}

float3 goo_diffuse_light(float3 N, float3 P)
{
  float3 light = goo_world_color.rgb;
  for (int i = 0; i < goo_light_count; i++) {
    float att;
    float3 L = goo_light_vector(i, P, att);
    light += goo_lights[i * 2 + 1].rgb * att * max(0.0f, dot(N, L));
  }
  return light;
}

float3 goo_specular_light(float3 N, float3 P, float roughness)
{
  float3 V = drw_world_incident_vector(P);
  float shininess = mix(512.0f, 2.0f, clamp(roughness, 0.0f, 1.0f));
  float3 light = goo_world_color.rgb * (1.0f - roughness) * 0.5f;
  for (int i = 0; i < goo_light_count; i++) {
    float att;
    float3 L = goo_light_vector(i, P, att);
    float3 H = normalize(L + V);
    float spec = pow(max(0.0f, dot(N, H)), shininess) * (shininess + 8.0f) / 25.13f;
    light += goo_lights[i * 2 + 1].rgb * att * spec * max(0.0f, dot(N, L));
  }
  return light;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Closure evaluation
 * \{ */

Closure closure_eval(ClosureDiffuse diffuse)
{
  g_goo_radiance += diffuse.color * diffuse.weight * goo_diffuse_light(diffuse.N, g_data.P);
  return Closure(0);
}

Closure closure_eval(ClosureSubsurface diffuse)
{
  /* TODO(goo-port): Goo 4.4 SSS. Approximated as diffuse. */
  g_goo_radiance += diffuse.color * diffuse.weight * goo_diffuse_light(diffuse.N, g_data.P);
  return Closure(0);
}

Closure closure_eval(ClosureTranslucent translucent)
{
  g_goo_radiance += translucent.color * translucent.weight *
                    goo_diffuse_light(-translucent.N, g_data.P);
  return Closure(0);
}

Closure closure_eval(ClosureReflection reflection)
{
  g_goo_radiance += reflection.color * reflection.weight *
                    goo_specular_light(reflection.N, g_data.P, reflection.roughness);
  return Closure(0);
}

Closure closure_eval(ClosureRefraction refraction)
{
  g_goo_radiance += refraction.color * refraction.weight * goo_world_color.rgb;
  return Closure(0);
}

Closure closure_eval(ClosureThinRefraction refraction)
{
  g_goo_radiance += refraction.color * refraction.weight * goo_world_color.rgb;
  return Closure(0);
}

Closure closure_eval(ClosureEmission emission)
{
  g_goo_radiance += emission.emission * emission.weight;
  return Closure(0);
}

Closure closure_eval(ClosureTransparency transparency)
{
  g_goo_transmittance += transparency.transmittance * transparency.weight;
  g_goo_holdout += transparency.holdout * transparency.weight;
  return Closure(0);
}

Closure closure_eval(ClosureVolumeScatter /*volume_scatter*/)
{
  return Closure(0);
}

Closure closure_eval(ClosureVolumeAbsorption /*volume_absorption*/)
{
  return Closure(0);
}

Closure closure_eval(ClosureHair /*hair*/)
{
  return Closure(0);
}

/* Glass BSDF. */
Closure closure_eval(ClosureReflection reflection, ClosureRefraction refraction)
{
  closure_eval(reflection);
  closure_eval(refraction);
  return Closure(0);
}

/* Dielectric BSDF. */
Closure closure_eval(ClosureDiffuse diffuse, ClosureReflection reflection)
{
  closure_eval(diffuse);
  closure_eval(reflection);
  return Closure(0);
}

/* Coat BSDF. */
Closure closure_eval(ClosureReflection reflection, ClosureReflection coat)
{
  closure_eval(reflection);
  closure_eval(coat);
  return Closure(0);
}

/* Volume BSDF. */
Closure closure_eval(ClosureVolumeScatter volume_scatter,
                     ClosureVolumeAbsorption volume_absorption,
                     ClosureEmission emission)
{
  closure_eval(volume_scatter);
  closure_eval(volume_absorption);
  closure_eval(emission);
  return Closure(0);
}

/* Specular BSDF. */
Closure closure_eval(ClosureDiffuse diffuse, ClosureReflection reflection, ClosureReflection coat)
{
  closure_eval(diffuse);
  closure_eval(reflection);
  closure_eval(coat);
  return Closure(0);
}

/* Principled BSDF. */
Closure closure_eval(ClosureDiffuse diffuse,
                     ClosureReflection reflection,
                     ClosureReflection coat,
                     ClosureRefraction refraction)
{
  closure_eval(diffuse);
  closure_eval(reflection);
  closure_eval(coat);
  closure_eval(refraction);
  return Closure(0);
}

/* NOP: accumulation happens in closure_eval (weights are pre-applied by codegen). */
Closure closure_add(Closure /*cl1*/, Closure /*cl2*/)
{
  return Closure(0);
}

Closure closure_mix(Closure /*cl1*/, Closure /*cl2*/, float /*fac*/)
{
  return Closure(0);
}

/* Shader to RGB: Goo's core NPR tool. Returns the lit result of the closures evaluated so far,
 * then resets the accumulators so the converted color can feed further shading. */
float4 closure_to_rgba(Closure /*cl*/)
{
  float4 result = float4(g_goo_radiance,
                         saturate(1.0f - reduce_add(g_goo_transmittance) * (1.0f / 3.0f)));
  closure_weights_reset(0.0f);
  return result;
}

/** \} */
