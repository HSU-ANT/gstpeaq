/* GstPEAQ
 * Copyright (C) 2006, 2007, 2012, 2013, 2014, 2015, 2021
 * Martin Holters <martin.holters@hsu-hh.de>
 *
 * modpatt.c: Modulation pattern processor.
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

#include "modpatt.h"

#include <cmath>

namespace peaq {
void ModulationProcessor::set_ear_model(PeaqEarModel* ear_model)
{
  this->ear_model = ear_model;

  auto band_count = ear_model->get_band_count();

  previous_loudness.resize(band_count);
  filtered_loudness.resize(band_count);
  filtered_loudness_derivative.resize(band_count);
  modulation.resize(band_count);
  ear_time_constants.resize(band_count);
  for (size_t k = 0; k < band_count; k++) {
    /* (56) in [BS1387] */
    ear_time_constants[k] = ear_model->calc_time_constant(k, 0.008, 0.05);
  }
}

void ModulationProcessor::process(double const* unsmeared_excitation)
{
  auto band_count = ear_model->get_band_count();
  auto step_size = ear_model->get_step_size();
  auto sampling_rate = ear_model->get_sampling_rate();
  auto derivative_factor = static_cast<double>(sampling_rate) / step_size;

  for (std::size_t k = 0; k < band_count; k++) {
    /* (54) in [BS1387] */
    auto loudness = std::pow(unsmeared_excitation[k], 0.3);
    auto loudness_derivative =
      derivative_factor * std::abs(loudness - previous_loudness[k]);
    filtered_loudness_derivative[k] =
      ear_time_constants[k] * filtered_loudness_derivative[k] +
      (1 - ear_time_constants[k]) * loudness_derivative;
    /* (55) in [BS1387] */
    filtered_loudness[k] = ear_time_constants[k] * filtered_loudness[k] +
                           (1. - ear_time_constants[k]) * loudness;
    /* (57) in [BS1387] */
    modulation[k] = filtered_loudness_derivative[k] / (1. + filtered_loudness[k] / 0.3);
    previous_loudness[k] = loudness;
  }
}
} // namespace peaq

PeaqModulationProcessor* peaq_modulationprocessor_new(PeaqEarModel* ear_model)
{
  auto* modproc = new PeaqModulationProcessor{};
  peaq_modulationprocessor_set_ear_model(modproc, ear_model);
  return modproc;
}

void peaq_modulationprocessor_delete(PeaqModulationProcessor* modproc)
{
  delete modproc;
}

void peaq_modulationprocessor_set_ear_model(PeaqModulationProcessor* modproc,
                                            PeaqEarModel* ear_model)
{
  modproc->set_ear_model(ear_model);
}

PeaqEarModel* peaq_modulationprocessor_get_ear_model(PeaqModulationProcessor const* modproc)
{
  return modproc->get_ear_model();
}

void peaq_modulationprocessor_process(PeaqModulationProcessor* modproc,
                                      double const* unsmeared_excitation)
{
  modproc->process(unsmeared_excitation);
}

double const* peaq_modulationprocessor_get_average_loudness(
  PeaqModulationProcessor const* modproc)
{
  return modproc->get_average_loudness().data();
}

double const* peaq_modulationprocessor_get_modulation(
  PeaqModulationProcessor const* modproc)
{
  return modproc->get_modulation().data();
}
