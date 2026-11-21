/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_post_infos.hh"

FRAGMENT_SHADER_CREATE_INFO(goo_film_resolve)

void main()
{
  int2 max_texel = max(textureSize(source_tx, 0) - int2(1), int2(0));
  int2 src_texel = clamp(int2(gl_FragCoord.xy) + overscan_offset, int2(0), max_texel);
  out_color = texelFetch(source_tx, src_texel, 0);
}
