/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_effects_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_dof_dilate)

void main()
{
  int2 tile = int2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(tile, tile_extent))) {
    return;
  }

  float4 best = texelFetch(src_tile_tx, tile, 0);
  int r = clamp(radius_tiles, 0, 8);
  for (int i = -r; i <= r; i++) {
    int2 t = clamp(tile + dir_step * i, int2(0), tile_extent - int2(1));
    float4 s = texelFetch(src_tile_tx, t, 0);
    best.x = max(best.x, s.x);
    best.y = max(best.y, s.y);
  }
  imageStoreFast(dst_tile_img, tile, best);
}
