/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_effects_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_volume_clear)

void main()
{
  int3 coord = int3(gl_GlobalInvocationID.xyz);
  if (any(greaterThanEqual(coord, goo_vol_buf.froxel_size.xyz))) {
    return;
  }
  imageStoreFast(vol_scatter_img, coord, float4(0.0f));
  imageStoreFast(vol_extinct_img, coord, float4(0.0f));
  imageStoreFast(vol_emissive_img, coord, float4(0.0f));
}
