/* GstPEAQ
 * Copyright (C) 2006, 2007, 2011, 2012, 2013, 2014, 2015, 2021
 * Martin Holters <martin.holters@hsu-hh.de>
 *
 * earmodel.c: Peripheral ear model part.
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

#include "earmodel.h"

namespace peaq {

double EarModel::calc_loudness(state_t const* state) const
{
  auto overall_loudness = 0.;
  auto const* excitation = get_excitation(state);
  for (std::size_t i = 0; i < band_count; i++) {
    auto loudness =
      loudness_factor[i] *
      (std::pow(1. - threshold[i] + threshold[i] * excitation[i] / excitation_threshold[i],
                0.23) -
       1.);
    overall_loudness += std::max(loudness, 0.);
  }
  overall_loudness *= 24. / band_count;
  return overall_loudness;
}

double EarModel::calc_time_constant(std::size_t band, double tau_min, double tau_100) const
{
  auto step_size = this->get_step_size();
  /* (21), (38), (41), and (56) in [BS1387], (32) in [Kabal03] */
  auto tau = tau_min + 100. / fc[band] * (tau_100 - tau_min);
  /* (24), (40), and (44) in [BS1387], (33) in [Kabal03] */
  return std::exp(step_size / (-48000. * tau));
}

} // namespace peaq

void peaq_earmodel_delete(PeaqEarModel* model)
{
  delete model;
}

double peaq_earmodel_get_playback_level(PeaqEarModel const* model)
{
  return model->get_playback_level();
}

void peaq_earmodel_set_playback_level(PeaqEarModel* model, double level)
{
  model->set_playback_level(level);
}

peaq::EarModel::state_t* peaq_earmodel_state_alloc(PeaqEarModel const* model)
{
  return model->state_alloc();
}

void peaq_earmodel_state_free(PeaqEarModel const* model, peaq::EarModel::state_t* state)
{
  model->state_free(state);
}

void peaq_earmodel_process_block(PeaqEarModel const* model,
                                 PeaqEarModelState* state,
                                 float const* samples)
{
  model->process_block(state, samples);
}

double const* peaq_earmodel_get_excitation(PeaqEarModel const* model,
                                           PeaqEarModelState const* state)
{
  return model->get_excitation(state);
}

double const* peaq_earmodel_get_unsmeared_excitation(PeaqEarModel const* model,
                                                     PeaqEarModelState const* state)
{
  return model->get_unsmeared_excitation(state);
}

unsigned int peaq_earmodel_get_frame_size(PeaqEarModel const* model)
{
  return model->get_frame_size();
}

unsigned int peaq_earmodel_get_step_size(PeaqEarModel const* model)
{
  return model->get_step_size();
}

double peaq_earmodel_calc_loudness(PeaqEarModel const* model,
                                   PeaqEarModelState const* state)
{
  return model->calc_loudness(state);
}
