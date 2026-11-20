/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Mesh/object matrix and info accessors for `goo_nodetree_lib.glsl`. */

#pragma once

#include "draw_model_lib.glsl"
#include "draw_object_infos_lib.glsl"

ObjectMatrices object_matrices_get()
{
  ObjectMatrices m;
  m.model = drw_modelmat();
  m.model_inverse = drw_modelinv();
  return m;
}

ObjectInfos object_infos_get()
{
  return drw_object_infos();
}
