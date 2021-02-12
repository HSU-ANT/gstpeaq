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

#ifdef __cplusplus
#include <memory>
#include <vector>

namespace peaq {
class ModulationProcessor
{
public:
  [[nodiscard]] auto get_ear_model() const { return ear_model; }
  void set_ear_model(PeaqEarModel* ear_model);
  void process(double const* unsmeared_excitation);
  [[nodiscard]] const auto& get_average_loudness() const { return filtered_loudness; }
  [[nodiscard]] const auto& get_modulation() const { return modulation; }

private:
  PeaqEarModel* ear_model{};

  std::vector<double> ear_time_constants;
  std::vector<double> previous_loudness;
  std::vector<double> filtered_loudness;
  std::vector<double> filtered_loudness_derivative;
  std::vector<double> modulation;
};
} // namespace peaq

using PeaqModulationProcessor = peaq::ModulationProcessor;

extern "C" {
#else
/**
 * PeaqModulationProcessor:
 *
 * The opaque PeaqModulationProcessor structure.
 */
typedef struct _PeaqModulationProcessor PeaqModulationProcessor;
#endif


/**
 * peaq_modulationprocessor_new:
 * @ear_model: The #PeaqEarModel to get the band information from.
 *
 * Constructs a new #PeaqModulationProcessor, using the given @ear_model to
 * obtain information about the number of frequency bands used and their center
 * frequencies.
 *
 * Returns: The newly constructed #PeaqModulationProcessor.
 */
PeaqModulationProcessor *peaq_modulationprocessor_new (PeaqEarModel *ear_model);
void peaq_modulationprocessor_delete(PeaqModulationProcessor *modproc);

/**
 * peaq_modulationprocessor_set_ear_model:
 * @modproc: The #PeaqModulationProcessor to set the #PeaqEarModel of.
 * @ear_model: The #PeaqEarModel to get the band information from.
 *
 * Sets the #PeaqEarModel from which the frequency band information is used and
 * precomputes time constants that depend on the band center frequencies.
 */
void peaq_modulationprocessor_set_ear_model (PeaqModulationProcessor *modproc,
                                             PeaqEarModel *ear_model);

/**
 * peaq_modulationprocessor_get_ear_model:
 * @modproc: The #PeaqModulationProcessor to get the #PeaqEarModel of.
 *
 * Returns the #PeaqEarModel used by the @modproc to get the frequency band
 * information.
 *
 * Returns: The #PeaqEarModel as set with
 * peaq_modulationprocessor_set_ear_model().
 */
PeaqEarModel *peaq_modulationprocessor_get_ear_model (PeaqModulationProcessor const *modproc);

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
void peaq_modulationprocessor_process (PeaqModulationProcessor *modproc,
				      double const* unsmeared_excitation);

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
double const *peaq_modulationprocessor_get_average_loudness (PeaqModulationProcessor const *modproc);

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
double const *peaq_modulationprocessor_get_modulation (PeaqModulationProcessor const *modproc);

#ifdef __cplusplus
}
#endif

#endif
