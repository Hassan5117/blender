/* SPDX-FileCopyrightText: 2022-2023 Blender Authors
 * SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_post_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_cryptomatte_sort)

void main()
{
  int2 texel = int2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(texel, output_extent))) {
    return;
  }

  int pass_len = (clamp(crypto_levels, 2, GOO_CRYPTO_LEVELS_MAX) + 1) / 2;
  int slot_count = pass_len * 2;

  for (int layer = 0; layer < crypto_layer_count; layer++) {
    int base_slice = layer * pass_len;
    float2 samples[GOO_CRYPTO_LEVELS_MAX];
    for (int i = 0; i < GOO_CRYPTO_LEVELS_MAX; i++) {
      samples[i] = float2(0.0f);
    }

    for (int p = 0; p < pass_len; p++) {
      float4 pair = imageLoadFast(accum_crypto_img, int3(texel, base_slice + p));
      samples[p * 2] = pair.xy;
      samples[p * 2 + 1] = pair.zw;
    }

    for (int i = 0; i < slot_count - 1 && samples[i].y != 0.0f; i++) {
      int highest_index = i;
      float highest_weight = samples[i].y;
      for (int j = i + 1; j < slot_count && samples[j].y != 0.0f; j++) {
        if (samples[j].y > highest_weight) {
          highest_index = j;
          highest_weight = samples[j].y;
        }
      }
      if (highest_index != i) {
        float2 tmp = samples[i];
        samples[i] = samples[highest_index];
        samples[highest_index] = tmp;
      }
    }

    for (int p = 0; p < pass_len; p++) {
      imageStoreFast(accum_crypto_img,
                     int3(texel, base_slice + p),
                     float4(samples[p * 2], samples[p * 2 + 1]));
    }
  }
}
