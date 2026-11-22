/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_effects_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_dof_gather)

#include "eevee_utility_tx.bsl.hh"
#include "gpu_shader_math_constants_lib.glsl"

float2 goo_dof_polygon_map(float r, float theta, float blades, float rotation)
{
  if (blades >= 2.5f) {
    float sector = 6.2831853f / blades;
    float half_sector = sector * 0.5f;
    float local_a = mod(theta + half_sector, sector) - half_sector;
    r *= cos(half_sector) / max(cos(local_a), 1e-4f);
  }
  float ang = theta + rotation;
  return float2(cos(ang), sin(ang)) * r;
}

void main()
{
  int2 extent = int2(goo_dof_buf.filter_params.xy + 0.5f);
  int2 texel = int2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(texel, extent))) {
    return;
  }

  float4 center_col = texelFetch(src_color_tx, texel, 0);
  float center_coc = texelFetch(coc_tx, texel, 0).r;
  int2 tile_max_coord = max(textureSize(tile_tx, 0) - int2(1), int2(0));
  float2 tile_coc = texelFetch(tile_tx, clamp(texel / 16, int2(0), tile_max_coord), 0).rg;

  float tile_fg_max = tile_coc.r;
  float tile_bg_max = tile_coc.g;
  if (abs(center_coc) < 0.35f && tile_fg_max < 0.5f) {
    imageStoreFast(dst_color_img, texel, center_col);
    return;
  }

  float blades = goo_dof_buf.bokeh_params.x;
  float rot = goo_dof_buf.bokeh_params.y;
  float bokeh_thr = goo_dof_buf.bokeh_params.z;
  float bokeh_nmax = goo_dof_buf.bokeh_params.w;
  float2 aniso = goo_dof_buf.aniso_params.xy;
  bool hq_slight = (goo_dof_buf.filter_params.w > 0.5f);

  [[resource_table]] const UtilityTexture util_tx = resource_table_get(UtilityTexture);
  float2 noise = util_tx.fetch(float2(texel), float(UTIL_BLUE_NOISE_LAYER)).xy;
  float theta_jitter = noise.x * 6.2831853f + float(sample_index) * 2.39996323f;

  int tap_count = (hq_slight || max(tile_fg_max, tile_bg_max) > 12.0f) ? 64 : 48;
  float search_rad = max(abs(center_coc), tile_fg_max);
  if (search_rad < 0.5f) {
    imageStoreFast(dst_color_img, texel, center_col);
    return;
  }

  float center_fg_w = smoothstep(0.0f, 1.5f, -center_coc);
  float center_bg_w = smoothstep(0.0f, 1.5f, center_coc);

  float4 bg_accum = float4(0.0f);
  float bg_weight = 0.0f;
  float4 fg_accum = float4(0.0f);
  float fg_weight = 0.0f;
  float fg_total_taps = 0.0f;

  if (center_coc >= -0.5f) {
    bg_accum = center_col * 0.5f;
    bg_weight = 0.5f;
  }
  else {
    fg_accum = center_col * 0.5f;
    fg_weight = 0.5f;
    fg_total_taps = 0.5f;
  }

  float2 inv_ext = 1.0f / float2(extent);
  float2 base_uv = (float2(texel) + 0.5f) * inv_ext;

  /* Background gather pass + Foreground bokeh gather pass. */
  float fg_search = max(tile_fg_max, max(-center_coc, 0.0f));
  float bg_search = max(center_coc, 0.0f);

  for (int i = 0; i < 64; i++) {
    if (i >= tap_count) {
      break;
    }
    float u = (float(i) + 0.25f + 0.5f * noise.y) / float(tap_count);
    float r_disk = sqrt(u);
    float theta = float(i) * 2.39996323f + theta_jitter;
    float2 shape_off = goo_dof_polygon_map(r_disk, theta, blades, rot) * aniso;

    /* Background gather within center_coc (never bleed foreground over sharp background). */
    if (bg_search > 0.35f) {
      float r_bg_px = r_disk * bg_search;
      float2 tap_uv = clamp(base_uv + shape_off * bg_search * inv_ext, float2(0.0f), float2(1.0f));
      float tap_coc = textureLod(coc_tx, tap_uv, 0.0f).r;
      if (tap_coc >= -0.35f) {
        float w = smoothstep(r_bg_px - 1.0f, r_bg_px + 0.5f, max(tap_coc, 0.0f));
        /* Depth gate: reject closer focused samples when pixel is out-of-focus background. */
        if (tap_coc < center_coc * 0.5f && center_coc > 2.0f) {
          w *= saturate(tap_coc / max(center_coc, 1e-4f));
        }
        if (w > 1e-4f) {
          float4 s_col = max(textureLod(src_color_tx, tap_uv, 0.0f), float4(0.0f));
          float lum = max(s_col.r, max(s_col.g, s_col.b));
          if (lum > bokeh_thr && bokeh_nmax > bokeh_thr) {
            float boost = 1.0f + saturate((lum - bokeh_thr) / max(bokeh_nmax - bokeh_thr, 1e-4f));
            w *= boost;
          }
          bg_accum += s_col * w;
          bg_weight += w;
        }
      }
    }

    /* Foreground gather over dilated tile fg radius. */
    if (fg_search > 0.35f) {
      float r_fg_px = r_disk * fg_search;
      float2 tap_uv = clamp(base_uv + shape_off * fg_search * inv_ext, float2(0.0f), float2(1.0f));
      float tap_coc = textureLod(coc_tx, tap_uv, 0.0f).r;
      float tap_fg_r = max(-tap_coc, 0.0f);
      fg_total_taps += 1.0f;
      if (tap_fg_r > 0.25f) {
        float w = smoothstep(r_fg_px - 1.25f, r_fg_px + 0.5f, tap_fg_r);
        if (w > 1e-4f) {
          float4 s_col = max(textureLod(src_color_tx, tap_uv, 0.0f), float4(0.0f));
          fg_accum += s_col * w;
          fg_weight += w;
        }
      }
    }
  }

  float4 bg_col = (bg_weight > 1e-5f) ? (bg_accum / bg_weight) : center_col;
  float4 base_col = mix(center_col, bg_col, center_bg_w);

  if (fg_weight > 1e-4f && fg_total_taps > 0.0f) {
    float4 fg_col = fg_accum / fg_weight;
    float fg_cov = saturate((fg_weight / fg_total_taps) * 1.35f);
    float fg_alpha = max(center_fg_w, fg_cov * smoothstep(0.0f, 1.5f, fg_search));
    base_col = mix(base_col, fg_col, saturate(fg_alpha));
  }

  imageStoreFast(dst_color_img, texel, base_col);
}
