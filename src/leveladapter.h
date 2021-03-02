/* GstPEAQ
 * Copyright (C) 2006, 2012, 2013, 2015, 2021
 * Martin Holters <martin.holters@hsu-hh.de>
 *
 * leveladapter.h: Level and pattern adaptation.
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

#ifndef __LEVELADAPTER_H__
#define __LEVELADAPTER_H__ 1

/**
 * SECTION:leveladapter
 * @short_description: Level and pattern adaptation.
 * @title: PeaqLevelAdapter
 *
 * #PeaqLevelAdapter encapsulates the level and pattern adaptation described in
 * section 3.1 of <xref linkend="BS1387" />. It estimates the per-band level
 * differences between reference and test signal and adapts them to each other
 * to compensate level differences and linear distortions.
 */

#include "earmodel.h"

#include <array>

namespace peaq {
template<std::size_t BANDCOUNT>
class LevelAdapter
{
public:
  struct state_t
  {
#if 0
    state_t() {
      /* [Kabal03] suggests initialization to 1, although the standard does not
       * mention it; there seems to be almost no difference on conformance, though
       */
      std::fill(begin(pattcorr_ref), end(pattcorr_ref), 1.0);
      std::fill(begin(pattcorr_test), end(pattcorr_test), 1.0);
    }
#endif
    [[nodiscard]] auto const& get_adapted_ref() const
    {
      return spectrally_adapted_ref_patterns;
    }
    [[nodiscard]] auto const& get_adapted_test() const
    {
      return spectrally_adapted_test_patterns;
    }

  private:
    friend class LevelAdapter;
    std::array<double, BANDCOUNT> ref_filtered_excitation{};
    std::array<double, BANDCOUNT> test_filtered_excitation{};
    std::array<double, BANDCOUNT> filtered_num{};
    std::array<double, BANDCOUNT> filtered_den{};
    std::array<double, BANDCOUNT> pattcorr_ref{};
    std::array<double, BANDCOUNT> pattcorr_test{};
    std::array<double, BANDCOUNT> spectrally_adapted_ref_patterns{};
    std::array<double, BANDCOUNT> spectrally_adapted_test_patterns{};
  };
  template<typename EarModel>
  LevelAdapter(EarModel const& ear_model)
  {
    static_assert(BANDCOUNT == EarModel::get_band_count());

    /* see section 3.1 in [BS1387], section 4.1 in [Kabal03] */
    for (std::size_t k = 0; k < BANDCOUNT; k++) {
      ear_time_constants[k] = ear_model.calc_time_constant(k, 0.008, 0.05);
    }
  }
  void process(std::array<double, BANDCOUNT> const& ref_excitation,
               std::array<double, BANDCOUNT> const& test_excitation,
               state_t& state) const
  {
    using std::cbegin;

    auto num = 0.0;
    auto den = 0.0;
    for (std::size_t k = 0; k < BANDCOUNT; k++) {
      /* (42) in [BS1387], (56) in [Kabal03] */
      state.ref_filtered_excitation[k] =
        ear_time_constants[k] * state.ref_filtered_excitation[k] +
        (1 - ear_time_constants[k]) * ref_excitation[k];
      /* (43) in [BS1387], (56) in [Kabal03] */
      state.test_filtered_excitation[k] =
        ear_time_constants[k] * state.test_filtered_excitation[k] +
        (1 - ear_time_constants[k]) * test_excitation[k];
      /* (45) in [BS1387], (57) in [Kabal03] */
      num +=
        std::sqrt(state.ref_filtered_excitation[k] * state.test_filtered_excitation[k]);
      den += state.test_filtered_excitation[k];
    }
    auto lev_corr = num * num / (den * den);
    auto levcorr_excitation = std::array<double, BANDCOUNT>{};
    if (lev_corr > 1) {
      /* (46) in [BS1387], (58) in [Kabal03] */
      std::transform(cbegin(ref_excitation),
                     cend(ref_excitation),
                     begin(levcorr_excitation),
                     [lev_corr](auto e) { return e / lev_corr; });
    } else {
      /* (47) in [BS1387], (58) in [Kabal03] */
      std::transform(cbegin(test_excitation),
                     cend(test_excitation),
                     begin(levcorr_excitation),
                     [lev_corr](auto e) { return e * lev_corr; });
    }
    auto const& levcorr_ref_excitation = lev_corr > 1 ? levcorr_excitation : ref_excitation;
    auto const& levcorr_test_excitation =
      lev_corr > 1 ? test_excitation : levcorr_excitation;
    auto pattadapt_ref = std::array<double, BANDCOUNT>{};
    auto pattadapt_test = std::array<double, BANDCOUNT>{};
    for (std::size_t k = 0; k < BANDCOUNT; k++) {
      /* (48) in [BS1387], (59) in [Kabal03] */
      state.filtered_num[k] = ear_time_constants[k] * state.filtered_num[k] +
                              levcorr_test_excitation[k] * levcorr_ref_excitation[k];
      state.filtered_den[k] = ear_time_constants[k] * state.filtered_den[k] +
                              levcorr_ref_excitation[k] * levcorr_ref_excitation[k];
      /* (49) in [BS1387], (60) in [Kabal03] */
      /* these values cannot be zero [Kabal03], so the special case desribed in
       * [BS1387] is unnecessary */
      if (state.filtered_num[k] >= state.filtered_den[k]) {
        pattadapt_ref[k] = 1.;
        pattadapt_test[k] = state.filtered_den[k] / state.filtered_num[k];
      } else {
        pattadapt_ref[k] = state.filtered_num[k] / state.filtered_den[k];
        pattadapt_test[k] = 1.;
      }
    }
    for (std::size_t k = 0; k < BANDCOUNT; k++) {
      /* (51) in [BS1387], (63) in [Kabal03] */
      /* dependence on BANDCOUNT is an ugly hack to avoid a nasty switch/case */
      auto m1 = std::min(k, BANDCOUNT / 36); /* 109 -> 3, 55 -> 1, 40 -> 1  */
      auto m2 =
        std::min(BANDCOUNT - k - 1, BANDCOUNT / 25); /* 109 -> 4, 55 -> 2, 40 -> 1  */
      /* (50) in [BS1387], (62) in [Kabal03] */
      auto ra_scale = double(m1 + m2 + 1);
      auto ra_ref = std::accumulate(cbegin(pattadapt_ref) + k - m1,
                                    cbegin(pattadapt_ref) + k + m2 + 1,
                                    0.0) /
                    ra_scale;
      auto ra_test = std::accumulate(cbegin(pattadapt_test) + k - m1,
                                     cbegin(pattadapt_test) + k + m2 + 1,
                                     0.0) /
                     ra_scale;
      /* (50) in [BS1387], (61) in [Kabal03] */
      state.pattcorr_ref[k] = ear_time_constants[k] * state.pattcorr_ref[k] +
                              (1 - ear_time_constants[k]) * ra_ref;
      state.pattcorr_test[k] = ear_time_constants[k] * state.pattcorr_test[k] +
                               (1 - ear_time_constants[k]) * ra_test;
      /* (52) in [BS1387], (64) in [Kabal03] */
      state.spectrally_adapted_ref_patterns[k] =
        levcorr_ref_excitation[k] * state.pattcorr_ref[k];
      /* (53) in [BS1387], (64) in [Kabal03] */
      state.spectrally_adapted_test_patterns[k] =
        levcorr_test_excitation[k] * state.pattcorr_test[k];
    }
  }

private:
  std::array<double, BANDCOUNT> ear_time_constants;
};

template<typename EarModel>
LevelAdapter(EarModel const&) -> LevelAdapter<EarModel::get_band_count()>;

} // namespace peaq

