/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "gpu_shader_math_base_lib.glsl"

[[node]]
void node_twirl(float3 vector, float3 center, float amount, float3 &result)
{
  /* Only use X/Y for 2D twirl, Z is passed through. */
  float uv_x = vector.x - center.x;
  float uv_y = vector.y - center.y;
  float radius = sqrt(uv_x * uv_x + uv_y * uv_y);
  float angle = atan2(uv_y, uv_x);
  angle += radius * amount;
  float shifted_x = cos(angle) * radius;
  float shifted_y = sin(angle) * radius;
  result.x = shifted_x + center.x;
  result.y = shifted_y + center.y;
  result.z = vector.z;
}
