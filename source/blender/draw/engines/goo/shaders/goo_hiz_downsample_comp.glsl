/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_indirect_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_hiz_downsample)

void main()
{
  int2 texel = int2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(texel, dst_extent))) {
    return;
  }

  int2 base = texel * 2;
  int max_dx = ((src_extent.x & 1) != 0 && texel.x == dst_extent.x - 1) ? 3 : 2;
  int max_dy = ((src_extent.y & 1) != 0 && texel.y == dst_extent.y - 1) ? 3 : 2;

  float min_d = 1.0f;
  for (int dy = 0; dy < max_dy; dy++) {
    for (int dx = 0; dx < max_dx; dx++) {
      int2 p = min(base + int2(dx, dy), src_extent - int2(1));
      min_d = min(min_d, texelFetch(src_hiz_tx, p, 0).r);
    }
  }
  imageStoreFast(dst_hiz_img, texel, float4(min_d));
}
