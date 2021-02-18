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
void AlgoBasic::do_process()
{
  auto above_thres =
    std::any_of(cbegin(ref_buffers), cend(ref_buffers), [](auto const& chandata) {
      return is_frame_above_threshold(cbegin(chandata), cbegin(chandata) + FRAME_SIZE);
    });
  for (auto& accum : mov_accums) {
    peaq_movaccum_set_tentative(accum.get(), !above_thres);
  }
  for (auto chan = 0U; chan < channel_count; chan++) {
    fft_ear_model.process_block(&fft_earmodel_state_ref[chan], ref_buffers[chan].data());
    fft_ear_model.process_block(&fft_earmodel_state_test[chan], test_buffers[chan].data());
    auto const* ref_excitation =
      fft_ear_model.get_excitation(&fft_earmodel_state_ref[chan]);
    auto const* test_excitation =
      fft_ear_model.get_excitation(&fft_earmodel_state_test[chan]);
    auto const* ref_unsmeared_excitation =
      fft_ear_model.get_unsmeared_excitation(&fft_earmodel_state_ref[chan]);
    auto const* test_unsmeared_excitation =
      fft_ear_model.get_unsmeared_excitation(&fft_earmodel_state_test[chan]);

    level_adapters[chan].process(ref_excitation, test_excitation);
    ref_modulation_processors[chan].process(ref_unsmeared_excitation);
    test_modulation_processors[chan].process(test_unsmeared_excitation);
    if (loudness_reached_frame == std::numeric_limits<unsigned int>::max()) {
      if (fft_ear_model.calc_loudness(&fft_earmodel_state_ref[chan]) > 0.1 &&
          fft_ear_model.calc_loudness(&fft_earmodel_state_test[chan]) > 0.1) {
        loudness_reached_frame = frame_counter;
      }
    }
  }

  std::vector<FFTEarModel::state_t*> ref_state(channel_count);
  std::vector<FFTEarModel::state_t*> test_state(channel_count);
  for (auto chan = 0U; chan < channel_count; chan++) {
    ref_state[chan] = &fft_earmodel_state_ref[chan];
    test_state[chan] = &fft_earmodel_state_test[chan];
  }
  std::vector<ModulationProcessor*> ref_mod_procs(ref_modulation_processors.size());
  std::vector<ModulationProcessor*> test_mod_procs(test_modulation_processors.size());
  for (std::size_t i = 0; i < ref_modulation_processors.size(); i++) {
    ref_mod_procs[i] = &ref_modulation_processors[i];
    test_mod_procs[i] = &test_modulation_processors[i];
  }
  std::vector<LevelAdapter*> _level_adapters(level_adapters.size());
  for (std::size_t i = 0; i < level_adapters.size(); i++) {
    _level_adapters[i] = &level_adapters[i];
  }

  /* modulation difference */
  if (frame_counter >= 24) {
    peaq_mov_modulation_difference(ref_mod_procs.data(),
                                   test_mod_procs.data(),
                                   mov_accums[MOV_AVG_MOD_DIFF_1].get(),
                                   mov_accums[MOV_AVG_MOD_DIFF_2].get(),
                                   mov_accums[MOV_WIN_MOD_DIFF].get());
  }

  /* noise loudness */
  if (frame_counter >= 24 && frame_counter - 3 >= loudness_reached_frame) {
    peaq_mov_noise_loudness(ref_mod_procs.data(),
                            test_mod_procs.data(),
                            _level_adapters.data(),
                            mov_accums[MOV_RMS_NOISE_LOUD].get());
  }

  /* bandwidth */
  peaq_mov_bandwidth((void**)ref_state.data(),
                     (void**)test_state.data(),
                     mov_accums[MOV_BANDWIDTH_REF].get(),
                     mov_accums[MOV_BANDWIDTH_TEST].get());

  /* noise-to-mask ratio */
  peaq_mov_nmr(&fft_ear_model,
               (void**)ref_state.data(),
               (void**)test_state.data(),
               mov_accums[MOV_TOTAL_NMR].get(),
               mov_accums[MOV_REL_DIST_FRAMES].get());

  /* probability of detection */
  peaq_mov_prob_detect(&fft_ear_model,
                       (void**)ref_state.data(),
                       (void**)test_state.data(),
                       channel_count,
                       mov_accums[MOV_ADB].get(),
                       mov_accums[MOV_MFPD].get());

  /* error harmonic structure */
  peaq_mov_ehs(&fft_ear_model,
               (void**)ref_state.data(),
               (void**)test_state.data(),
               mov_accums[MOV_EHS].get());

  frame_counter++;
}

