/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

[[node]]
void view_position_get(float3 &P)
{
#if defined(GOO_ENGINE) && !defined(GPU_VERTEX_SHADER)
  P = goo_view_position_get();
#else
  P = float3(0.0f);
#endif
}
