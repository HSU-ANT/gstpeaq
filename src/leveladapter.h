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

#include <memory>
#include <vector>

namespace peaq {
class LevelAdapter
{
public:
  LevelAdapter(EarModel& ear_model);
  void process(double const* ref_excitation, double const* test_excitation);
  [[nodiscard]] auto const& get_adapted_ref() const
  {
    return spectrally_adapted_ref_patterns;
  }
  [[nodiscard]] auto const& get_adapted_test() const
  {
    return spectrally_adapted_test_patterns;
  }

private:
  std::size_t band_count;
  std::vector<double> ear_time_constants;
  std::vector<double> ref_filtered_excitation;
  std::vector<double> test_filtered_excitation;
  std::vector<double> filtered_num;
  std::vector<double> filtered_den;
  std::vector<double> pattcorr_ref;
  std::vector<double> pattcorr_test;
  std::vector<double> spectrally_adapted_ref_patterns;
  std::vector<double> spectrally_adapted_test_patterns;
};
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
