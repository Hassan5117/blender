/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

#include "goo_shader_shared.hh"
#include "gpu_shader_math_constants_lib.glsl"
#include "gpu_shader_math_safe_lib.glsl"
#include "gpu_shader_math_vector_safe_lib.glsl"

float2 goo_oct_dir_to_uv(float3 dir)
{
  float3 n = safe_normalize(dir);
  float l1 = abs(n.x) + abs(n.y) + abs(n.z);
  n /= max(l1, 1e-6f);
  if (n.z < 0.0f) {
    float2 sgn = step(0.0f, n.xy) * 2.0f - 1.0f;
    n.xy = (1.0f - abs(n.yx)) * sgn;
  }
  return n.xy * 0.5f + 0.5f;
}

float3 goo_oct_uv_to_dir(float2 uv)
{
  float2 f = uv * 2.0f - 1.0f;
  float3 n = float3(f.x, f.y, 1.0f - abs(f.x) - abs(f.y));
  if (n.z < 0.0f) {
    float2 sgn = step(0.0f, n.xy) * 2.0f - 1.0f;
    n.xy = (1.0f - abs(n.yx)) * sgn;
  }
  return safe_normalize(n);
}

float2 goo_oct_sample_uv(float2 oct_uv, float res)
{
  float safe_res = max(res, 2.0f);
  return clamp(oct_uv, float2(0.0f), float2(1.0f)) * ((safe_res - 1.0f) / safe_res) +
         float2(0.5f / safe_res);
}

GooSHL2 goo_sh_encode_dir(float3 d)
{
  GooSHL2 y;
  y.sh[0] = float4(0.282094792f);
  y.sh[1] = float4(-0.488602512f * d.y);
  y.sh[2] = float4(0.488602512f * d.z);
  y.sh[3] = float4(-0.488602512f * d.x);
  y.sh[4] = float4(1.092548431f * d.x * d.y);
  y.sh[5] = float4(-1.092548431f * d.y * d.z);
  y.sh[6] = float4(0.315391565f * (3.0f * d.z * d.z - 1.0f));
  y.sh[7] = float4(-1.092548431f * d.x * d.z);
  y.sh[8] = float4(0.546274215f * (d.x * d.x - d.y * d.y));
  return y;
}

float3 goo_eval_sh_lambert(GooSHL2 sh, float3 N)
{
  float3 n_n = safe_normalize(N);
  GooSHL2 y = goo_sh_encode_dir(n_n);
  float3 res = sh.sh[0].rgb * y.sh[0].x;
  res += (sh.sh[1].rgb * y.sh[1].x + sh.sh[2].rgb * y.sh[2].x + sh.sh[3].rgb * y.sh[3].x) *
         (2.0f / 3.0f);
  res += (sh.sh[4].rgb * y.sh[4].x + sh.sh[5].rgb * y.sh[5].x + sh.sh[6].rgb * y.sh[6].x +
          sh.sh[7].rgb * y.sh[7].x + sh.sh[8].rgb * y.sh[8].x) *
         0.25f;
  return max(res, float3(0.0f));
}
