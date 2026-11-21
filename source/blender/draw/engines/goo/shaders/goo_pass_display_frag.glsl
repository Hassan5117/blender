/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_post_infos.hh"

FRAGMENT_SHADER_CREATE_INFO(goo_pass_display)

float4 goo_cryptomatte_false_color(float hash)
{
  uint m3hash = floatBitsToUint(hash);
  return float4(hash,
                float(m3hash << 8) / float(0xFFFFFFFFu),
                float(m3hash << 16) / float(0xFFFFFFFFu),
                1.0f);
}

void main()
{
  int2 tex_size = max(textureSize(accum_color_tx, 0).xy, int2(1));
  if (display_mode == GOO_DISPLAY_MODE_VALUE_PASS || display_mode == GOO_DISPLAY_MODE_DEPTH_PASS) {
    tex_size = max(textureSize(accum_value_tx, 0).xy, int2(1));
  }
  else if (display_mode == GOO_DISPLAY_MODE_CRYPTO_PASS) {
    tex_size = max(textureSize(accum_crypto_tx, 0).xy, int2(1));
  }

  int2 texel = clamp(int2(gl_FragCoord.xy), int2(0), tex_size - int2(1));

  if (display_mode == GOO_DISPLAY_MODE_COLOR_PASS) {
    float4 c = texelFetch(accum_color_tx, int3(texel, max(display_layer, 0)), 0);
    out_color = float4(c.rgb, 1.0f);
  }
  else if (display_mode == GOO_DISPLAY_MODE_VALUE_PASS) {
    float v = texelFetch(accum_value_tx, int3(texel, max(display_layer, 0)), 0).r;
    out_color = float4(float3(v), 1.0f);
  }
  else if (display_mode == GOO_DISPLAY_MODE_DEPTH_PASS) {
    float z = texelFetch(accum_value_tx, int3(texel, max(display_layer, 0)), 0).r;
    float d = (z >= 1e9f) ? 0.0f : (1.0f / (1.0f + max(z, 0.0f) * max(depth_scale, 1e-4f)));
    out_color = float4(float3(d), 1.0f);
  }
  else if (display_mode == GOO_DISPLAY_MODE_CRYPTO_PASS) {
    float2 sample0 = texelFetch(accum_crypto_tx, int3(texel, max(display_layer, 0)), 0).rg;
    out_color = (sample0.y > 0.0f) ? goo_cryptomatte_false_color(sample0.x) : float4(0.0f, 0.0f, 0.0f, 1.0f);
  }
  else {
    out_color = float4(0.0f, 0.0f, 0.0f, 1.0f);
  }
}
