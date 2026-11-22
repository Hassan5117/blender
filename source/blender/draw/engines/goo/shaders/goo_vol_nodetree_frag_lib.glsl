/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once
/* Replaced at runtime by `goo_material.cc` for volume materials. Placeholder for linting. */
#pragma runtime_generated

#include "goo_geom_types_lib.glsl"
#include "goo_volume_nodetree_lib.glsl"

#ifndef NODETREE_FUNCTIONS /* Needed for linting. */
#  define NODETREE_FUNCTIONS

void attrib_load(VolumePoint /*domain*/) {}

Closure nodetree_volume()
{
  return Closure(0);
}

#endif
