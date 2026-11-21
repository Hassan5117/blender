/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_indirect_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_env_convolve)

#include "goo_octahedral_lib.glsl"
#include "gpu_shader_math_constants_lib.glsl"
#include "gpu_shader_math_matrix_construct_lib.glsl"

float goo_radical_inverse_vdc(uint bits)
{
  bits = bitfieldReverse(bits);
  return float(bits) * 2.3283064365386963e-10f;
}

float3 goo_importance_sample_ggx(float2 xi, float a2, float3x3 basis)
{
  float phi = 2.0f * M_PI * xi.x;
  float cos_theta = sqrt(max((1.0f - xi.y) / max(1.0f + (a2 - 1.0f) * xi.y, 1e-6f), 0.0f));
  float sin_theta = sqrt(max(1.0f - cos_theta * cos_theta, 0.0f));
  float3 H_local = float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
  return normalize(basis * H_local);
}

void main()
{
  int2 texel = int2(gl_GlobalInvocationID.xy);
  if (any(greaterThanEqual(texel, int2(dst_res)))) {
    return;
  }

  float2 oct_uv = float2(texel) / float(max(dst_res - 1, 1));
  float3 N = goo_oct_uv_to_dir(oct_uv);
  float3 V = N;
  float3x3 basis = from_up_axis(N);

  float alpha = clamp(target_roughness * target_roughness, 0.02f, 1.0f);
  float a2 = alpha * alpha;

  const uint SAMPLE_COUNT = 128u;
  float3 prefiltered = float3(0.0f);
  float total_weight = 0.0f;
  float f_src_res = float(max(src_res, 2));

  for (uint i = 0u; i < SAMPLE_COUNT; i++) {
    float2 xi = float2(float(i) / float(SAMPLE_COUNT), goo_radical_inverse_vdc(i));
    float3 H = goo_importance_sample_ggx(xi, a2, basis);
    float3 L = normalize(2.0f * dot(V, H) * H - V);
    float ndotl = max(dot(N, L), 0.0f);
    if (ndotl > 0.0f) {
      float2 sample_uv = goo_oct_sample_uv(goo_oct_dir_to_uv(L), f_src_res);
      float3 rad = textureLod(env_src_tx, float3(sample_uv, float(layer)), 0.0f).rgb;
      prefiltered += rad * ndotl;
      total_weight += ndotl;
    }
  }

  float3 result = (total_weight > 1e-5f) ? (prefiltered / total_weight) : float3(0.0f);
  imageStoreFast(env_dst_img, int3(texel, layer), float4(result, 1.0f));
}
