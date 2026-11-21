/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_indirect_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_color_downsample_array)

void main()
{
  int2 texel = int2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(texel, dst_extent))) {
    return;
  }

  int2 base = texel * 2;
  float4 sum = float4(0.0f);
  for (int dy = 0; dy < 2; dy++) {
    for (int dx = 0; dx < 2; dx++) {
      int2 p = clamp(base + int2(dx, dy), int2(0), src_extent - int2(1));
      sum += texelFetch(src_tx, int3(p, layer), 0);
    }
  }
  imageStoreFast(dst_img, int3(texel, layer), sum * 0.25f);
}