void AlgoAdvanced::do_process_fft()
{
  auto above_thres =
    std::any_of(cbegin(ref_buffers),
                cend(ref_buffers),
                [start_offset = buffer_fft_offset,
                 end_offset = buffer_fft_offset + FFT_FRAME_SIZE](auto const& chandata) {
                  return is_frame_above_threshold(cbegin(chandata) + start_offset,
                                                  cbegin(chandata) + end_offset);
                });

  peaq_movaccum_set_tentative(mov_accums[MOV_SEGMENTAL_NMR].get(), !above_thres);
  peaq_movaccum_set_tentative(mov_accums[MOV_EHS].get(), !above_thres);

  for (auto c = 0U; c < channel_count; c++) {
    fft_ear_model.process_block(&fft_earmodel_state_ref[c],
                                ref_buffers[c].data() + buffer_fft_offset);
    fft_ear_model.process_block(&fft_earmodel_state_test[c],
                                test_buffers[c].data() + buffer_fft_offset);
  }

  std::vector<FFTEarModel::state_t*> ref_state(channel_count);
  std::vector<FFTEarModel::state_t*> test_state(channel_count);
  for (auto chan = 0U; chan < channel_count; chan++) {
    ref_state[chan] = &fft_earmodel_state_ref[chan];
    test_state[chan] = &fft_earmodel_state_test[chan];
  }

  /* noise-to-mask ratio */
  peaq_mov_nmr(&fft_ear_model,
               (void**)ref_state.data(),
               (void**)test_state.data(),
               mov_accums[MOV_SEGMENTAL_NMR].get(),
               nullptr);

  /* error harmonic structure */
  peaq_mov_ehs(&fft_ear_model,
               (void**)ref_state.data(),
               (void**)test_state.data(),
               mov_accums[MOV_EHS].get());
}

void AlgoAdvanced::do_process_fb()
{
  auto above_thres =
    std::any_of(cbegin(ref_buffers),
                cend(ref_buffers),
                [start_offset = buffer_fb_offset,
                 end_offset = buffer_fb_offset + FB_FRAME_SIZE](auto const& chandata) {
                  return is_frame_above_threshold(cbegin(chandata) + start_offset,
                                                  cbegin(chandata) + end_offset);
                });
  peaq_movaccum_set_tentative(mov_accums[MOV_RMS_MOD_DIFF].get(), !above_thres);
  peaq_movaccum_set_tentative(mov_accums[MOV_RMS_NOISE_LOUD_ASYM].get(), !above_thres);
  peaq_movaccum_set_tentative(mov_accums[MOV_AVG_LIN_DIST].get(), !above_thres);

  for (auto c = 0U; c < channel_count; c++) {
    fb_ear_model.process_block(&fb_earmodel_state_ref[c],
                               ref_buffers[c].data() + buffer_fb_offset);
    fb_ear_model.process_block(&fb_earmodel_state_test[c],
                               test_buffers[c].data() + buffer_fb_offset);
  }
  for (auto chan = 0U; chan < channel_count; chan++) {
    auto const* ref_excitation = fb_ear_model.get_excitation(&fb_earmodel_state_ref[chan]);
    auto const* test_excitation =
      fb_ear_model.get_excitation(&fb_earmodel_state_test[chan]);
    auto const* ref_unsmeared_excitation =
      fb_ear_model.get_unsmeared_excitation(&fb_earmodel_state_ref[chan]);
    auto const* test_unsmeared_excitation =
      fb_ear_model.get_unsmeared_excitation(&fb_earmodel_state_test[chan]);

    level_adapters[chan].process(ref_excitation, test_excitation);
    ref_modulation_processors[chan].process(ref_unsmeared_excitation);
    test_modulation_processors[chan].process(test_unsmeared_excitation);
    if (loudness_reached_frame == std::numeric_limits<unsigned int>::max()) {
      if (fb_ear_model.calc_loudness(&fb_earmodel_state_ref[chan]) > 0.1 &&
          fb_ear_model.calc_loudness(&fb_earmodel_state_test[chan]) > 0.1) {
        loudness_reached_frame = frame_counter;
      }
    }
  }

  std::vector<FilterbankEarModel::state_t*> ref_state(channel_count);
  std::vector<FilterbankEarModel::state_t*> test_state(channel_count);
  for (auto chan = 0U; chan < channel_count; chan++) {
    ref_state[chan] = &fb_earmodel_state_ref[chan];
    test_state[chan] = &fb_earmodel_state_test[chan];
  }
  std::vector<LevelAdapter*> _level_adapters(level_adapters.size());
  for (std::size_t i = 0; i < level_adapters.size(); i++) {
    _level_adapters[i] = &level_adapters[i];
  }
  std::vector<ModulationProcessor*> ref_mod_procs(ref_modulation_processors.size());
  std::vector<ModulationProcessor*> test_mod_procs(test_modulation_processors.size());
  for (std::size_t i = 0; i < ref_modulation_processors.size(); i++) {
    ref_mod_procs[i] = &ref_modulation_processors[i];
    test_mod_procs[i] = &test_modulation_processors[i];
  }

  /* modulation difference */
  if (frame_counter >= 125) {
    peaq_mov_modulation_difference(ref_mod_procs.data(),
                                   test_mod_procs.data(),
                                   mov_accums[MOV_RMS_MOD_DIFF].get(),
                                   nullptr,
                                   nullptr);
  }

  /* noise loudness */
  if (frame_counter >= 125 && frame_counter - 13 >= loudness_reached_frame) {
    peaq_mov_noise_loud_asym(ref_mod_procs.data(),
                             test_mod_procs.data(),
                             _level_adapters.data(),
                             mov_accums[MOV_RMS_NOISE_LOUD_ASYM].get());
    peaq_mov_lin_dist(ref_mod_procs.data(),
                      test_mod_procs.data(),
                      _level_adapters.data(),
                      (void**)ref_state.data(),
                      mov_accums[MOV_AVG_LIN_DIST].get());
  }

  frame_counter++;
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
