/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#ifdef GPU_SHADER
#  pragma once
#  include "gpu_shader_compat.hh"

#  include "draw_view_infos.hh"
#endif

#include "gpu_shader_create_info.hh"

GPU_SHADER_CREATE_INFO(goo_shadow_mesh)
VERTEX_IN(0, float3, pos)
VERTEX_SOURCE("goo_shadow_vert.glsl")
FRAGMENT_SOURCE("goo_shadow_frag.glsl")
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()
