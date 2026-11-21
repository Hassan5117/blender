/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_post_infos.hh"

FRAGMENT_SHADER_CREATE_INFO(goo_film_crop_depth)

void main()
{
  int2 max_texel = max(textureSize(depth_tx, 0) - int2(1), int2(0));
  int2 src_texel = clamp(int2(gl_FragCoord.xy) + overscan_offset, int2(0), max_texel);
  gl_FragDepth = texelFetch(depth_tx, src_texel, 0).r;
}
