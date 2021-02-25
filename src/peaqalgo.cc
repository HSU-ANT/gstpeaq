/* GstPEAQ
 * Copyright (C) 2021
 * Martin Holters <martin.holters@hsu-hh.de>
 *
 * peaqalgo.cc: Compute objective audio quality measures
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

#include "peaqalgo.h"

namespace peaq {
void AlgoBasic::do_process(bool above_thres)
{
  std::apply([above_thres](auto&... accum) { (accum.set_tentative(!above_thres), ...); },
             mov_accums);

  /* modulation difference */
  if (frame_counter >= 24) {
    mov_modulation_difference(std::get<0>(ear_models),
                              ref_modproc_state,
                              test_modproc_state,
                              std::get<MOV_AVG_MOD_DIFF_1>(mov_accums),
                              std::get<MOV_AVG_MOD_DIFF_2>(mov_accums),
                              std::get<MOV_WIN_MOD_DIFF>(mov_accums));
  }

  /* noise loudness */
  if (frame_counter >= 24 && frame_counter - 3 >= loudness_reached_frame) {
    mov_noise_loudness(std::get<0>(ear_models),
                       ref_modproc_state,
                       test_modproc_state,
                       level_state,
                       std::get<MOV_RMS_NOISE_LOUD>(mov_accums));
  }

  /* bandwidth */
  mov_bandwidth(std::get<0>(states_ref),
                std::get<0>(states_test),
                std::get<MOV_BANDWIDTH_REF>(mov_accums),
                std::get<MOV_BANDWIDTH_TEST>(mov_accums));

  /* noise-to-mask ratio */
  mov_nmr(std::get<0>(ear_models),
          std::get<0>(states_ref),
          std::get<0>(states_test),
          std::get<MOV_TOTAL_NMR>(mov_accums),
          std::get<MOV_REL_DIST_FRAMES>(mov_accums));

  /* probability of detection */
  mov_prob_detect(std::get<0>(ear_models),
                  std::get<0>(states_ref),
                  std::get<0>(states_test),
                  std::get<MOV_ADB>(mov_accums),
                  std::get<MOV_MFPD>(mov_accums));

  /* error harmonic structure */
  mov_ehs(std::get<0>(states_ref), std::get<0>(states_test), std::get<MOV_EHS>(mov_accums));
}

void AlgoAdvanced::do_process_fft(bool above_thres)
{
  std::get<MOV_SEGMENTAL_NMR>(mov_accums).set_tentative(!above_thres);
  std::get<MOV_EHS>(mov_accums).set_tentative(!above_thres);

  /* noise-to-mask ratio */
  mov_nmr(std::get<1>(ear_models),
          std::get<1>(states_ref),
          std::get<1>(states_test),
          std::get<MOV_SEGMENTAL_NMR>(mov_accums));

  /* error harmonic structure */
  mov_ehs(std::get<1>(states_ref), std::get<1>(states_test), std::get<MOV_EHS>(mov_accums));
}

void AlgoAdvanced::do_process_fb(bool above_thres)
{
  std::get<MOV_RMS_MOD_DIFF>(mov_accums).set_tentative(!above_thres);
  std::get<MOV_RMS_NOISE_LOUD_ASYM>(mov_accums).set_tentative(!above_thres);
  std::get<MOV_AVG_LIN_DIST>(mov_accums).set_tentative(!above_thres);

  /* modulation difference */
  if (frame_counter >= 125) {
    mov_modulation_difference(std::get<0>(ear_models),
                              ref_modproc_state,
                              test_modproc_state,
                              std::get<MOV_RMS_MOD_DIFF>(mov_accums));
  }

  /* noise loudness */
  if (frame_counter >= 125 && frame_counter - 13 >= loudness_reached_frame) {
    mov_noise_loud_asym(std::get<0>(ear_models),
                        ref_modproc_state,
                        test_modproc_state,
                        level_state,
                        std::get<MOV_RMS_NOISE_LOUD_ASYM>(mov_accums));
    mov_lin_dist(std::get<0>(ear_models),
                 ref_modproc_state,
                 level_state,
                 std::get<0>(states_ref),
                 std::get<MOV_AVG_LIN_DIST>(mov_accums));
  }
}

} // namespace peaq

PeaqAlgo* peaq_algo_basic_new()
{
  return new peaq::AlgoBasic{};
}

PeaqAlgo* peaq_algo_advanced_new()
{
  return new peaq::AlgoAdvanced{};
}

void peaq_algo_delete(PeaqAlgo* algo)
{
  delete algo;
}

int peaq_algo_get_channels(PeaqAlgo const* algo)
{
  return algo->get_channels();
}

void peaq_algo_set_channels(PeaqAlgo* algo, unsigned int channel_count)
{
  algo->set_channels(channel_count);
}

double peaq_algo_get_playback_level(PeaqAlgo const* algo)
{
  return algo->get_playback_level();
}

void peaq_algo_set_playback_level(PeaqAlgo* algo, double playback_level)
{
  algo->set_playback_level(playback_level);
}

void peaq_algo_process_block(PeaqAlgo* algo,
                             float const* refdata,
                             float const* testdata,
                             std::size_t num_samples)
{
  algo->process_block(refdata, testdata, num_samples);
}

void peaq_algo_flush(PeaqAlgo* algo)
{
  algo->flush();
}

double peaq_algo_calculate_odg(PeaqAlgo* algo, int console_output)
{
  return algo->calculate_odg(console_output != 0);
}

double peaq_algo_calculate_di(PeaqAlgo* algo, int console_output)
{
  return algo->calculate_di(console_output != 0);
}
