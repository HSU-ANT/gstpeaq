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
  auto fc_array = std::vector<double>(FB_NUMBANDS);

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

  set_bands(fc_array);
  set_playback_level(92);
}

auto FilterbankEarModel::apply_filter_bank(FilterbankEarModel::state_t const& state) const
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

void FilterbankEarModel::process_block(FilterbankEarModel::state_t& state,
                                       std::array<double, FRAME_SIZE>& sample_data) const
{
  for (std::size_t k = 0; k < FRAME_SIZE; k++) {
    /* setting of playback level; 2.2.3 in [BS1387], 3 in [Kabal03] */
    auto scaled_input = sample_data[k] * level_factor;

    /* DC rejection filter; 2.2.4 in [BS1387], 3.1 in [Kabal03] */
    auto hpfilter1_out = scaled_input - 2. * state.hpfilter1_x1 + state.hpfilter1_x2 +
                         1.99517 * state.hpfilter1_y1 - 0.995174 * state.hpfilter1_y2;
    auto hpfilter2_out = hpfilter1_out - 2. * state.hpfilter1_y1 + state.hpfilter1_y2 +
                         1.99799 * state.hpfilter2_y1 - 0.997998 * state.hpfilter2_y2;
    state.hpfilter1_x2 = state.hpfilter1_x1;
    state.hpfilter1_x1 = scaled_input;
    state.hpfilter1_y2 = state.hpfilter1_y1;
    state.hpfilter1_y1 = hpfilter1_out;
    state.hpfilter2_y2 = state.hpfilter2_y1;
    state.hpfilter2_y1 = hpfilter2_out;

    /* Filter bank; 2.2.5 in [BS1387], 3.2 in [Kabal03]; include outer and
     * middle ear filtering; 2.2.6 in [BS1387] 3.3 in [Kabal03] */
    if (state.fb_buf_offset == 0) {
      state.fb_buf_offset = BUFFER_LENGTH;
    }
    state.fb_buf_offset--;
    /* filterbank input is stored twice s.t. starting at fb_buf_offset there
     * are always at least BUFFER_LENGTH samples of past data available */
    state.fb_buf[state.fb_buf_offset] = hpfilter2_out;
    state.fb_buf[state.fb_buf_offset + BUFFER_LENGTH] = hpfilter2_out;
    if (k % 32 == 0) {
      auto fb_out = apply_filter_bank(state);
      auto A = fb_out;

      /* frequency domain spreading; 2.2.7 in [BS1387], 3.4 in [Kabal03] */
      for (std::size_t band = 0; band < FB_NUMBANDS; band++) {
        auto fc = get_band_center_frequency(band);
        auto L = 10 * std::log10(std::norm(fb_out[band]));
        auto s = std::max(4.0, 24.0 + 230.0 / fc - 0.2 * L);
        auto dist_s = std::pow(DIST, s);
        /* a and b=1-a are probably swapped in the standard's pseudo code */
#if defined(SWAP_SLOPE_FILTER_COEFFICIENTS) && SWAP_SLOPE_FILTER_COEFFICIENTS
        state.cu[band] = dist_s + SLOPE_FILTER_A * (state.cu[band] - dist_s);
#else
        state.cu[band] = state.cu[band] + SLOPE_FILTER_A * (dist_s - state.cu[band]);
#endif
        auto d = fb_out[band];
        for (auto j = band + 1; j < FB_NUMBANDS; j++) {
          d *= state.cu[band];
          A[j] += d;
        }
      }

      for (auto band = FB_NUMBANDS - 1; band > 0; band--) {
        A[band - 1] += CL * A[band];
      }

      /* rectification; 2.2.8. in [BS1387], part of 3.4 in [Kabal03] */
      std::array<double, FB_NUMBANDS> E0;
      std::transform(cbegin(A),
                     cend(A),
                     begin(E0),
                     static_cast<double (*)(const std::complex<double>&)>(std::norm));

      /* time domain smearing (1) - backward masking; 2.2.9 in [BS1387], 3.5 in
       * [Kabal03] */
      for (std::size_t band = 0; band < FB_NUMBANDS; band++) {
        std::move(begin(state.E0_buf[band]),
                  end(state.E0_buf[band]) - 1,
                  begin(state.E0_buf[band]) + 1);
        state.E0_buf[band][0] = E0[band];
      }
    }
  }
  std::array<double, FB_NUMBANDS> E1;
  for (std::size_t band = 0; band < FB_NUMBANDS; band++) {
    E1[band] = 0.;
    /* exploit symmetry */
    for (int i = 0; i < 5; i++) {
      E1[band] += (state.E0_buf[band][i] + state.E0_buf[band][10 - i]) * back_mask_h[i];
    }
    /* include term for n=N/2 only once */
    E1[band] += state.E0_buf[band][5] * back_mask_h[5];

    /* adding of internal noise; 2.2.10 in [BS1387], 3.6 in [Kabal03] */
    auto EThres = get_internal_noise(band);
    state.unsmeared_excitation[band] = E1[band] + EThres;

    /* time domain smearing (2) - forward masking; 2.2.11 in [BS1387], 3.7 in
     * [Kabal03] */
    auto a = get_ear_time_constant(band);

    state.excitation[band] =
      a * state.excitation[band] + (1. - a) * state.unsmeared_excitation[band];
  }
}

} // namespace peaq

PeaqEarModel* peaq_filterbankearmodel_new()
{
  return new PeaqFilterbankEarModel();
}