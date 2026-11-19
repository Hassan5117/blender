/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#ifdef GPU_SHADER
#  pragma once

#  include "draw_object_infos_infos.hh"
#  include "draw_view_infos.hh"
#endif

#include "gpu_shader_create_info.hh"

/* -------------------------------------------------------------------- */
/** \name Phase-0 flat surface shader (engine plumbing test).
 * \{ */

GPU_SHADER_INTERFACE_INFO(goo_surface_flat_iface)
SMOOTH(float3, normal_ws)
SMOOTH(float3, position_ws)
GPU_SHADER_INTERFACE_END()

GPU_SHADER_CREATE_INFO(goo_surface_flat)
VERTEX_IN(0, float3, pos)
VERTEX_IN(1, float3, nor)
VERTEX_OUT(goo_surface_flat_iface)
FRAGMENT_OUT(0, float4, out_color)
PUSH_CONSTANT(float4, base_color)
PUSH_CONSTANT(float4, world_color)
VERTEX_SOURCE("goo_surface_flat_vert.glsl")
FRAGMENT_SOURCE("goo_surface_flat_frag.glsl")
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

/** \} */
