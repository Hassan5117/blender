/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

[[node]]
void node_shader_info(float3 position,
                      float3 normal,
                      float4 &half_light,
                      float &shadows,
                      float &self_shadows,
                      float4 &ambient,
                      float &half_lambert)
{
#if defined(GOO_ENGINE) && !defined(GPU_VERTEX_SHADER)
  goo_calc_shader_info(
      position, normal, half_light, shadows, self_shadows, ambient, half_lambert);
#else
  half_light = float4(0.0f);
  shadows = 1.0f;
  self_shadows = 1.0f;
  ambient = float4(0.0f);
  half_lambert = 0.0f;
#endif
}

[[node]]
void node_shader_info_light_groups(float3 position,
                                   float3 normal,
                                   float4 light_groups,
                                   float4 light_group_shadows,
                                   float4 &half_light,
                                   float &shadows,
                                   float &self_shadows,
                                   float4 &ambient,
                                   float &half_lambert)
{
#if defined(GOO_ENGINE) && !defined(GPU_VERTEX_SHADER)
  goo_calc_shader_info(position,
                       normal,
                       floatBitsToInt(light_groups),
                       floatBitsToInt(light_group_shadows),
                       half_light,
                       shadows,
                       self_shadows,
                       ambient,
                       half_lambert);
#else
  half_light = float4(0.0f);
  shadows = 1.0f;
  self_shadows = 1.0f;
  ambient = float4(0.0f);
  half_lambert = 0.0f;
#endif
}
