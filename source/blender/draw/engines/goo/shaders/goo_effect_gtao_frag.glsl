/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_post_infos.hh"

FRAGMENT_SHADER_CREATE_INFO(goo_effect_gtao)

#include "goo_gtao_lib.glsl"

void main()
{
  int2 max_texel = max(textureSize(goo_depth_tx, 0) - int2(1), int2(0));
  int2 texel = clamp(int2(gl_FragCoord.xy), int2(0), max_texel);
  float depth = texelFetch(goo_depth_tx, texel, 0).r;
  float radius = goo_light_buf.gtao_params.x;
  if (depth >= 1.0f || radius <= 0.0f) {
    out_horizons = float4(0.0f);
    return;
  }

  float3 vP = drw_point_screen_to_view(float3(screen_uv, depth));
  int steps = int(round(mix(6.0f, 12.0f, clamp(goo_light_buf.gtao_params.z, 0.0f, 1.0f))));
  float4 horizons = goo_gtao_horizon_search(
      vP, gl_FragCoord.xy, goo_light_buf.sample_params.yz, goo_depth_tx, radius, 0.0f, steps);
  out_horizons = goo_gtao_pack_horizons(horizons);
}