/**
 * peaq_leveladapter_process:
 * @level: The #PeaqLevelAdapter.
 * @ref_excitation: The excitation patterns of the reference signal
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mi>Ref</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mover accent="true"><mi>E</mi><mo>~</mo></mover><mi>sR</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * @test_excitation: The excitation patterns of the test signal
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mi>Test</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mover accent="true"><mi>E</mi><mo>~</mo></mover><mi>sT</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 *
 * Performs the actual level and pattern adaptation as described in
 * section 3.1 of <xref linkend="BS1387" /> and section 4.1 of <xref
 * linkend="Kabal03" />. The number of elements in the input data
 * @ref_excitation and @test_excitation
 * has to match the number of bands specified by the underlying #PeaqEarModel
 * as set with peaq_leveladapter_set_ear_model() or upon construction with
 * peaq_leveladapter_new().
 */

/**
 * peaq_leveladapter_get_adapted_ref:
 * @level: The #PeaqLevelAdapter to get the current spectrally adapted patterns
 * from.
 *
 * Returns the spectrally adapted patterns of the reference signal as computed
 * during the last call to peaq_leveladapter_process().
 *
 * Returns: The spectrally adapted patterns of the reference signal
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Ref</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mover accent="true"><mi>E</mi><mo>~</mo></mover><mi>PR</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * The pointer points to internal data of the #PeaqLevelAdapter and must not be
 * freed.
 */

/**
 * peaq_leveladapter_get_adapted_test:
 * @level: The #PeaqLevelAdapter to get the current spectrally adapted patterns
 * from.
 *
 * Returns the spectrally adapted patterns of the test signal as computed
 * during the last call to peaq_leveladapter_process().
 *
 * Returns: The spectrally adapted patterns of the test signal
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mrow><mi>P</mi><mo>,</mo><mi>Test</mi></mrow></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mover accent="true"><mi>E</mi><mo>~</mo></mover><mi>PT</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * The pointer points to internal data of the #PeaqLevelAdapter and must not be
 * freed.
 */

#endif
