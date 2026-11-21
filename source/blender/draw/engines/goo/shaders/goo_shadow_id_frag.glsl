/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "infos/goo_shadow_infos.hh"

FRAGMENT_SHADER_CREATE_INFO(goo_shadow_id_mesh)

#include "draw_model_lib.glsl"

void main()
{
  out_shadow_id = drw_resource_id() + 1u;
}
