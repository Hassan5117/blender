/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_indirect_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_color_copy)

void main()
{
  int2 texel = int2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(texel, extent))) {
    return;
  }
  float4 col = texelFetch(src_tx, texel, 0);
  imageStoreFast(dst_img, texel, col);
}
