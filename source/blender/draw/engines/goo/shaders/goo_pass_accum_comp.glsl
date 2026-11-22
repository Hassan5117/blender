/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_post_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_pass_accum)

#include "draw_view_lib.glsl"
#include "gpu_shader_utildefines_lib.glsl"

void main()
{
  int2 dst_texel = int2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(dst_texel, output_extent))) {
    return;
  }

  int2 src_size = max(textureSize(sample_depth_tx, 0), int2(1));
  int2 src_texel = clamp(dst_texel + overscan_offset, int2(0), src_size - int2(1));
  float depth = texelFetch(sample_depth_tx, src_texel, 0).r;

  for (int layer = 0; layer < goo_pass_buf.color_layer_count; layer++) {
    float4 val = texelFetch(sample_color_tx, int3(src_texel, layer), 0);
    if (layer == goo_pass_buf.env_id && goo_pass_buf.mist_params.w > 0.5f && depth >= 1.0f) {
      val = goo_pass_buf.env_fallback;
    }
    else if ((layer == goo_pass_buf.shadow_id || layer == goo_pass_buf.ao_id) && depth >= 1.0f) {
      val = float4(1.0f);
    }

    float4 prev = (sample_index == 0) ? float4(0.0f) :
                                        imageLoadFast(accum_color_img, int3(dst_texel, layer));
    imageStoreFast(accum_color_img, int3(dst_texel, layer), prev + val * sample_weight);
  }

  if (goo_pass_buf.depth_id >= 0) {
    float z = (depth >= 1.0f) ? 1e10f : -drw_depth_screen_to_view(depth);
    float prev_z = (sample_index == 0) ?
                       1e10f :
                       imageLoadFast(accum_value_img, int3(dst_texel, goo_pass_buf.depth_id)).r;
    imageStoreFast(accum_value_img, int3(dst_texel, goo_pass_buf.depth_id), float4(min(prev_z, z)));
  }

  if (goo_pass_buf.mist_id >= 0) {
    float mist = 1.0f;
    if (depth < 1.0f) {
      float2 uv = (float2(src_texel) + 0.5f) / float2(src_size);
      float3 vP = drw_point_screen_to_view(float3(uv, depth));
      bool is_persp = drw_view().winmat[3][3] == 0.0f;
      float mist_dist = is_persp ? length(vP) : abs(vP.z);
      mist = saturate(mist_dist * goo_pass_buf.mist_params.x + goo_pass_buf.mist_params.y);
      mist = pow(mist, max(goo_pass_buf.mist_params.z, 1e-4f));
    }
    float prev_mist = (sample_index == 0) ?
                          0.0f :
                          imageLoadFast(accum_value_img, int3(dst_texel, goo_pass_buf.mist_id)).r;
    imageStoreFast(
        accum_value_img, int3(dst_texel, goo_pass_buf.mist_id), float4(prev_mist + mist * sample_weight));
  }

  for (int i = 0; i < goo_pass_buf.aov_value_len; i++) {
    int accum_layer = goo_pass_buf.aov_value_id + i;
    float val = texelFetch(sample_value_tx, int3(src_texel, i), 0).r;
    float prev_val = (sample_index == 0) ?
                         0.0f :
                         imageLoadFast(accum_value_img, int3(dst_texel, accum_layer)).r;
    imageStoreFast(
        accum_value_img, int3(dst_texel, accum_layer), float4(prev_val + val * sample_weight));
  }

  if (goo_pass_buf.crypto_flags != 0) {
    int pass_len = (clamp(goo_pass_buf.crypto_levels, 2, GOO_CRYPTO_LEVELS_MAX) + 1) / 2;
    float4 crypto = texelFetch(sample_crypto_tx, src_texel, 0);
    bool has_geo = crypto.w > 0.5f;
    int active_layer = 0;

    for (int bit = 0; bit < 3; bit++) {
      if ((goo_pass_buf.crypto_flags & (1 << bit)) == 0) {
        continue;
      }

      int base_slice = active_layer * pass_len;
      active_layer++;

      float2 slots[GOO_CRYPTO_LEVELS_MAX];
      for (int p = 0; p < pass_len; p++) {
        float4 pair = (sample_index == 0) ?
                          float4(0.0f) :
                          imageLoadFast(accum_crypto_img, int3(dst_texel, base_slice + p));
        slots[p * 2] = pair.xy;
        slots[p * 2 + 1] = pair.zw;
      }

      float hash = (bit == 0) ? crypto.x : ((bit == 1) ? crypto.y : crypto.z);
      if (has_geo && hash != 0.0f) {
        int slot_count = pass_len * 2;
        for (int s = 0; s < slot_count; s++) {
          if (slots[s].y == 0.0f || slots[s].x == hash) {
            float cov = slots[s].y + sample_weight;
            if (cov > 0.9999f) {
              cov = 1.0f;
            }
            slots[s] = float2(hash, cov);
            break;
          }
        }
      }

      for (int p = 0; p < pass_len; p++) {
        imageStoreFast(accum_crypto_img,
                       int3(dst_texel, base_slice + p),
                       float4(slots[p * 2], slots[p * 2 + 1]));
      }
    }
  }
}
