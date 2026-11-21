/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_indirect_infos.hh"

COMPUTE_SHADER_CREATE_INFO(goo_cube_to_sh)

#include "goo_octahedral_lib.glsl"
#include "gpu_shader_math_constants_lib.glsl"

shared float4 s_sh[64][9];

void goo_cube_face_basis(int face, float3 &fwd, float3 &right, float3 &up)
{
  /* Standard cube orientation matching cubemap direction lookup. */
  if (face == 0) {
    fwd = float3(1.0f, 0.0f, 0.0f);
    right = float3(0.0f, 0.0f, -1.0f);
    up = float3(0.0f, -1.0f, 0.0f);
  }
  else if (face == 1) {
    fwd = float3(-1.0f, 0.0f, 0.0f);
    right = float3(0.0f, 0.0f, 1.0f);
    up = float3(0.0f, -1.0f, 0.0f);
  }
  else if (face == 2) {
    fwd = float3(0.0f, 1.0f, 0.0f);
    right = float3(1.0f, 0.0f, 0.0f);
    up = float3(0.0f, 0.0f, 1.0f);
  }
  else if (face == 3) {
    fwd = float3(0.0f, -1.0f, 0.0f);
    right = float3(1.0f, 0.0f, 0.0f);
    up = float3(0.0f, 0.0f, -1.0f);
  }
  else if (face == 4) {
    fwd = float3(0.0f, 0.0f, 1.0f);
    right = float3(1.0f, 0.0f, 0.0f);
    up = float3(0.0f, -1.0f, 0.0f);
  }
  else {
    fwd = float3(0.0f, 0.0f, -1.0f);
    right = float3(-1.0f, 0.0f, 0.0f);
    up = float3(0.0f, -1.0f, 0.0f);
  }
}

void main()
{
  uint tid = gl_LocalInvocationIndex;
  int dim = max(grid_dim, 4);
  int face_texels = dim * dim;
  int total_texels = 6 * face_texels;
  float texel_size = 2.0f / float(dim);

  float4 local_sh[9];
  for (int k = 0; k < 9; k++) {
    local_sh[k] = float4(0.0f);
  }
  float weight_sum = 0.0f;

  for (int idx = int(tid); idx < total_texels; idx += 64) {
    int face = idx / face_texels;
    int rem = idx - face * face_texels;
    int iy = rem / dim;
    int ix = rem - iy * dim;

    float u = ((float(ix) + 0.5f) / float(dim)) * 2.0f - 1.0f;
    float v = ((float(iy) + 0.5f) / float(dim)) * 2.0f - 1.0f;

    float3 fwd;
    float3 right;
    float3 up;
    goo_cube_face_basis(face, fwd, right, up);
    float3 dir = normalize(fwd + right * u + up * v);

    float dist2 = 1.0f + u * u + v * v;
    float d_omega = (texel_size * texel_size) / (dist2 * sqrt(dist2));

    float3 col = max(texture(capture_cube_tx, dir).rgb, float3(0.0f));
    if (indirect_clamp > 0.0f) {
      float lum = max(col.r, max(col.g, col.b));
      if (lum > indirect_clamp) {
        col *= (indirect_clamp / lum);
      }
    }

    GooSHL2 basis = goo_sh_encode_dir(dir);
    float3 weighted = col * d_omega;
    for (int k = 0; k < 9; k++) {
      local_sh[k].rgb += weighted * basis.sh[k].x;
    }
    weight_sum += d_omega;
  }

  local_sh[0].w = weight_sum;
  for (int k = 0; k < 9; k++) {
    s_sh[tid][k] = local_sh[k];
  }

  for (uint stride = 32u; stride > 0u; stride >>= 1u) {
    barrier();
    if (tid < stride) {
      for (int k = 0; k < 9; k++) {
        s_sh[tid][k] += s_sh[tid + stride][k];
      }
    }
  }
  barrier();

  if (tid == 0u) {
    float total_omega = max(s_sh[0][0].w, 1e-5f);
    float norm = (4.0f * M_PI) / total_omega;
    for (int k = 0; k < 9; k++) {
      out_sh_buf[dst_index].sh[k] = float4(s_sh[0][k].rgb * norm, 0.0f);
    }
  }
}
