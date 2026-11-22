/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Screen-space Separable / Burley Subsurface Scattering (SSS) G-buffer & resolve pass.
 */

#pragma once

#include "DRW_gpu_wrapper.hh"
#include "draw_manager.hh"
#include "draw_pass.hh"
#include "draw_view.hh"

#include "goo_defines.hh"

namespace blender::goo {

using namespace draw;

class ProbeModule;

class SssModule {
 private:
  Texture dummy_sss_tx_ = {"goo.dummy_sss_tx"};
  Texture sss_data_tx_ = {"goo.sss_data_tx"};

  gpu::Texture *sss_write_ptr_ = nullptr;
  gpu::Texture *resolve_sss_ptr_ = nullptr;
  gpu::Texture *resolve_depth_ptr_ = nullptr;
  gpu::Texture *resolve_color_ptr_ = nullptr;

  int2 unf_extent_ = int2(1);
  int unf_sample_index_ = 0;
  int3 dispatch_size_ = int3(1);

  PassSimple resolve_ps_ = {"Goo.SssResolve"};
  bool has_sss_materials_ = false;

 public:
  void begin_sync(ProbeModule &probes);

  void mark_material_uses_sss()
  {
    has_sss_materials_ = true;
  }

  void end_sync(int2 render_extent);

  bool enabled() const
  {
    return has_sss_materials_;
  }

  template<typename PassType> void bind_write_image(PassType &pass)
  {
    pass.bind_image(GOO_SSS_DATA_IMG_SLOT, &sss_write_ptr_);
  }

  template<typename PassType> void bind_dummy_image(PassType &pass)
  {
    pass.bind_image(GOO_SSS_DATA_IMG_SLOT, dummy_sss_tx_);
  }

  void clear_gbuffer();

  void resolve(Manager &manager,
               View &view,
               gpu::Texture *depth_tx,
               gpu::Texture *color_tx,
               int sample_index,
               int2 extent);
};

}  // namespace blender::goo
