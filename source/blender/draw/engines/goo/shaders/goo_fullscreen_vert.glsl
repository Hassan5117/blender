/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_post_infos.hh"

VERTEX_SHADER_CREATE_INFO(goo_fullscreen)

#include "gpu_shader_fullscreen_lib.glsl"

void main()
{
  fullscreen_vertex(gl_VertexID, gl_Position, screen_uv);
}
