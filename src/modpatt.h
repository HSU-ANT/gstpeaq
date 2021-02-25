/* GstPEAQ
 * Copyright (C) 2006, 2012, 2013, 2015, 2021
 * Martin Holters <martin.holters@hsu-hh.de>
 *
 * modpatt.h: Modulation pattern processor.
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

#ifndef __MODPATT_H__
#define __MODPATT_H__ 1

/**
 * SECTION:modpatt
 * @short_description: Modulation Pattern Processing.
 * @title: PeaqModulationProcessor
 *
 * #PeaqModulationProcessor encapsulates the modulation processing described in
 * section 3.2 of <xref linkend="BS1387" />. It computes the per-band
 * modulation parameters.
 */

#include "earmodel.h"

#include <array>
#include <cmath>

namespace peaq {
template<std::size_t BANDCOUNT>
class ModulationProcessor
{
public:
  struct state_t
  {
    [[nodiscard]] const auto& get_average_loudness() const { return filtered_loudness; }
    [[nodiscard]] const auto& get_modulation() const { return modulation; }

  private:
    friend class ModulationProcessor;
    std::array<double, BANDCOUNT> previous_loudness{};
    std::array<double, BANDCOUNT> filtered_loudness{};
    std::array<double, BANDCOUNT> filtered_loudness_derivative{};
    std::array<double, BANDCOUNT> modulation{};
  };
  template<typename EarModel>
  ModulationProcessor(EarModel const& ear_model)
  {
    static_assert(BANDCOUNT == EarModel::get_band_count());
    auto step_size = ear_model.get_step_size();
    auto sampling_rate = ear_model.get_sampling_rate();
    derivative_factor = static_cast<double>(sampling_rate) / step_size;

    for (size_t k = 0; k < BANDCOUNT; k++) {
      /* (56) in [BS1387] */
      ear_time_constants[k] = ear_model.calc_time_constant(k, 0.008, 0.05);
    }
  }
  void process(std::array<double, BANDCOUNT> const& unsmeared_excitation,
               state_t& state) const
  {
    for (std::size_t k = 0; k < BANDCOUNT; k++) {
      /* (54) in [BS1387] */
      auto loudness = std::pow(unsmeared_excitation[k], 0.3);
      auto loudness_derivative =
        derivative_factor * std::abs(loudness - state.previous_loudness[k]);
      state.filtered_loudness_derivative[k] =
        ear_time_constants[k] * state.filtered_loudness_derivative[k] +
        (1 - ear_time_constants[k]) * loudness_derivative;
      /* (55) in [BS1387] */
      state.filtered_loudness[k] = ear_time_constants[k] * state.filtered_loudness[k] +
                                   (1. - ear_time_constants[k]) * loudness;
      /* (57) in [BS1387] */
      state.modulation[k] =
        state.filtered_loudness_derivative[k] / (1. + state.filtered_loudness[k] / 0.3);
      state.previous_loudness[k] = loudness;
    }
  }

private:
  double derivative_factor;
  std::array<double, BANDCOUNT> ear_time_constants;
};

template<typename EarModel>
ModulationProcessor(EarModel const&) -> ModulationProcessor<EarModel::get_band_count()>;
} // namespace peaq

/**
 * peaq_modulationprocessor_process:
 * @modproc: The #PeaqModulationProcessor.
 * @unsmeared_excitation: The unsmeared excitation patterns
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mn>2</mn></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>E</mi><mi>s</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 *
 * Performs the actual computation of the modulation as described in section
 * 3.2 of <xref linkend="BS1387" /> and section 4.2 of <xref linkend="Kabal03"
 * />. The number of elements in the input data @unsmeared_excitation has to
 * match the number of bands specified by the underlying #PeaqEarModel as set
 * with peaq_modulationprocessor_set_ear_model() or upon construction with
 * peaq_modulationprocessor_new().
 */

/**
 * peaq_modulationprocessor_get_average_loudness:
 * @modproc: The #PeaqModulationProcessor to get the current average loudness from.
 *
 * Returns the average loudness <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mover accent="true"><mi>E</mi><mi>-</mi></mover><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * as computed during the last call to peaq_modulationprocessor_process().
 *
 * Returns: The average loudness <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mover accent="true"><mi>E</mi><mi>-</mi></mover><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>.
 * The pointer points to internal data of the #PeaqModulationProcessor and must
 * not be freed.
 */

/**
 * peaq_modulationprocessor_get_modulation:
 * @modproc: The #PeaqModulationProcessor to get the current modulation from.
 *
 * Returns the modulation <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>Mod</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>
 * as computed during the last call to peaq_modulationprocessor_process().
 *
 * Returns: The modulation <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>Mod</mi><mfenced open="[" close="]"><mi>k</mi></mfenced>
 * </math></inlineequation>.
 * The pointer points to internal data of the #PeaqModulationProcessor and must
 * not be freed.
 */

#endif
