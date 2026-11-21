/* SPDX-FileCopyrightText: 2026 Goo Engine port contributors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw_engine
 *
 * Scene color pyramid, Hi-Z depth pyramid, and Screen-Space Reflections (SSR) trace/resolve.
 */

#pragma once

#include "DRW_gpu_wrapper.hh"
#include "draw_manager.hh"
#include "draw_pass.hh"
#include "draw_view.hh"

#include "goo_defines.hh"

namespace blender::goo {

using namespace draw;

class LightModule;
class ProbeModule;

class SsrModule {
 private:
  Texture hiz_depth_tx_ = {"goo.hiz_depth_tx"};
  Texture ssr_data_tx_ = {"goo.ssr_data_tx"};
  Texture ssr_spec_tx_ = {"goo.ssr_spec_tx"};
  Texture ssr_fallback_tx_ = {"goo.ssr_fallback_tx"};
  Texture ssr_hit_tx_ = {"goo.ssr_hit_tx"};
  Texture ssr_out_tx_ = {"goo.ssr_out_tx"};
  Texture dummy_ssr_tx_ = {"goo.dummy_ssr_tx"};

  gpu::Texture *ssr_data_ptr_ = nullptr;
  gpu::Texture *ssr_spec_ptr_ = nullptr;
  gpu::Texture *ssr_fallback_ptr_ = nullptr;

  PassSimple color_copy_ps_ = {"Goo.ColorCopy"};
  PassSimple color_downsample_ps_ = {"Goo.ColorDownsample"};
  PassSimple hiz_copy_ps_ = {"Goo.HiZCopy"};
  PassSimple hiz_downsample_ps_ = {"Goo.HiZDownsample"};
  PassSimple ssr_trace_ps_ = {"Goo.SsrTrace"};
  PassSimple ssr_resolve_ps_ = {"Goo.SsrResolve"};

  gpu::Texture *unf_src_tx_ = nullptr;
  gpu::Texture *unf_dst_img_ = nullptr;
  gpu::Texture *unf_depth_src_tx_ = nullptr;
  gpu::Texture *unf_hiz_tx_ = nullptr;
  gpu::Texture *unf_hit_tx_ = nullptr;
  int2 unf_extent_ = int2(1);
  int2 unf_src_extent_ = int2(1);
  int2 unf_dst_extent_ = int2(1);
  int2 unf_trace_extent_ = int2(1);
  int unf_half_res_ = 0;
  int unf_color_mips_ = 1;
  int3 unf_copy_dispatch_ = int3(1);
  int3 unf_down_dispatch_ = int3(1);
  int3 unf_hiz_copy_dispatch_ = int3(1);
  int3 unf_hiz_down_dispatch_ = int3(1);
  int3 unf_trace_dispatch_ = int3(1);
  int3 unf_resolve_dispatch_ = int3(1);

 public:
  static int safe_mip_count(int2 extent, int max_mips);

  void init_buffers(int2 render_extent, bool need_gbuffer, ProbeModule &probes, LightModule &lights);
  void clear_gbuffer(bool ssr_active);

  void build_color_pyramid(Manager &manager,
                           gpu::Texture *src_tx,
                           Texture &dst_pyramid_tx,
                           int2 extent);

  void build_hiz_pyramid(Manager &manager, gpu::Texture *prepass_depth_tx, int2 extent);

  void trace_and_resolve(Manager &manager,
                         View &sample_view,
                         gpu::Texture *prepass_depth_tx,
                         Texture &scene_color_pyramid_tx,
                         gpu::Texture *dst_color_tx,
                         ProbeModule &probes,
                         int2 extent);

  template<typename PassType> void bind_write_images(PassType &pass)
  {
    pass.bind_image(GOO_SSR_DATA_IMG_SLOT, &ssr_data_ptr_);
    pass.bind_image(GOO_SSR_SPEC_IMG_SLOT, &ssr_spec_ptr_);
    pass.bind_image(GOO_SSR_FALLBACK_IMG_SLOT, &ssr_fallback_ptr_);
  }

  template<typename PassType> void bind_dummy_images(PassType &pass)
  {
    pass.bind_image(GOO_SSR_DATA_IMG_SLOT, dummy_ssr_tx_);
    pass.bind_image(GOO_SSR_SPEC_IMG_SLOT, dummy_ssr_tx_);
    pass.bind_image(GOO_SSR_FALLBACK_IMG_SLOT, dummy_ssr_tx_);
  }
};

}  // namespace blender::goo
