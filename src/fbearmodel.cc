/* GstPEAQ
 * Copyright (C) 2013, 2015, 2021 Martin Holters <martin.holters@hsu-hh.de>
 *
 * fbearmodel.c: Filter bank-based peripheral ear model part.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "fbearmodel.h"
#include "settings.h"

#include <tuple>

namespace peaq {

const std::array<double, 6> FilterbankEarModel::back_mask_h = [] {
  /* precompute coefficients of the backward masking filter, see section 2.2.9
   * in [BS1387] and section 3.5 in [Kabal03]; due to symmetry, storing the
   * first six coefficients is sufficient */
  auto h = std::array<double, 6>{};
  for (int i = 0; i < 6; i++) {
    h[i] = std::cos(M_PI * (i - 5.) / 12.) * std::cos(M_PI * (i - 5.) / 12.) * 0.9761 / 6.;
  }
  return h;
}();

FilterbankEarModel::FilterbankEarModel()
{
  auto fc_array = std::array<double, FB_NUMBANDS>{};

  /* precompute filter bank impulse responses */
  for (std::size_t band = 0; band < FB_NUMBANDS; band++) {
    /* use (36) and (37) from [Kabal03] to determine the center frequencies
     * instead of the tabulated values from [BS1387] */
    auto fc =
      std::sinh((std::asinh(50.0 / 650.0) +
                 band * (std::asinh(18000.0 / 650.0) - std::asinh(50.0 / 650.0)) / 39.0)) *
      650.0;
    fc_array[band] = fc;
    auto N = filter_length[band];
    /* include outer and middle ear filtering in filter bank coefficients */
    auto Wt = calc_ear_weight(fc);
    /* due to symmetry, it is sufficient to compute the first half of the
     * coefficients */
    fbh[band].resize(N / 2 + 1);
    for (std::size_t n = 0; n < fbh[band].size(); n++) {
      /* (29) in [BS1387], (39) and (38) in [Kabal03] */
      auto win = 4. / N * std::sin(M_PI * n / N) * std::sin(M_PI * n / N) * Wt;
      auto phi = 2 * M_PI * fc * (n - N / 2.) / 48000.;
      fbh[band][n] = std::polar(win, phi);
    }
  }

  precompute_constants(fc_array, LOUDNESS_SCALE, TAU_MIN, TAU_100, STEP_SIZE);
  set_playback_level(92);
}

auto FilterbankEarModel::apply_filter_bank(FilterbankEarModel::state_t const& state) const
  -> std::array<std::complex<double>, FB_NUMBANDS>
{
  auto fb_out = std::array<std::complex<double>, FB_NUMBANDS>{};
  for (std::size_t band = 0; band < FB_NUMBANDS; band++) {
    auto N = filter_length[band];
    /* additional delay, (31) in [BS1387] */
    auto D = 1 + (filter_length[0] - N) / 2;
    auto out = std::complex<double>{};
    /* exploit symmetry in filter responses */
    auto N_2 = N / 2;
    const auto* in1 = cbegin(state.fb_buf) + D + state.fb_buf_offset;
    const auto* in2 = cbegin(state.fb_buf) + D + N + state.fb_buf_offset;
    auto h = cbegin(fbh[band]);
    /* first filter coefficient is zero, so skip it */
    for (size_t n = 1; n < N_2; n++) {
      h++;
      in1++;
      in2--;
      out += std::complex<double>{ (*in1 + *in2) * std::real(*h),   /* even symmetry */
                                   (*in1 - *in2) * std::imag(*h) }; /* odd symmetry */
    }
    /* include term for n=N/2 only once */
    in1++;
    h++;
    out += *in1 * *h;
    fb_out[band] = out;
  }
  return fb_out;
}

} // namespace peaq