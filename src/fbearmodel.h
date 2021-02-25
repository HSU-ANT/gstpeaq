/* GstPEAQ
 * Copyright (C) 2013, 2015, 2021 Martin Holters <martin.holters@hsu-hh.de>
 *
 * fbearmodel.h: Filer bank-based peripheral ear model part.
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

#ifndef __FBEARMODEL_H__
#define __FBEARMODEL_H__ 1

#include "earmodel.h"

#include <algorithm>
#include <array>
#include <complex>
#include <vector>

namespace peaq {

class FilterbankEarModel : public EarModelBase<FilterbankEarModel>
{
private:
  static constexpr std::size_t BUFFER_LENGTH = 1456;
  static constexpr std::size_t FB_NUMBANDS = 40;
  static constexpr double SLOPE_FILTER_A = 0.99335550625034; /* exp(-32 / (48000 * 0.1)) */
  static constexpr double DIST = 0.921851456499719; /* pow(0.1,(z[39]-z[0])/(39*20)) */
  static constexpr double CL = 0.0802581846102741;  /* pow(DIST, 31) */
  /* taken from Table 8 in [BS1387] */
  static constexpr std::array<std::size_t, FB_NUMBANDS> filter_length{
    1456, 1438, 1406, 1362, 1308, 1244, 1176, 1104, 1030, 956, 884, 814, 748, 686,
    626,  570,  520,  472,  430,  390,  354,  320,  290,  262, 238, 214, 194, 176,
    158,  144,  130,  118,  106,  96,   86,   78,   70,   64,  58,  52
  };

public:
  static constexpr std::size_t FRAME_SIZE = 192;
  static constexpr std::size_t STEP_SIZE = FRAME_SIZE;
  /* see section 3.3 in [BS1387], section 4.3 in [Kabal03] */
  static constexpr auto LOUDNESS_SCALE = 1.26539;
  /* see section 2.2.11 in [BS1387], section 3.7 in [Kabal03] */
  static constexpr auto TAU_MIN = 0.004;
  static constexpr auto TAU_100 = 0.020;
  struct state_t
  {
    [[nodiscard]] auto const& get_excitation() const { return excitation; }
    [[nodiscard]] auto const& get_unsmeared_excitation() const
    {
      return unsmeared_excitation;
    }

  private:
    friend class FilterbankEarModel;
    double hpfilter1_x1{};
    double hpfilter1_x2{};
    double hpfilter1_y1{};
    double hpfilter1_y2{};
    double hpfilter2_y1{};
    double hpfilter2_y2{};
    std::array<double, 2 * BUFFER_LENGTH> fb_buf{};
    unsigned int fb_buf_offset{};
    std::array<double, BUFFER_LENGTH> cu{};
    std::array<std::array<double, 11>, FB_NUMBANDS> E0_buf{};
    std::array<double, FB_NUMBANDS> excitation{};
    std::array<double, FB_NUMBANDS> unsmeared_excitation{};
  };
  static auto constexpr get_band_count() { return FB_NUMBANDS; }
  FilterbankEarModel();
  [[nodiscard]] double get_playback_level() const { return 20. * std::log10(level_factor); }
  void set_playback_level(double level)
  {
    /* scale factor for playback level; (27) in [BS1387], (34) in [Kabal03] */
    level_factor = std::pow(10.0, level / 20.0);
  }
  template<typename InputIterator>
  void process_block(state_t& state, InputIterator samples) const
  {
    for (std::size_t k = 0; k < FRAME_SIZE; k++) {
      /* setting of playback level; 2.2.3 in [BS1387], 3 in [Kabal03] */
      auto scaled_input = samples[k] * level_factor;

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

private:
  double level_factor;
  std::array<std::vector<std::complex<double>>, 40> fbh;

  static const std::array<double, 6> back_mask_h;
  [[nodiscard]] auto apply_filter_bank(state_t const& state) const
    -> std::array<std::complex<double>, FB_NUMBANDS>;
};
} // namespace peaq

/**
 * SECTION:fbearmodel
 * @short_description: Filter-bank based ear model.
 * @title: PeaqFilterbankEarModel
 *
 * The processing is performed by calling peaq_earmodel_process_block(). The
 * first step is to
 * apply a DC rejection filter (high pass at 20 Hz) and decompose the signal
 * into 40 bands using an FIR filter bank. After weighting the individual bands
 * with the outer and middle ear filter, the signal energy in spread accross
 * frequency and time. Addition of the internal noise then yields the unsmeared
 * excitation patterns. Another time domain spreading finally gives the
 * excitation patterns.
 */

#endif
