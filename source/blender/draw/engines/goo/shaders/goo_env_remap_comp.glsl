/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_indirect_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_env_remap)

#include "goo_octahedral_lib.glsl"

void main()
{
  int2 texel = int2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(texel, int2(dst_res)))) {
    return;
  }

  float2 oct_uv = float2(texel) / float(max(dst_res - 1, 1));
  float3 dir = goo_oct_uv_to_dir(oct_uv);
  float3 col = max(texture(capture_cube_tx, dir).rgb, float3(0.0f));
  if (indirect_clamp > 0.0f) {
    float lum = max(col.r, max(col.g, col.b));
    if (lum > indirect_clamp) {
      col *= (indirect_clamp / lum);
    }
  }
  imageStoreFast(env_mip0_img, int3(texel, dst_layer), float4(col, 1.0f));
}
