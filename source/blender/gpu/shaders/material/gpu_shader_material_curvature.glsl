/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

[[node]]
void node_screenspace_curvature(float iiterations,
                                float sample_scale,
                                float clamp_dist,
                                float3 scale,
                                float &scene_curvature,
                                float &scene_rim)
{
#if defined(GOO_ENGINE) && !defined(GPU_VERTEX_SHADER)
  goo_screenspace_curvature(
      iiterations, sample_scale, clamp_dist, scale, scene_curvature, scene_rim);
#else
  scene_curvature = 0.0f;
  scene_rim = 0.0f;
#endif
}
