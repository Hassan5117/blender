/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_effects_infos.hh"

FRAGMENT_SHADER_CREATE_INFO(goo_velocity_mesh)

void main()
{
  float2 curr_uv = (vel_interp.curr_clip.xy / max(abs(vel_interp.curr_clip.w), 1e-6f)) * 0.5f + 0.5f;
  float2 prev_uv = (vel_interp.prev_clip.xy / max(abs(vel_interp.prev_clip.w), 1e-6f)) * 0.5f + 0.5f;
  float2 next_uv = (vel_interp.next_clip.xy / max(abs(vel_interp.next_clip.w), 1e-6f)) * 0.5f + 0.5f;
  out_velocity = float4(prev_uv - curr_uv, next_uv - curr_uv);
}
