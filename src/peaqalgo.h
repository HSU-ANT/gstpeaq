/* GstPEAQ
 * Copyright (C) 2021
 * Martin Holters <martin.holters@hsu-hh.de>
 *
 * peaqalgo.h: Compute objective audio quality measures
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

#ifdef __cplusplus
#include "fbearmodel.h"
#include "fftearmodel.h"
#include "leveladapter.h"
#include "modpatt.h"
#include "movaccum.h"
#include "movs.h"
#include "nn.h"

#include <cassert>
#include <iomanip>
#include <iostream>
#include <numeric>

namespace peaq {

class Algo
{
public:
  virtual ~Algo() = default;
  [[nodiscard]] virtual auto get_channels() const -> int = 0;
  virtual void set_channels(unsigned int channel_count) = 0;
  [[nodiscard]] virtual auto get_playback_level() const -> double = 0;
  virtual void set_playback_level(double playback_level) = 0;
  virtual void process_block(float const* refdata,
                             float const* testdata,
                             std::size_t num_samples) = 0;
  virtual void flush() = 0;
  [[nodiscard]] virtual auto calculate_di(bool console_output = false) const -> double = 0;
  [[nodiscard]] virtual auto calculate_odg(bool console_output = false) const -> double = 0;
};

template<typename MovAccums, typename EarModels, typename Derived>
class AlgoBase : public Algo
{
public:
  [[nodiscard]] auto get_channels() const -> int final { return channel_count; }
  void set_channels(unsigned int channel_count) override
  {
    this->channel_count = channel_count;
    ref_buffers.resize(channel_count);
    test_buffers.resize(channel_count);
    std::apply([channel_count](auto&... state) { (state.resize(channel_count), ...); },
               states_ref);
    std::apply([channel_count](auto&... state) { (state.resize(channel_count), ...); },
               states_test);
    level_state.resize(channel_count);
    ref_modproc_state.resize(channel_count);
    test_modproc_state.resize(channel_count);
  }
  [[nodiscard]] auto get_playback_level() const -> double final
  {
    return std::get<0>(ear_models).get_playback_level();
  }
  void set_playback_level(double playback_level) final
  {
    std::apply(
      [playback_level](auto&... ear_model) {
        (ear_model.set_playback_level(playback_level), ...);
      },
      ear_models);
  }
  void process_block(float const* refdata,
                     float const* testdata,
                     std::size_t num_samples) final
  {

    while (num_samples > 0) {
      auto insert_count = std::min(num_samples, BUFFER_SIZE - buffer_valid_count);
      for (auto c = 0U; c < channel_count; c++) {
        for (std::size_t i = 0; i < insert_count; i++) {
          ref_buffers[c][buffer_valid_count + i] = refdata[channel_count * i + c];
          test_buffers[c][buffer_valid_count + i] = testdata[channel_count * i + c];
        }
      }

      num_samples -= insert_count;
      refdata += channel_count * insert_count;
      testdata += channel_count * insert_count;
      buffer_valid_count += insert_count;

      per_earmodel<process_all_frames_f>();

      auto step_size =
        std::apply([](auto&... off) { return std::min({ off... }); }, buffer_offsets);
      for (auto c = 0U; c < channel_count; c++) {
        std::copy(
          cbegin(ref_buffers[c]) + step_size, cend(ref_buffers[c]), begin(ref_buffers[c]));
        std::copy(cbegin(test_buffers[c]) + step_size,
                  cend(test_buffers[c]),
                  begin(test_buffers[c]));
      }
      buffer_valid_count -= step_size;
      std::transform(cbegin(buffer_offsets),
                     cend(buffer_offsets),
                     begin(buffer_offsets),
                     [step_size](auto off) { return off - step_size; });
    }
  }
  void flush() final
  {
    if (buffer_valid_count > 0) {
      for (auto c = 0U; c < channel_count; c++) {
        std::fill(begin(ref_buffers[c]) + buffer_valid_count, end(ref_buffers[c]), 0.0);
        std::fill(begin(test_buffers[c]) + buffer_valid_count, end(test_buffers[c]), 0.0);
      }
      buffer_valid_count = BUFFER_SIZE;
      per_earmodel<process_one_frame_f>();
      buffer_valid_count = 0;
      std::fill(begin(buffer_offsets), end(buffer_offsets), 0);
    }
  }
  [[nodiscard]] auto calculate_odg(bool console_output = false) const -> double final
  {
    auto distortion_index =
      dynamic_cast<Derived const*>(this)->calculate_di(console_output);
    return calculate_odg(distortion_index, console_output);
  }
  static auto calculate_odg(double distortion_index, bool console_output = false)
  {
    auto odg = peaq::calculate_odg(distortion_index);
    if (console_output) {
      std::cout.imbue(std::locale(""));
      std::cout << std::fixed << std::setprecision(3)
                << "Objective Difference Grade: " << odg << "\n";
    }
    return odg;
  }

private:
  template<typename E = EarModels>
  struct EarModelsTrait;
  template<typename... T>
  struct EarModelsTrait<std::tuple<T...>>
  {
    using states_t = std::tuple<std::vector<typename T::state_t>...>;
    static constexpr auto buffer_size = (T::FRAME_SIZE + ...);
  };

  static auto constexpr BUFFER_SIZE = EarModelsTrait<>::buffer_size;
  static auto constexpr BANDCOUNT_0 = std::tuple_element_t<0, EarModels>::get_band_count();

  unsigned int channel_count;
  std::size_t buffer_valid_count{ 0 };
  std::array<std::size_t, std::tuple_size_v<EarModels>> buffer_offsets{};
  std::vector<std::array<float, BUFFER_SIZE>> ref_buffers;
  std::vector<std::array<float, BUFFER_SIZE>> test_buffers;

  void preprocess()
  {
    for (auto chan = 0U; chan < channel_count; chan++) {
      auto const& ref_excitation = std::get<0>(states_ref)[chan].get_excitation();
      auto const& test_excitation = std::get<0>(states_test)[chan].get_excitation();
      auto const& ref_unsmeared_excitation =
        std::get<0>(states_ref)[chan].get_unsmeared_excitation();
      auto const& test_unsmeared_excitation =
        std::get<0>(states_test)[chan].get_unsmeared_excitation();

      level_adapter.process(ref_excitation, test_excitation, level_state[chan]);
      modulation_processor.process(ref_unsmeared_excitation, ref_modproc_state[chan]);
      modulation_processor.process(test_unsmeared_excitation, test_modproc_state[chan]);
      if (loudness_reached_frame == std::numeric_limits<unsigned int>::max()) {
        if (std::get<0>(ear_models).calc_loudness(&std::get<0>(states_ref)[chan]) > 0.1 &&
            std::get<0>(ear_models).calc_loudness(&std::get<0>(states_test)[chan]) > 0.1) {
          loudness_reached_frame = frame_counter;
        }
      }
    }
  }

  template<int n>
  void process_one_frame()
  {
    auto start_offset = std::get<n>(buffer_offsets);
    auto above_thres = std::any_of(
      cbegin(ref_buffers), cend(ref_buffers), [start_offset](auto const& refbuf) {
        return is_frame_above_threshold<n>(cbegin(refbuf) + start_offset);
      });

    for (auto c = 0U; c < channel_count; c++) {
      std::get<n>(ear_models)
        .process_block(std::get<n>(states_ref)[c], cbegin(ref_buffers[c]) + start_offset);
      std::get<n>(ear_models)
        .process_block(std::get<n>(states_test)[c], cbegin(test_buffers[c]) + start_offset);
    }
    if (n == 0) {
      preprocess();
    }
    dynamic_cast<Derived*>(this)->template do_process<n>(above_thres);
    if (n == 0) {
      frame_counter++;
    }

    std::get<n>(buffer_offsets) += std::get<n>(ear_models).get_step_size();
  }
  template<int n>
  void process_all_frames()
  {
    while (buffer_valid_count >=
           std::get<n>(ear_models).FRAME_SIZE + std::get<n>(buffer_offsets)) {
      process_one_frame<n>();
    }
  }
  template<std::size_t n>
  struct process_one_frame_f
  {
    void operator()(AlgoBase* instance) { instance->process_one_frame<n>(); }
  };

  template<std::size_t n>
  struct process_all_frames_f
  {
    void operator()(AlgoBase* instance) { instance->process_all_frames<n>(); }
  };

  template<template<std::size_t> typename func, size_t... I>
  void per_earmodel(std::index_sequence<I...>)
  {
    (func<I>{}(this), ...);
  }

  template<template<std::size_t> typename func>
  void per_earmodel()
  {
    per_earmodel<func>(std::make_index_sequence<std::tuple_size_v<EarModels>>{});
  }

  template<std::size_t n, typename InputIt>
  static auto is_frame_above_threshold(InputIt first)
  {
    auto last = first + std::tuple_element_t<n, EarModels>::FRAME_SIZE;
    auto five_ahead = first + 5;
    auto sum = std::accumulate(
      first, five_ahead, 0.0F, [](auto s, auto x) { return s + std::abs(x); });
    while (five_ahead < last) {
      sum += std::abs(*five_ahead++) - std::abs(*first++);
      if (sum >= 200. / 32768) {
        return true;
      }
    }
    return false;
  }

protected:
  MovAccums mov_accums;
  EarModels ear_models;
  std::size_t frame_counter{ 0 };
  std::size_t loudness_reached_frame{ std::numeric_limits<unsigned int>::max() };
  typename EarModelsTrait<>::states_t states_ref;
  typename EarModelsTrait<>::states_t states_test;
  std::vector<typename LevelAdapter<BANDCOUNT_0>::state_t> level_state;
  std::vector<typename ModulationProcessor<BANDCOUNT_0>::state_t> ref_modproc_state;
  std::vector<typename ModulationProcessor<BANDCOUNT_0>::state_t> test_modproc_state;

private:
  LevelAdapter<BANDCOUNT_0> level_adapter{ std::get<0>(ear_models) };
  ModulationProcessor<BANDCOUNT_0> modulation_processor{ std::get<0>(ear_models) };
};

class AlgoBasic
  : public AlgoBase<std::tuple<movaccum_avg,
                               movaccum_avg,
                               movaccum_avg_log,
                               movaccum_avg_window,
                               movaccum_adb,
                               movaccum_avg,
                               movaccum_avg,
                               movaccum_avg,
                               movaccum_rms,
                               movaccum_filtered_max,
                               movaccum_avg>,
                    std::tuple<FFTEarModel<109>>,
                    AlgoBasic>
{
private:
  enum
  {
    MOV_BANDWIDTH_REF,
    MOV_BANDWIDTH_TEST,
    MOV_TOTAL_NMR,
    MOV_WIN_MOD_DIFF,
    MOV_ADB,
    MOV_EHS,
    MOV_AVG_MOD_DIFF_1,
    MOV_AVG_MOD_DIFF_2,
    MOV_RMS_NOISE_LOUD,
    MOV_MFPD,
    MOV_REL_DIST_FRAMES,
  };

  void do_process(bool above_thres);

public:
  template<int n, std::enable_if_t<n == 0, bool> = true>
  void do_process(bool above_thres)
  {
    do_process(above_thres);
  }
  void set_channels(unsigned int channel_count) final
  {
    AlgoBase::set_channels(channel_count);
    std::apply(
      [channel_count, i = 0](auto&... accum) mutable {
        ((accum.set_channels(i == MOV_ADB || i == MOV_MFPD ? 1 : channel_count), i++), ...);
      },
      mov_accums);
  }
  [[nodiscard]] auto calculate_di(bool console_output = false) const -> double final
  {
    auto movs = std::apply(
      [](auto&... accum) { return std::array{ accum.get_value()... }; }, mov_accums);
    auto distortion_index = calculate_di_basic(movs);

    if (console_output) {
      std::cout.imbue(std::locale(""));
      std::cout << std::fixed << std::setprecision(6) << "   BandwidthRefB: " << movs[0]
                << "\n"
                << "  BandwidthTestB: " << movs[1] << "\n"
                << "      Total NMRB: " << movs[2] << "\n"
                << "    WinModDiff1B: " << movs[3] << "\n"
                << "            ADBB: " << movs[4] << "\n"
                << "            EHSB: " << movs[5] << "\n"
                << "    AvgModDiff1B: " << movs[6] << "\n"
                << "    AvgModDiff2B: " << movs[7] << "\n"
                << "   RmsNoiseLoudB: " << movs[8] << "\n"
                << "           MFPDB: " << movs[9] << "\n"
                << "  RelDistFramesB: " << movs[10] << "\n";
    }

    return distortion_index;
  }
};

class AlgoAdvanced
  : public AlgoBase<
      std::tuple<movaccum_rms, movaccum_rms_asym, movaccum_avg, movaccum_avg, movaccum_avg>,
      std::tuple<FilterbankEarModel, FFTEarModel<55>>,
      AlgoAdvanced>
{
private:
  enum
  {
    MOV_RMS_MOD_DIFF,
    MOV_RMS_NOISE_LOUD_ASYM,
    MOV_SEGMENTAL_NMR,
    MOV_EHS,
    MOV_AVG_LIN_DIST,
  };

  void do_process_fb(bool above_thres);
  void do_process_fft(bool above_thres);

public:
  template<int n, std::enable_if_t<n == 0, bool> = true>
  void do_process(bool above_thres)
  {
    do_process_fb(above_thres);
  }
  template<int n, std::enable_if_t<n == 1, bool> = true>
  void do_process(bool above_thres)
  {
    do_process_fft(above_thres);
  }
  void set_channels(unsigned int channel_count) final
  {
    AlgoBase::set_channels(channel_count);
    std::apply(
      [channel_count](auto&... accum) { (accum.set_channels(channel_count), ...); },
      mov_accums);
  }
  [[nodiscard]] auto calculate_di(bool console_output = false) const -> double final
  {
    auto movs = std::apply(
      [](auto&... accum) { return std::array{ accum.get_value()... }; }, mov_accums);
    auto distortion_index = calculate_di_advanced(movs);

    if (console_output) {
      std::cout.imbue(std::locale(""));
      std::cout << std::fixed << std::setprecision(6) << "RmsModDiffA = " << movs[0] << "\n"
                << "RmsNoiseLoudAsymA = " << movs[1] << "\n"
                << "SegmentalNMRB = " << movs[2] << "\n"
                << "EHSB = " << movs[3] << "\n"
                << "AvgLinDistA = " << movs[4] << "\n";
    }

    return distortion_index;
  }
};

} // namespace peaq

using PeaqAlgo = peaq::Algo;
extern "C" {
#else
typedef struct _PeaqAlgo PeaqAlgo;
#endif

PeaqAlgo* peaq_algo_basic_new();
PeaqAlgo* peaq_algo_advanced_new();
void peaq_algo_delete(PeaqAlgo* algo);
int peaq_algo_get_channels(PeaqAlgo const* algo);
void peaq_algo_set_channels(PeaqAlgo* algo, unsigned int channel_count);
double peaq_algo_get_playback_level(PeaqAlgo const* algo);
void peaq_algo_set_playback_level(PeaqAlgo* algo, double playback_level);
void peaq_algo_process_block(PeaqAlgo* algo,
                             float const* refdata,
                             float const* testdata,
                             size_t num_samples);
void peaq_algo_flush(PeaqAlgo* algo);
double peaq_algo_calculate_di(PeaqAlgo* algo, int console_output);
double peaq_algo_calculate_odg(PeaqAlgo* algo, int console_output);

#ifdef __cplusplus
} // extern "C" {
#endif