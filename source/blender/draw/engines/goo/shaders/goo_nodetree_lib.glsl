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

#include "infos/goo_effects_infos.hh"
#include "infos/goo_indirect_infos.hh"
#include "infos/goo_material_infos.hh"

SHADER_LIBRARY_CREATE_INFO(goo_lighting)
SHADER_LIBRARY_CREATE_INFO(goo_render_passes)
SHADER_LIBRARY_CREATE_INFO(goo_probe_ubo)
SHADER_LIBRARY_CREATE_INFO(goo_indirect_read)
SHADER_LIBRARY_CREATE_INFO(goo_ssr_write)
SHADER_LIBRARY_CREATE_INFO(goo_sss_write)
SHADER_LIBRARY_CREATE_INFO(UtilityTexture)
SHADER_LIBRARY_CREATE_INFO(draw_view)

#include "draw_shader_shared.hh"
#include "draw_view_lib.glsl"
#include "eevee_bxdf_lut_lib.bsl.hh"
#include "eevee_utility_tx.bsl.hh"
#include "goo_defines.hh"
#include "goo_gtao_lib.glsl"
#include "goo_probe_lib.glsl"
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

float ambient_occlusion_eval(float3 normal,
                             float max_distance,
                             const float inverted,
                             const float sample_count)
{
#ifdef MAT_GEOM_WORLD
  return 1.0f;
#else
  float3 vP = drw_point_world_to_view(g_data.P);
  int steps = clamp(int(sample_count), 2, 16);
  float4 horizons = goo_gtao_horizon_search(vP,
                                            gl_FragCoord.xy,
                                            goo_light_buf.sample_params.yz,
                                            goo_depth_tx,
                                            max(max_distance, 1e-4f),
                                            inverted,
                                            steps);
  float vis = 1.0f;
  float3 unused_bn = float3(0.0f);
  goo_gtao_integrate(horizons,
                     gl_FragCoord.xy,
                     goo_light_buf.sample_params.yz,
                     drw_world_incident_vector(g_data.P),
                     safe_normalize(normal),
                     g_data.Ng,
                     inverted,
                     false,
                     vis,
                     unused_bn);
  return saturate(vis);
#endif
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
/** \name Accumulators & Render Passes / AOVs
 * \{ */

float3 g_goo_radiance;
float3 g_goo_transmittance;
float g_holdout;
float3 g_goo_diff_light;
float3 g_goo_diff_color;
float3 g_goo_spec_light;
float3 g_goo_spec_color;
float3 g_goo_emission;
float g_goo_shadow_sum;
float g_goo_shadow_weight;
float g_goo_frag_depth;
bool g_goo_frag_depth_set;
float3 g_goo_ssr_normal;
float g_goo_ssr_roughness;
float3 g_goo_ssr_throughput;
float3 g_goo_ssr_fallback;
float g_goo_ssr_mask;
float g_goo_ssr_weight;
float3 g_goo_sss_irrad;
float3 g_goo_sss_albedo;
float3 g_goo_sss_radius;
float3 g_goo_sss_direct_radiance;
float g_goo_sss_weight;

void closure_weights_reset(float /*closure_rand*/)
{
  g_goo_radiance = float3(0.0f);
  g_goo_transmittance = float3(0.0f);
  g_holdout = 0.0f;
  g_goo_diff_light = float3(0.0f);
  g_goo_diff_color = float3(0.0f);
  g_goo_spec_light = float3(0.0f);
  g_goo_spec_color = float3(0.0f);
  g_goo_emission = float3(0.0f);
  g_goo_ssr_normal = float3(0.0f);
  g_goo_ssr_roughness = -1.0f;
  g_goo_ssr_throughput = float3(0.0f);
  g_goo_ssr_fallback = float3(0.0f);
  g_goo_ssr_mask = 0.0f;
  g_goo_ssr_weight = 0.0f;
  g_goo_sss_irrad = float3(0.0f);
  g_goo_sss_albedo = float3(0.0f);
  g_goo_sss_radius = float3(0.0f);
  g_goo_sss_direct_radiance = float3(0.0f);
  g_goo_sss_weight = 0.0f;
}

void goo_pass_accum_reset()
{
  g_goo_shadow_sum = 0.0f;
  g_goo_shadow_weight = 0.0f;
}

void output_aov(int2 texel, float4 color, float value, uint hash, float holdout, uint ob_flag)
{
#ifdef GOO_PASS_WRITE
  if ((goo_material_flags & GOO_MAT_FLAG_NO_PASS_WRITE) != 0 ||
      (goo_probe_buf.capture_flags & GOO_CAPTURE_NO_PASS_WRITE) != 0)
  {
    return;
  }
  uint total_len = uint(goo_pass_buf.aov_color_len + goo_pass_buf.aov_value_len);
  if (total_len == 0u) {
    return;
  }

  uint hash_index = 0u;
  for (; hash_index < uint(GOO_AOV_MAX) && hash_index < total_len; hash_index += 4u) {
    bool4 cmp_mask = equal(goo_pass_buf.aov_hash[hash_index >> 2u], uint4(hash));
    if (any(cmp_mask)) {
      hash_index += (cmp_mask[0] ? 0u : (cmp_mask[1] ? 1u : (cmp_mask[2] ? 2u : 3u)));
      break;
    }
  }

  if (hash_index < total_len) {
    if ((ob_flag & uint(OBJECT_HOLDOUT)) != 0u) {
      holdout = 1.0f;
    }
    float att = 1.0f - saturate(holdout);
    bool is_value = hash_index >= uint(goo_pass_buf.aov_color_len);
    if (is_value) {
      int layer = int(hash_index) - goo_pass_buf.aov_color_len;
      imageStoreFast(goo_rp_value_img, int3(texel, layer), float4(value * att));
    }
    else {
      int layer = goo_pass_buf.aov_color_id + int(hash_index);
      imageStoreFast(goo_rp_color_img, int3(texel, layer), color * att);
    }
  }
#endif
}

void goo_set_depth(float z_in)
{
  float view_near = drw_depth_screen_to_view(0.0f);
  float view_far = drw_depth_screen_to_view(1.0f);
  float z_clipped = max(min(-z_in, view_near), view_far);
  g_goo_frag_depth = drw_depth_view_to_screen(z_clipped);
  g_goo_frag_depth_set = true;
}

/* Hashed alpha threshold (Wyman & McGuire), matching Goo 4.4 `prepass_frag.glsl`. */
float goo_hash2(float2 a)
{
  return fract(1e4f * sin(17.0f * a.x + 0.1f * a.y) * (0.1f + abs(sin(13.0f * a.y + a.x))));
}

float goo_hash3(float3 a)
{
  return goo_hash2(float2(goo_hash2(a.xy), a.z));
}

float goo_hashed_alpha_threshold(float3 co)
{
  float max_deriv = max(max(length(gpu_dfdx(co)), length(gpu_dfdy(co))), 1e-6f);
  float pix_scale = 1.0f / max_deriv;

  float pix_scale_log = log2(pix_scale);
  float2 pix_scales = float2(exp2(floor(pix_scale_log)), exp2(ceil(pix_scale_log)));

  float2 alpha_n = float2(goo_hash3(floor(pix_scales.x * co)),
                          goo_hash3(floor(pix_scales.y * co)));

  float fac = fract(pix_scale_log);
  if (goo_light_buf.sample_params.x > 0.5f) {
    alpha_n = fract(alpha_n + goo_light_buf.sample_params.yz);
  }
  float x = mix(alpha_n.x, alpha_n.y, fac);

  float a = min(fac, 1.0f - fac);
  float one_a = 1.0f - a;
  float denom = 1.0f / max(2.0f * a * one_a, 1e-6f);
  float one_x = 1.0f - x;
  float3 cases = float3(
      (x * x) * denom, (x - 0.5f * a) / max(one_a, 1e-6f), 1.0f - (one_x * one_x * denom));

  float threshold = (x < one_a) ? ((x < a) ? cases.x : cases.y) : cases.z;
  return clamp(threshold, 1.0e-6f, 1.0f);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Lighting & Shadow Maps
 * \{ */

float goo_shadow_pcf(int slice, float3 P, float3 N, int shadow_id_mode)
{
  GooShadowSlice s = goo_shadow_buf.slices[slice];
  float3 P_biased = P + N * s.params.x;
  float4 clip = s.vp * float4(P_biased, 1.0f);
  if (clip.w <= 0.0f) {
    return 1.0f;
  }
  float3 ndc = clip.xyz / clip.w;
  float3 coord = ndc * 0.5f + 0.5f;
  if (coord.x < 0.0f || coord.x > 1.0f || coord.y < 0.0f || coord.y > 1.0f || coord.z < 0.0f ||
      coord.z > 1.0f)
  {
    return 1.0f;
  }

  float depth_bias = s.params.y;
  float texel = s.params.w;
  uint my_id = (shadow_id_mode != 0) ? goo_resource_id_get() : 0u;
  int2 id_max = int2(0);
  if (shadow_id_mode != 0) {
    id_max = max(textureSize(goo_shadow_id_tx, 0).xy - int2(1), int2(0));
  }

  float sum = 0.0f;
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      float2 uv = clamp(
          coord.xy + float2(float(dx), float(dy)) * texel, float2(0.0f), float2(1.0f));
      float stored_depth = textureLod(goo_shadow_tx, float3(uv, float(slice)), 0.0f).r;
      bool lit = (coord.z - depth_bias <= stored_depth);
      if (!lit && shadow_id_mode != 0) {
        int2 px = clamp(int2(uv * float2(id_max + int2(1))), int2(0), id_max);
        uint caster_id = texelFetch(goo_shadow_id_tx, int3(px, slice), 0).r;
        if (shadow_id_mode == 1) {
          lit = (my_id != 0u) && (caster_id == my_id);
        }
        else {
          lit = (my_id == 0u) || (caster_id != my_id);
        }
      }
      sum += lit ? 1.0f : 0.0f;
    }
  }
  return sum * (1.0f / 9.0f);
}

float goo_light_shadow(GooLightData ld, float3 P, float3 N, int shadow_id_mode)
{
  if (ld.spot_shadow.y < -0.5f) {
    return 1.0f;
  }
  int shadow_start = int(ld.spot_shadow.y + 0.5f);
  int shadow_count = int(ld.spot_shadow.z + 0.5f);
  int light_type = int(ld.position_type.w + 0.5f);

  if (light_type == GOO_LIGHT_SUN) {
    for (int c = 0; c < shadow_count; c++) {
      GooShadowSlice s = goo_shadow_buf.slices[shadow_start + c];
      float4 clip = s.vp * float4(P + N * s.params.x, 1.0f);
      if (clip.w > 0.0f) {
        float3 coord = (clip.xyz / clip.w) * 0.5f + 0.5f;
        if (coord.x >= 0.02f && coord.x <= 0.98f && coord.y >= 0.02f && coord.y <= 0.98f &&
            coord.z >= 0.0f && coord.z <= 1.0f)
        {
          return goo_shadow_pcf(shadow_start + c, P, N, shadow_id_mode);
        }
      }
    }
    return 1.0f;
  }

  if (light_type == GOO_LIGHT_SPOT) {
    return goo_shadow_pcf(shadow_start, P, N, shadow_id_mode);
  }

  if (light_type == GOO_LIGHT_POINT && shadow_count == 6) {
    float3 d = P - ld.position_type.xyz;
    float3 ad = abs(d);
    int face = 0;
    if (ad.x >= ad.y && ad.x >= ad.z) {
      face = (d.x >= 0.0f) ? 0 : 1;
    }
    else if (ad.y >= ad.z) {
      face = (d.y >= 0.0f) ? 2 : 3;
    }
    else {
      face = (d.z >= 0.0f) ? 4 : 5;
    }
    return goo_shadow_pcf(shadow_start + face, P, N, shadow_id_mode);
  }

  return 1.0f;
}

bool goo_light_group_match(int4 light_bits, int4 group_bits)
{
  return ((light_bits.x & group_bits.x) | (light_bits.y & group_bits.y) |
          (light_bits.z & group_bits.z) | (light_bits.w & group_bits.w)) != 0;
}

void goo_eval_light_unshadowed(int i, float3 P, float &attenuation, float3 &L)
{
  GooLightData ld = goo_light_buf.lights[i];
  int light_type = int(ld.position_type.w + 0.5f);
  if (light_type == GOO_LIGHT_SUN) {
    L = normalize(ld.position_type.xyz);
    attenuation = 1.0f;
  }
  else {
    float3 to_light = ld.position_type.xyz - P;
    float dist_sqr = dot(to_light, to_light);
    float radius = max(0.05f, ld.color_energy.a);
    float denom = max(dist_sqr, radius * radius);
    L = to_light * inversesqrt(max(1e-6f, dist_sqr));
    attenuation = 1.0f / denom;
    if (light_type == GOO_LIGHT_SPOT) {
      float cos_angle = dot(-L, ld.direction_spot_cos.xyz);
      float spot_att = clamp((cos_angle - ld.direction_spot_cos.w) * ld.spot_shadow.x, 0.0f, 1.0f);
      attenuation *= spot_att * spot_att;
    }
    else if (light_type == GOO_LIGHT_AREA) {
      attenuation *= max(0.0f, dot(-L, ld.direction_spot_cos.xyz));
    }
  }
}

float goo_contact_shadow(GooLightData ld, float3 P, float shadow_vis)
{
#ifdef MAT_GEOM_WORLD
  return 1.0f;
#else
  float contact_dist = ld.contact_shadow.x;
  if (contact_dist <= 0.0f || shadow_vis <= 0.001f) {
    return 1.0f;
  }

  float3 ray_world;
  int light_type = int(ld.position_type.w + 0.5f);
  if (light_type == GOO_LIGHT_SUN) {
    ray_world = safe_normalize(ld.position_type.xyz) * contact_dist;
  }
  else {
    float3 to_light = ld.position_type.xyz - P;
    float dist_to_light = length(to_light);
    if (dist_to_light <= 1e-5f) {
      return 1.0f;
    }
    ray_world = (to_light / dist_to_light) * min(contact_dist, dist_to_light);
  }

  float3 vP = drw_point_world_to_view(P);
  float3 vNg = safe_normalize(drw_normal_world_to_view(g_data.Ng));
  float3 vStart = vP + vNg * ld.contact_shadow.y;
  float3 vEnd = vStart + drw_normal_world_to_view(ray_world);

  float near_z = drw_depth_screen_to_view(0.0f);
  if (vStart.z > near_z) {
    return 1.0f;
  }
  if (vEnd.z > near_z) {
    float t_clip = saturate(safe_divide(near_z - vStart.z, vEnd.z - vStart.z));
    vEnd = mix(vStart, vEnd, t_clip);
  }

  float jitter = clamp(goo_gtao_noise(gl_FragCoord.xy, goo_light_buf.sample_params.yz).x, 0.15f, 1.0f);
  float thickness = max(ld.contact_shadow.z, 1e-4f);

  for (int step_idx = 0; step_idx < 16; step_idx++) {
    float t = (float(step_idx) + jitter) / 16.0f;
    float3 vSample = mix(vStart, vEnd, t);
    float3 ss = drw_point_view_to_screen(vSample);
    if (ss.x < 0.0f || ss.x > 1.0f || ss.y < 0.0f || ss.y > 1.0f || ss.z <= 0.0f || ss.z >= 1.0f) {
      break;
    }

    float depth_sample = textureLod(goo_depth_tx, ss.xy, 0.0f).r;
    if (depth_sample >= 1.0f) {
      continue;
    }

    float z_scene = drw_depth_screen_to_view(depth_sample);
    float depth_delta = z_scene - vSample.z;
    if (depth_delta > 0.002f && depth_delta < thickness) {
      return smoothstep(0.85f, 1.0f, t);
    }
  }
  return 1.0f;
#endif
}

float goo_gtao_eval_vis_bent(
    float3 N, float3 albedo, bool respect_enable_flag, float3 &out_bent_n)
{
  out_bent_n = N;
#ifdef MAT_GEOM_WORLD
  return 1.0f;
#else
  int flags = int(goo_light_buf.gtao_params.w + 0.5f);
  if (goo_light_buf.gtao_params.x <= 0.0f ||
      (respect_enable_flag && (flags & GOO_GTAO_FLAG_ENABLED) == 0))
  {
    return 1.0f;
  }

  int2 max_texel = max(textureSize(goo_horizon_tx, 0) - int2(1), int2(0));
  int2 texel = clamp(int2(gl_FragCoord.xy), int2(0), max_texel);
  float4 horizons = goo_gtao_unpack_horizons(texelFetch(goo_horizon_tx, texel, 0));

  float vis = 1.0f;
  float3 bent_n = N;
  bool use_bent = (flags & GOO_GTAO_FLAG_BENT_NORMALS) != 0;
  goo_gtao_integrate(horizons,
                     gl_FragCoord.xy,
                     goo_light_buf.sample_params.yz,
                     drw_world_incident_vector(g_data.P),
                     safe_normalize(N),
                     g_data.Ng,
                     0.0f,
                     use_bent,
                     vis,
                     bent_n);

  if (use_bent) {
    out_bent_n = bent_n;
  }
  if ((flags & GOO_GTAO_FLAG_BOUNCE) != 0) {
    vis = goo_gtao_multibounce(vis, albedo);
  }
  return pow(clamp(vis, 0.0f, 1.0f), max(goo_light_buf.gtao_params.y, 0.0f));
#endif
}

float goo_gtao_eval_vis(float3 N, float3 albedo, bool respect_enable_flag)
{
  float3 unused_bent = N;
  return goo_gtao_eval_vis_bent(N, albedo, respect_enable_flag, unused_bent);
}

float goo_scene_diffuse_ao(float3 N, float3 albedo)
{
  return goo_gtao_eval_vis(N, albedo, true);
}

float goo_eval_ao_pass(float3 N, float3 albedo)
{
  return goo_gtao_eval_vis(N, albedo, false);
}

void goo_eval_light_shadowed(
    int i, float3 P, float3 N, float &att_unshadowed, float &vis, float3 &L)
{
  goo_eval_light_unshadowed(i, P, att_unshadowed, L);
  vis = 1.0f;
  if (att_unshadowed > 0.0f) {
    GooLightData ld = goo_light_buf.lights[i];
    if (goo_light_group_match(ld.light_group_bits, goo_light_group_shadows)) {
      int shadow_id_mode = ((goo_material_flags & GOO_MAT_FLAG_CHECK_SHADOW_ID) != 0) ? 1 : 0;
      vis = goo_light_shadow(ld, P, N, shadow_id_mode);
      vis *= goo_contact_shadow(ld, P, vis);
    }
  }
}

void goo_eval_light(int i, float3 P, float3 N, float &attenuation, float3 &L)
{
  float att_unshadowed;
  float vis;
  goo_eval_light_shadowed(i, P, N, att_unshadowed, vis, L);
  attenuation = att_unshadowed * vis;
}

float goo_eval_shadow_pass(float3 P, float3 N)
{
  if (g_goo_shadow_weight > 1e-5f) {
    return saturate(g_goo_shadow_sum / g_goo_shadow_weight);
  }
  float sum = 0.0f;
  float weight = 0.0f;
  float3 n_n = safe_normalize(N);
  for (int i = 0; i < goo_light_buf.light_count; i++) {
    GooLightData ld = goo_light_buf.lights[i];
    if (!goo_light_group_match(ld.light_group_bits, goo_light_groups)) {
      continue;
    }
    float att_unshadowed;
    float vis;
    float3 L;
    goo_eval_light_shadowed(i, P, n_n, att_unshadowed, vis, L);
    float ndotl = max(0.0f, dot(n_n, L));
    if (att_unshadowed > 0.0f && ndotl > 0.0f) {
      float light_fac = max(ld.color_energy.r, max(ld.color_energy.g, ld.color_energy.b)) *
                        ld.spot_shadow.w * att_unshadowed * ndotl;
      sum += vis * light_fac;
      weight += light_fac;
    }
  }
  return (weight > 1e-5f) ? saturate(sum / weight) :
                            ((goo_light_buf.light_count > 0) ? 0.0f : 1.0f);
}

float3 goo_diffuse_light(float3 N, float3 P, float3 albedo)
{
  float3 bent_n = N;
  float ao = goo_gtao_eval_vis_bent(N, albedo, true, bent_n);
#ifdef GOO_INDIRECT_READ
  float3 V = drw_world_incident_vector(P);
  float3 indirect = goo_eval_diffuse_indirect(P, bent_n, V);
#else
  float3 indirect = goo_light_buf.world_ambient.rgb;
#endif
  float3 light = indirect * ao;
  for (int i = 0; i < goo_light_buf.light_count; i++) {
    GooLightData ld = goo_light_buf.lights[i];
    if (!goo_light_group_match(ld.light_group_bits, goo_light_groups)) {
      continue;
    }
    float att_unshadowed;
    float vis;
    float3 L;
    goo_eval_light_shadowed(i, P, N, att_unshadowed, vis, L);
    float ndotl = max(0.0f, dot(N, L));
    if (att_unshadowed > 0.0f && ndotl > 0.0f) {
      float light_fac = max(ld.color_energy.r, max(ld.color_energy.g, ld.color_energy.b)) *
                        ld.spot_shadow.w * att_unshadowed * ndotl;
      g_goo_shadow_sum += vis * light_fac;
      g_goo_shadow_weight += light_fac;
      float att = att_unshadowed * vis;
      float3 radiance = ld.color_energy.rgb * ld.spot_shadow.w;
      light += radiance * att * ndotl * (1.0f / 3.14159265f);
    }
  }
  return light;
}

float3 goo_specular_light_ex(float3 N,
                             float3 P,
                             float roughness,
                             float3 &out_indirect_spec,
                             float &out_planar_conf,
                             float &out_ao)
{
  float3 V = drw_world_incident_vector(P);
  float shininess = mix(512.0f, 2.0f, clamp(roughness, 0.0f, 1.0f));
  float3 bent_n = N;
  out_ao = goo_gtao_eval_vis_bent(N, float3(0.5f), true, bent_n);
#ifdef GOO_INDIRECT_READ
  out_indirect_spec = goo_eval_specular_indirect(P, N, V, roughness, out_planar_conf);
#else
  out_indirect_spec = goo_light_buf.world_ambient.rgb * (1.0f - roughness) * 0.5f;
  out_planar_conf = 0.0f;
#endif
  float3 light = out_indirect_spec * out_ao;
  for (int i = 0; i < goo_light_buf.light_count; i++) {
    GooLightData ld = goo_light_buf.lights[i];
    if (!goo_light_group_match(ld.light_group_bits, goo_light_groups)) {
      continue;
    }
    float att_unshadowed;
    float vis;
    float3 L;
    goo_eval_light_shadowed(i, P, N, att_unshadowed, vis, L);
    float ndotl = max(0.0f, dot(N, L));
    if (att_unshadowed > 0.0f && ndotl > 0.0f) {
      float light_fac = max(ld.color_energy.r, max(ld.color_energy.g, ld.color_energy.b)) *
                        ld.spot_shadow.w * att_unshadowed * ndotl;
      g_goo_shadow_sum += vis * light_fac;
      g_goo_shadow_weight += light_fac;
      float att = att_unshadowed * vis;
      float3 H = normalize(L + V);
      float spec = pow(max(0.0f, dot(N, H)), shininess) * (shininess + 8.0f) / 25.132741f;
      float3 radiance = ld.color_energy.rgb * ld.extra.x;
      light += radiance * att * spec * ndotl;
    }
  }
  return light;
}

float3 goo_specular_light(float3 N, float3 P, float roughness)
{
  float3 unused_ind = float3(0.0f);
  float unused_planar = 0.0f;
  float unused_ao = 1.0f;
  return goo_specular_light_ex(N, P, roughness, unused_ind, unused_planar, unused_ao);
}

/* -------------------------------------------------------------------- */
/** \name Goo NPR Node Helpers (ShaderInfo, Curvature, ScreenspaceInfo)
 * \{ */

float3 goo_view_position_get()
{
  float3 vP = drw_point_world_to_view(g_data.P);
  return float3(vP.x, vP.y, -vP.z);
}

void goo_calc_shader_info(float3 position,
                          float3 normal,
                          int4 light_groups,
                          int4 light_group_shadows,
                          float4 &half_light,
                          float &shadows,
                          float &self_shadows,
                          float4 &ambient,
                          float &half_lambert)
{
  float3 n_n = safe_normalize(normal);
  float shadow_accum = 0.0f;
  float self_shadow_accum = 0.0f;
  float light_accum = 0.0f;
  half_light = float4(0.0f);
  half_lambert = 0.0f;

  for (int i = 0; i < goo_light_buf.light_count; i++) {
    GooLightData ld = goo_light_buf.lights[i];
    if (!goo_light_group_match(ld.light_group_bits, light_groups)) {
      continue;
    }

    float att_unshadowed;
    float3 L;
    goo_eval_light_unshadowed(i, position, att_unshadowed, L);
    float ndotl = dot(n_n, L);

    if (goo_light_group_match(ld.light_group_bits, light_group_shadows) &&
        att_unshadowed > 0.0f)
    {
      int cast_mode = ((goo_material_flags & GOO_MAT_FLAG_CHECK_SHADOW_ID) != 0) ? 1 : 0;
      float vis = goo_light_shadow(ld, position, n_n, cast_mode);
      vis *= goo_contact_shadow(ld, position, vis);
      float light_fac = max(ld.color_energy.r, max(ld.color_energy.g, ld.color_energy.b)) *
                        ld.spot_shadow.w * att_unshadowed;
      shadow_accum += (1.0f - vis) * light_fac;
      float self_map_vis = goo_light_shadow(ld, position, n_n, 2);
      float self_vis = min(self_map_vis, smoothstep(-0.05f, 0.05f, ndotl));
      self_shadow_accum += (1.0f - self_vis) * light_fac;
      light_accum += light_fac;
    }

    float3 radiance = ld.color_energy.rgb * ld.spot_shadow.w * att_unshadowed *
                      max(0.0f, ndotl) * (1.0f / 3.14159265f);
    half_light += float4(radiance, 0.0f);
    half_lambert += (0.5f * ndotl + 0.5f) * att_unshadowed;
  }

  shadows = (light_accum > 1e-5f) ? (1.0f - shadow_accum / light_accum) : 1.0f;
  self_shadows = (light_accum > 1e-5f) ? (1.0f - self_shadow_accum / light_accum) : 1.0f;
#ifdef GOO_INDIRECT_READ
  ambient = float4(
      goo_eval_diffuse_indirect(position, n_n, drw_world_incident_vector(position)), 1.0f);
#else
  ambient = float4(goo_light_buf.world_ambient.rgb, 1.0f);
#endif
}

void goo_calc_shader_info(float3 position,
                          float3 normal,
                          float4 &half_light,
                          float &shadows,
                          float &self_shadows,
                          float4 &ambient,
                          float &half_lambert)
{
  goo_calc_shader_info(position,
                       normal,
                       goo_light_groups,
                       goo_light_group_shadows,
                       half_light,
                       shadows,
                       self_shadows,
                       ambient,
                       half_lambert);
}

void goo_screenspace_info(float3 viewPos, float4 &scene_col, float &scene_depth)
{
  float2 uv = clamp(
      drw_point_view_to_screen(float3(viewPos.x, viewPos.y, -viewPos.z)).xy, float2(0.0f), float2(1.0f));
  scene_col = textureLod(goo_scene_color_tx, uv, 0.0f);
  float depth = textureLod(goo_depth_tx, uv, 0.0f).r;
  scene_depth = -drw_depth_screen_to_view(depth);
}

float2 goo_rotate2d(float2 v, float a)
{
  float s = sin(a);
  float c = cos(a);
  return float2(c * v.x - s * v.y, s * v.x + c * v.y);
}

void goo_screenspace_curvature(float iiterations,
                               float sample_scale,
                               float clamp_dist,
                               float3 scale,
                               float &scene_curvature,
                               float &scene_rim)
{
  float2 uvs = clamp(drw_point_world_to_screen(g_data.P).xy, float2(0.0f), float2(1.0f));
  float2 texel_size = float2(1.0f / 1920.0f, 1.0f / 1080.0f);
  float mid_depth = drw_depth_screen_to_view(textureLod(goo_depth_tx, uvs, 0.0f).r);

  float clamp_range = 0.001f;
  int n_samples = clamp(int(iiterations), 1, 64);
  float i_samples = 64.0f / float(n_samples);
  float safe_scale = max(abs(sample_scale), 1e-4f);
  float alpha_hash = fract(sin(dot(gl_FragCoord.xy, float2(12.9898f, 78.233f))) * 43758.5453f);

  float accum = 0.0f;
  float rim_accum = 0.0f;

  for (int r = 0; r < 8; r++) {
    float2 offset = goo_rotate2d(float2(1.0f, 0.0f),
                                 (float(r) + alpha_hash) * 3.14159265f * 0.25f * 0.5f) *
                    texel_size * sample_scale * scale.xy;

    for (int i = 1; i <= n_samples; i++) {
      float2 uv_left = clamp(uvs + offset * float(i) * i_samples, float2(0.0f), float2(1.0f));
      float2 uv_right = clamp(uvs - offset * float(i) * i_samples, float2(0.0f), float2(1.0f));
      float left = drw_depth_screen_to_view(textureLod(goo_depth_tx, uv_left, 0.0f).r);
      float right = drw_depth_screen_to_view(textureLod(goo_depth_tx, uv_right, 0.0f).r);

      float curve = clamp(left - mid_depth, -clamp_range, clamp_range) +
                    clamp(right - mid_depth, -clamp_range, clamp_range);
      float afac = 1.0f - float(i - 1) / float(n_samples);

      accum += curve * afac * 0.001f;
      rim_accum += min(mid_depth - min(left, right), clamp_dist) * afac;
    }
  }

  scene_curvature = -accum / length(texel_size) * i_samples;
  scene_rim = rim_accum / safe_scale * clamp_range;
}

/** \} */

/** \} */

/* -------------------------------------------------------------------- */
/** \name Closure evaluation
 * \{ */

Closure closure_eval(ClosureDiffuse diffuse)
{
  float3 light = goo_diffuse_light(diffuse.N, g_data.P, diffuse.color);
  g_goo_radiance += diffuse.color * diffuse.weight * light;
  g_goo_diff_color += diffuse.color * diffuse.weight;
  g_goo_diff_light += light * diffuse.weight;
  return Closure(0);
}

float3 goo_eval_sss_translucency(float3 P, float3 N, float3 sss_radius)
{
  float3 n_n = safe_normalize(N);
  float3 safe_rad = max(sss_radius * (0.25f / 3.14159265f), float3(1e-4f));
  float3 P_in = P - n_n * 0.005f;
  float3 total = float3(0.0f);

  for (int i = 0; i < goo_light_buf.light_count; i++) {
    GooLightData ld = goo_light_buf.lights[i];
    if (!goo_light_group_match(ld.light_group_bits, goo_light_groups)) {
      continue;
    }
    float att_unshadowed;
    float3 L;
    goo_eval_light_unshadowed(i, P, att_unshadowed, L);
    float ndotl_back = max(0.0f, dot(-n_n, L));
    if (att_unshadowed <= 0.0f || ndotl_back <= 1e-5f) {
      continue;
    }

    float3 radiance = ld.color_energy.rgb * ld.spot_shadow.w;
    if (ld.spot_shadow.y < -0.5f || !goo_light_group_match(ld.light_group_bits, goo_light_group_shadows)) {
      float3 thin_trans = exp(-0.25f / safe_rad);
      total += radiance * att_unshadowed * ndotl_back * (1.0f / 3.14159265f) * thin_trans;
      continue;
    }

    int shadow_start = int(ld.spot_shadow.y + 0.5f);
    int shadow_count = int(ld.spot_shadow.z + 0.5f);
    int light_type = int(ld.position_type.w + 0.5f);
    int slice = -1;
    float4 clip = float4(0.0f);

    if (light_type == GOO_LIGHT_SUN) {
      for (int c = 0; c < shadow_count; c++) {
        GooShadowSlice sc = goo_shadow_buf.slices[shadow_start + c];
        float4 cl = sc.vp * float4(P_in, 1.0f);
        if (cl.w > 0.0f) {
          float3 co = (cl.xyz / cl.w) * 0.5f + 0.5f;
          if (co.x >= 0.01f && co.x <= 0.99f && co.y >= 0.01f && co.y <= 0.99f && co.z >= 0.0f &&
              co.z <= 1.0f)
          {
            slice = shadow_start + c;
            clip = cl;
            break;
          }
        }
      }
    }
    else if (light_type == GOO_LIGHT_SPOT) {
      slice = shadow_start;
      clip = goo_shadow_buf.slices[slice].vp * float4(P_in, 1.0f);
    }
    else if (light_type == GOO_LIGHT_POINT && shadow_count == 6) {
      float3 d = P_in - ld.position_type.xyz;
      float3 ad = abs(d);
      int face = 0;
      if (ad.x >= ad.y && ad.x >= ad.z) {
        face = (d.x >= 0.0f) ? 0 : 1;
      }
      else if (ad.y >= ad.z) {
        face = (d.y >= 0.0f) ? 2 : 3;
      }
      else {
        face = (d.z >= 0.0f) ? 4 : 5;
      }
      slice = shadow_start + face;
      clip = goo_shadow_buf.slices[slice].vp * float4(P_in, 1.0f);
    }

    if (slice < 0 || clip.w <= 1e-6f) {
      continue;
    }

    GooShadowSlice s = goo_shadow_buf.slices[slice];
    float3 coord = (clip.xyz / clip.w) * 0.5f + 0.5f;
    if (coord.x < 0.0f || coord.x > 1.0f || coord.y < 0.0f || coord.y > 1.0f || coord.z < 0.0f ||
        coord.z > 1.0f)
    {
      continue;
    }

    float stored_d = textureLod(goo_shadow_tx, float3(coord.xy, float(slice)), 0.0f).r;
    if (stored_d >= 0.99999f) {
      continue;
    }

    float thickness = 0.0f;
    if (s.params.z > 0.0f) {
      thickness = max((coord.z - stored_d) * s.params.z, 0.0f);
    }
    else {
      float far = max(ld.extra.y, 1.0f);
      float near = max(ld.extra.z, 1e-3f);
      float z_hit = (near * far) / max(far - stored_d * (far - near), 1e-5f);
      float z_cur = (near * far) / max(far - coord.z * (far - near), 1e-5f);
      thickness = max(z_cur - z_hit, 0.0f);
    }

    float3 trans = exp(-thickness / safe_rad);
    total += radiance * att_unshadowed * ndotl_back * (1.0f / 3.14159265f) * trans;
  }
  return total;
}

Closure closure_eval(ClosureSubsurface sss)
{
  float3 light = goo_diffuse_light(sss.N, g_data.P, sss.color);
  if ((goo_material_flags & GOO_MAT_FLAG_SSS_TRANSLUCENCY) != 0) {
    light += goo_eval_sss_translucency(g_data.P, sss.N, sss.sss_radius);
  }
  float3 direct_rad = sss.color * sss.weight * light;
  g_goo_radiance += direct_rad;
  g_goo_diff_color += sss.color * sss.weight;
  g_goo_diff_light += light * sss.weight;

  float max_rad = max(sss.sss_radius.r, max(sss.sss_radius.g, sss.sss_radius.b));
  if (sss.weight > 1e-6f && max_rad > 1e-5f) {
    g_goo_sss_irrad += light * sss.weight;
    g_goo_sss_albedo += sss.color * sss.weight;
    g_goo_sss_radius += sss.sss_radius * sss.weight;
    g_goo_sss_direct_radiance += direct_rad;
    g_goo_sss_weight += sss.weight;
  }
  return Closure(0);
}

Closure closure_eval(ClosureTranslucent translucent)
{
  float3 light = goo_diffuse_light(-translucent.N, g_data.P, translucent.color);
  g_goo_radiance += translucent.color * translucent.weight * light;
  g_goo_diff_color += translucent.color * translucent.weight;
  g_goo_diff_light += light * translucent.weight;
  return Closure(0);
}

Closure closure_eval(ClosureReflection reflection)
{
  float3 indirect_spec = float3(0.0f);
  float planar_conf = 0.0f;
  float ao = 1.0f;
  float3 light = goo_specular_light_ex(
      reflection.N, g_data.P, reflection.roughness, indirect_spec, planar_conf, ao);
  float3 throughput = reflection.color * reflection.weight;
  g_goo_radiance += throughput * light;
  g_goo_spec_color += throughput;
  g_goo_spec_light += light * reflection.weight;

  float lobe_w = length(throughput);
  if (lobe_w > g_goo_ssr_weight) {
    g_goo_ssr_weight = lobe_w;
    g_goo_ssr_normal = safe_normalize(reflection.N);
    g_goo_ssr_roughness = saturate(reflection.roughness);
  }
  g_goo_ssr_throughput += throughput * ao;
  g_goo_ssr_fallback += throughput * indirect_spec * ao;
  g_goo_ssr_mask = max(g_goo_ssr_mask, saturate(1.0f - planar_conf));
  return Closure(0);
}

float3 goo_eval_refraction(float3 N, float roughness, float ior, bool is_thin)
{
  float3 n_n = safe_normalize(N);
  float r_clamped = saturate(roughness);
  float3 P = g_data.P;
  float3 V = drw_world_incident_vector(P);

  float3 trace_P = P;
  float3 T;
  if (is_thin) {
    T = -V;
  }
  else {
    float safe_ior = max(ior, 1e-4f);
    float eta = 1.0f / safe_ior;
    float3 R1 = refract(-V, n_n, eta);
    if (dot(R1, R1) < 1e-5f) {
      R1 = reflect(-V, n_n);
    }
    R1 = safe_normalize(R1);

    if (goo_refraction_depth > 1e-4f) {
      float denom = min(dot(R1, n_n), -0.1f);
      float t_exit = -goo_refraction_depth / denom;
      trace_P = P + R1 * clamp(t_exit, 0.0f, goo_refraction_depth * 6.0f);
      T = -V;
    }
    else {
      T = R1;
    }
  }

#ifdef GOO_INDIRECT_READ
  float3 fallback_rad = goo_eval_env_direction(trace_P, T, r_clamped);
  bool do_ss_refract =
      ((goo_probe_buf.ssr_flags & (GOO_SSR_FLAG_ENABLED | GOO_SSR_FLAG_REFRACTION)) ==
       (GOO_SSR_FLAG_ENABLED | GOO_SSR_FLAG_REFRACTION)) &&
      ((goo_material_flags & (GOO_MAT_FLAG_SS_REFRACTION | GOO_MAT_FLAG_IS_REFRACT_PASS)) ==
       (GOO_MAT_FLAG_SS_REFRACTION | GOO_MAT_FLAG_IS_REFRACT_PASS));
  if (!do_ss_refract) {
    return fallback_rad;
  }

  float max_rough = goo_probe_buf.ssr_params.y;
  if (r_clamped > max_rough + 0.2f) {
    return fallback_rad;
  }

  float3 vP_start = drw_point_world_to_view(trace_P);
  float3 vT = safe_normalize(drw_normal_world_to_view(T));
  float3 ss_start = drw_point_view_to_screen(vP_start);
  float v_near = drw_depth_screen_to_view(0.0f);
  float max_t = 30.0f;
  if (vT.z > 1e-4f) {
    max_t = min(max_t, max((v_near - vP_start.z) * 0.9f / vT.z, 0.05f));
  }
  float3 ss_end = drw_point_view_to_screen(vP_start + vT * max_t);
  if (any(lessThan(ss_start.xy, float2(0.0f))) || any(greaterThan(ss_start.xy, float2(1.0f)))) {
    return fallback_rad;
  }

  float3 ss_delta = ss_end - ss_start;
  float clip_t = 1.0f;
  if (ss_delta.x > 1e-6f) {
    clip_t = min(clip_t, (1.0f - ss_start.x) / ss_delta.x);
  }
  else if (ss_delta.x < -1e-6f) {
    clip_t = min(clip_t, -ss_start.x / ss_delta.x);
  }
  if (ss_delta.y > 1e-6f) {
    clip_t = min(clip_t, (1.0f - ss_start.y) / ss_delta.y);
  }
  else if (ss_delta.y < -1e-6f) {
    clip_t = min(clip_t, -ss_start.y / ss_delta.y);
  }
  ss_delta *= clamp(clip_t, 0.0f, 1.0f);

  const int STEPS = 48;
  float dt = 1.0f / float(STEPS);
  float step_dz = abs(vT.z * max_t * clamp(clip_t, 0.0f, 1.0f)) * dt;
  float thickness = max(max(goo_probe_buf.ssr_params.z * 2.0f, 0.25f), step_dz * 2.0f);
  float t_hit = 0.0f;
  float t_prev = 0.0f;
  bool found = false;

  for (int i = 1; i <= STEPS; i++) {
    float t = float(i) * dt;
    float3 cur_ss = ss_start + ss_delta * t;
    if (any(lessThan(cur_ss.xy, float2(0.0f))) || any(greaterThan(cur_ss.xy, float2(1.0f))) ||
        cur_ss.z <= 0.0f || cur_ss.z >= 1.0f)
    {
      break;
    }
    float surf_z = textureLod(goo_depth_tx, cur_ss.xy, 0.0f).r;
    if (surf_z >= 1.0f) {
      t_prev = t;
      continue;
    }
    float ray_vz = drw_depth_screen_to_view(cur_ss.z);
    float surf_vz = drw_depth_screen_to_view(surf_z);
    float diff = surf_vz - ray_vz;
    if (diff > -0.02f && diff < thickness) {
      t_hit = t;
      found = true;
      break;
    }
    t_prev = t;
  }

  float2 hit_uv = float2(-1.0f);
  float hit_conf = 0.0f;
  if (found) {
    float lo = t_prev;
    float hi = t_hit;
    for (int b = 0; b < 5; b++) {
      float mid = 0.5f * (lo + hi);
      float3 mid_ss = ss_start + ss_delta * mid;
      float surf_z = textureLod(goo_depth_tx, mid_ss.xy, 0.0f).r;
      float ray_vz = drw_depth_screen_to_view(mid_ss.z);
      float surf_vz = drw_depth_screen_to_view(surf_z);
      float diff = surf_vz - ray_vz;
      if (surf_z < 1.0f && diff > -0.02f && diff < thickness * 1.5f) {
        hi = mid;
      }
      else {
        lo = mid;
      }
    }
    hit_uv = clamp(ss_start.xy + ss_delta.xy * hi, float2(0.0f), float2(1.0f));
    float border = min(min(hit_uv.x, 1.0f - hit_uv.x), min(hit_uv.y, 1.0f - hit_uv.y));
    hit_conf = saturate(border / max(goo_probe_buf.ssr_params.w, 1e-4f));
  }
  else {
    float bg_z = textureLod(goo_depth_tx, ss_start.xy, 0.0f).r;
    if (bg_z >= 1.0f) {
      hit_uv = clamp(ss_start.xy, float2(0.0f), float2(1.0f));
      float border = min(min(hit_uv.x, 1.0f - hit_uv.x), min(hit_uv.y, 1.0f - hit_uv.y));
      hit_conf = saturate(border / max(goo_probe_buf.ssr_params.w, 1e-4f));
    }
  }

  hit_conf *= 1.0f - smoothstep(max_rough, max(max_rough + 0.2f, max_rough + 1e-4f), r_clamped);
  if (hit_conf <= 1e-4f) {
    return fallback_rad;
  }

  float lod = clamp(sqrt(r_clamped) * float(GOO_COLOR_MIPS - 1), 0.0f, float(GOO_COLOR_MIPS - 1));
  float3 ss_rad = max(textureLod(goo_scene_color_tx, hit_uv, lod).rgb, float3(0.0f));
  float firefly = goo_probe_buf.indirect_params.x;
  if (firefly > 0.0f) {
    float lum = max(ss_rad.r, max(ss_rad.g, ss_rad.b));
    if (lum > firefly) {
      ss_rad *= (firefly / lum);
    }
  }
  return mix(fallback_rad, ss_rad, hit_conf);
#else
  return goo_light_buf.world_ambient.rgb;
#endif
}

Closure closure_eval(ClosureRefraction refraction)
{
  float3 light = goo_eval_refraction(refraction.N, refraction.roughness, refraction.ior, false);
  g_goo_radiance += refraction.color * refraction.weight * light;
  return Closure(0);
}

Closure closure_eval(ClosureThinRefraction refraction)
{
  float3 light = goo_eval_refraction(refraction.N, refraction.roughness, 1.0f, true);
  g_goo_radiance += refraction.color * refraction.weight * light;
  return Closure(0);
}

Closure closure_eval(ClosureEmission emission)
{
  g_goo_radiance += emission.emission * emission.weight;
  g_goo_emission += emission.emission * emission.weight;
  return Closure(0);
}

Closure closure_eval(ClosureTransparency transparency)
{
  g_goo_transmittance += transparency.transmittance * transparency.weight;
  g_holdout += transparency.holdout * transparency.weight;
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

Closure closure_eval(ClosureHair hair)
{
  float3 V = drw_world_incident_vector(g_data.P);
  float3 T = safe_normalize(hair.T);
  float3 B = cross(V, T);
  float3 N = (dot(B, B) > 1e-6f) ? safe_normalize(cross(T, normalize(B))) : g_data.N;
  float3 diff_light = goo_diffuse_light(N, g_data.P, hair.color);
  float rough = clamp((hair.roughness.x + hair.roughness.y) * 0.5f, 0.05f, 1.0f);
  float3 spec_light = goo_specular_light(N, g_data.P, rough);
  float3 spec_tint = mix(float3(0.25f), hair.color, 0.5f);
  g_goo_radiance += hair.weight * (hair.color * diff_light + spec_tint * spec_light);
  g_goo_diff_color += hair.color * hair.weight;
  g_goo_diff_light += diff_light * hair.weight;
  g_goo_spec_color += spec_tint * hair.weight;
  g_goo_spec_light += spec_light * hair.weight;
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

void goo_store_surface_passes(int2 texel, float holdout, uint resource_id)
{
#if defined(GOO_PASS_WRITE) && !defined(MAT_GEOM_WORLD)
  if ((goo_material_flags & GOO_MAT_FLAG_NO_PASS_WRITE) != 0 ||
      (goo_probe_buf.capture_flags & GOO_CAPTURE_NO_PASS_WRITE) != 0)
  {
    return;
  }

  float vis = 1.0f - saturate(holdout);
  if (goo_pass_buf.normal_id >= 0) {
    imageStoreFast(goo_rp_color_img,
                   int3(texel, goo_pass_buf.normal_id),
                   float4(safe_normalize(g_data.N) * vis, 1.0f));
  }
  if (goo_pass_buf.position_id >= 0) {
    imageStoreFast(
        goo_rp_color_img, int3(texel, goo_pass_buf.position_id), float4(g_data.P * vis, 1.0f));
  }
  if (goo_pass_buf.diff_dir_id >= 0) {
    imageStoreFast(goo_rp_color_img,
                   int3(texel, goo_pass_buf.diff_dir_id),
                   float4(g_goo_diff_light * vis, 1.0f));
  }
  if (goo_pass_buf.diff_col_id >= 0) {
    imageStoreFast(goo_rp_color_img,
                   int3(texel, goo_pass_buf.diff_col_id),
                   float4(g_goo_diff_color * vis, 1.0f));
  }
  if (goo_pass_buf.gloss_dir_id >= 0) {
    imageStoreFast(goo_rp_color_img,
                   int3(texel, goo_pass_buf.gloss_dir_id),
                   float4(g_goo_spec_light * vis, 1.0f));
  }
  if (goo_pass_buf.gloss_col_id >= 0) {
    imageStoreFast(goo_rp_color_img,
                   int3(texel, goo_pass_buf.gloss_col_id),
                   float4(g_goo_spec_color * vis, 1.0f));
  }
  if (goo_pass_buf.emit_id >= 0) {
    imageStoreFast(goo_rp_color_img,
                   int3(texel, goo_pass_buf.emit_id),
                   float4(g_goo_emission * vis, 1.0f));
  }
  if (goo_pass_buf.shadow_id >= 0) {
    float shad = goo_eval_shadow_pass(g_data.P, g_data.N);
    imageStoreFast(goo_rp_color_img,
                   int3(texel, goo_pass_buf.shadow_id),
                   float4(float3(shad) * vis, 1.0f));
  }
  if (goo_pass_buf.ao_id >= 0) {
    float ao = goo_eval_ao_pass(g_data.N, max(g_goo_diff_color, float3(0.5f)));
    imageStoreFast(
        goo_rp_color_img, int3(texel, goo_pass_buf.ao_id), float4(float3(ao) * vis, 1.0f));
  }
  if (goo_pass_buf.crypto_flags != 0 && holdout < 0.999f) {
    float2 obj_asset =
        ((goo_pass_buf.crypto_flags & (GOO_CRYPTO_FLAG_OBJECT | GOO_CRYPTO_FLAG_ASSET)) != 0) ?
            goo_crypto_object_buf[resource_id] :
            float2(0.0f);
    imageStoreFast(
        goo_rp_crypto_img, texel, float4(obj_asset.x, obj_asset.y, goo_material_hash, 1.0f));
  }
#endif
}

/** \} */

/** \} */
