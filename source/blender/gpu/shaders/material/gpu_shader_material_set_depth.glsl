/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

[[node]]
void goo_view_z_get(float &z)
{
#if defined(GOO_ENGINE) && !defined(GPU_VERTEX_SHADER)
  z = goo_view_position_get().z;
#else
  z = 0.0f;
#endif
}

[[node]]
void node_set_depth(Closure _in, float z_in, Closure &_out)
{
  _out = _in;
#if defined(GOO_ENGINE) && !defined(GPU_VERTEX_SHADER)
  goo_set_depth(z_in);
#endif
}
