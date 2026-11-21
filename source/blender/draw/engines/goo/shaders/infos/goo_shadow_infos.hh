/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#ifdef GPU_SHADER
#  pragma once
#  include "gpu_shader_compat.hh"

#  include "draw_object_infos_infos.hh"
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

GPU_SHADER_CREATE_INFO(goo_shadow_id_mesh)
VERTEX_IN(0, float3, pos)
FRAGMENT_OUT(0, uint, out_shadow_id)
VERTEX_SOURCE("goo_shadow_id_vert.glsl")
FRAGMENT_SOURCE("goo_shadow_id_frag.glsl")
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat)
ADDITIONAL_INFO(draw_resource_id_varying)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_shadow_curves)
VERTEX_SOURCE("goo_shadow_curves_vert.glsl")
FRAGMENT_SOURCE("goo_shadow_frag.glsl")
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat)
ADDITIONAL_INFO(draw_curves)
ADDITIONAL_INFO(draw_curves_infos)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()

GPU_SHADER_CREATE_INFO(goo_shadow_id_curves)
FRAGMENT_OUT(0, uint, out_shadow_id)
VERTEX_SOURCE("goo_shadow_id_curves_vert.glsl")
FRAGMENT_SOURCE("goo_shadow_id_frag.glsl")
ADDITIONAL_INFO(draw_view)
ADDITIONAL_INFO(draw_modelmat)
ADDITIONAL_INFO(draw_resource_id_varying)
ADDITIONAL_INFO(draw_curves)
ADDITIONAL_INFO(draw_curves_infos)
DO_STATIC_COMPILATION()
GPU_SHADER_CREATE_END()
