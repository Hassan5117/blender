/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_effects_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_mblur_dilate)

void main()
{
  int2 tile = int2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(tile, tile_extent))) {
    return;
  }

  float4 best = texelFetch(src_tile_tx, tile, 0);
  int r = clamp(radius_tiles, 1, 6);
  for (int dy = -r; dy <= r; dy++) {
    for (int dx = -r; dx <= r; dx++) {
      int2 t = clamp(tile + int2(dx, dy), int2(0), tile_extent - int2(1));
      float4 s = texelFetch(src_tile_tx, t, 0);
      if (dot(s.xy, s.xy) > dot(best.xy, best.xy)) {
        best.xy = s.xy;
      }
      if (dot(s.zw, s.zw) > dot(best.zw, best.zw)) {
        best.zw = s.zw;
      }
    }
  }
  imageStoreFast(dst_tile_img, tile, best);
}
