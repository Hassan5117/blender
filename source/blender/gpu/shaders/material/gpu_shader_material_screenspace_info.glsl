/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

[[node]]
void node_screenspace_info(float3 viewPos, float4 &scene_col, float &scene_depth)
{
#if defined(GOO_ENGINE) && !defined(GPU_VERTEX_SHADER)
  goo_screenspace_info(viewPos, scene_col, scene_depth);
#else
  scene_col = float4(0.0f);
  scene_depth = 0.0f;
#endif
}
