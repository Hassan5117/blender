/* SPDX-FileCopyrightText: 2022-2023 Blender Authors
 * SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/* Domain types passed to the runtime generated `attrib_load()`. */

#pragma once

#include "gpu_shader_compat.hh"

struct MeshVertex {
  float3 lP;
  /* Needed for tangent transformation. */
  float3x3 world_to_object;
  /* Default derived from local position. */
  float3 orco_default;
};
