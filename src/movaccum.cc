/* GstPEAQ
 * Copyright (C) 2013, 2014, 2015, 2021 Martin Holters <martin.holters@hsu-hh.de>
 *
 * movaccum.c: Model out variable (MOV) accumulation.
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

#include "movaccum.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numeric>
#include <vector>

enum class Status
{
  INIT,
  NORMAL,
  TENTATIVE
};

struct Fraction
{
  double num{};
  double den{};
};

class movaccum
{
public:
  virtual ~movaccum() = default;
  virtual void set_channels(std::size_t channels) = 0;
  [[nodiscard]] virtual std::size_t get_channels() const = 0;
  virtual void set_tentative(bool tentative) = 0;
  virtual void accumulate(std::size_t c, double val, double weight) = 0;
  [[nodiscard]] virtual double get_value() const = 0;
};

template<typename Strategy>
class movaccumimpl : public movaccum
{
public:
  void set_channels(std::size_t channels) override
  {
    this->channels = channels;
    data.resize(channels);
    data_saved.resize(channels);
  }
  [[nodiscard]] std::size_t get_channels() const override { return channels; }
  void set_tentative(bool tentative) override
  {
    if (tentative) {
      if (status == Status::NORMAL) {
        /* transition to tentative status */
        save_data();
        status = Status::TENTATIVE;
      }
    } else {
      status = Status::NORMAL;
    }
  }
  void save_data()
  {
    std::transform(cbegin(data), cend(data), begin(data_saved), Strategy::save_data);
  }
  void accumulate(std::size_t c, double val, double weight) override
  {
    if (status != Status::INIT) {
      Strategy::accumulate(data[c], val, weight);
    }
  }

  [[nodiscard]] double get_value() const override
  {
    return status == Status::TENTATIVE ? get_value(data_saved) : get_value(data);
  }

  template<typename T>
  [[nodiscard]] auto get_value(T const& data) const
  {
    return std::accumulate(
             cbegin(data),
             cend(data),
             0.0,
             [](auto v, auto const& d) { return v + Strategy::get_value(d); }) /
           channels;
  }

private:
  Status status{ Status::INIT };
  PeaqMovAccumMode mode{ MODE_AVG };
  std::size_t channels{};
  std::vector<typename Strategy::Tdata> data;
  std::vector<typename Strategy::Tdatasaved> data_saved;
};

template<typename _Tdata, typename _Tdatasaved = _Tdata>
struct base_strategy
{
  using Tdata = _Tdata;
  using Tdatasaved = _Tdatasaved;
  static auto const& save_data(Tdata const& d) { return d; }
};

struct weighted_sum_strategy : base_strategy<Fraction>
{
  static void accumulate(Fraction& data, double val, double weight)
  {
    data.num += weight * val;
    data.den += weight;
  }
};

struct movaccum_adb_strategy : weighted_sum_strategy
{
  static auto get_value(Fraction const& frac)
  {
    if (frac.den <= 0) {
      return 0.0;
    }
    return frac.num == 0. ? -0.5 : std::log10(frac.num / frac.den);
  }
};

using movaccum_adb = movaccumimpl<movaccum_adb_strategy>;

struct movaccum_avg_strategy : weighted_sum_strategy
{
  static auto get_value(Fraction const& frac) { return frac.num / frac.den; }
};

using movaccum_avg = movaccumimpl<movaccum_avg_strategy>;

struct movaccum_avg_log_strategy : weighted_sum_strategy
{
  static auto get_value(Fraction const& frac)
  {
    return 10.0 * std::log10(frac.num / frac.den);
  }
};

using movaccum_avg_log = movaccumimpl<movaccum_avg_log_strategy>;

struct WinAvgData
{
  Fraction frac{};
  std::array<double, 3> past_sqrts{ NAN, NAN, NAN };
};

struct movaccum_avg_window_strategy : base_strategy<WinAvgData, Fraction>
{
  static void accumulate(Tdata& data, double val, double /*weight*/)
  {
    auto val_sqrt = std::sqrt(val);
    if (!std::isnan(data.past_sqrts[0])) {
      auto winsum =
        std::accumulate(cbegin(data.past_sqrts), cend(data.past_sqrts), val_sqrt);
      winsum /= 4.;
      winsum *= winsum;
      winsum *= winsum;
      data.frac.num += winsum;
      data.frac.den += 1.;
    }
    for (auto i = 0; i < 2; i++) {
      data.past_sqrts[i] = data.past_sqrts[i + 1];
    }
    data.past_sqrts[2] = val_sqrt;
  }
  static auto const& save_data(Tdata const& d) { return d.frac; }
  static auto get_value(Fraction const& frac) { return std::sqrt(frac.num / frac.den); }
  static auto get_value(Tdata const& winavgdat) { return get_value(winavgdat.frac); }
};

using movaccum_avg_window = movaccumimpl<movaccum_avg_window_strategy>;

struct FiltMaxData
{
  double max{};
  double filt_state{};
};

struct movaccum_filtered_max_strategy : base_strategy<FiltMaxData, double>
{
  static void accumulate(Tdata& data, double val, double /*weight*/)
  {
    data.filt_state = 0.9 * data.filt_state + 0.1 * val;
    if (data.filt_state > data.max) {
      data.max = data.filt_state;
    }
  }
  static auto save_data(Tdata const& dat) { return dat.max; }
  static auto get_value(Tdata const& dat) { return dat.max; }
  static auto get_value(double const& dat) { return dat; }
};

using movaccum_filtered_max = movaccumimpl<movaccum_filtered_max_strategy>;

struct movaccum_rms_strategy : base_strategy<Fraction>
{
  static void accumulate(Fraction& data, double val, double weight)
  {
    weight *= weight;
    data.num += weight * val * val;
    data.den += weight;
  }
  static auto get_value(Fraction const& frac) { return std::sqrt(frac.num / frac.den); }
};

using movaccum_rms = movaccumimpl<movaccum_rms_strategy>;

struct TwinFraction
{
  double num1{};
  double num2{};
  double den{};
};

struct movaccum_rms_asym_strategy : base_strategy<TwinFraction>
{
  static void accumulate(Tdata& data, double val1, double val2)
  {
    data.num1 += val1 * val1;
    data.num2 += val2 * val2;
    data.den += 1.;
  }
  static auto get_value(Tdata const& twinfrac)
  {
    return sqrt(twinfrac.num1 / twinfrac.den) + 0.5 * sqrt(twinfrac.num2 / twinfrac.den);
  }
};

using movaccum_rms_asym = movaccumimpl<movaccum_rms_asym_strategy>;

struct _PeaqMovAccum
{
  PeaqMovAccumMode mode{ MODE_AVG };
  std::unique_ptr<movaccum> impl{ new movaccum_avg };
};

PeaqMovAccum* peaq_movaccum_new()
{
  return new PeaqMovAccum;
}

void peaq_movaccum_delete(PeaqMovAccum* acc)
{
  delete acc;
}

void peaq_movaccum_set_channels(PeaqMovAccum* acc, unsigned int channels)
{
  acc->impl->set_channels(channels);
}

unsigned int peaq_movaccum_get_channels(PeaqMovAccum const* acc)
{
  return acc->impl->get_channels();
}

void peaq_movaccum_set_mode(PeaqMovAccum* acc, PeaqMovAccumMode mode)
{
  auto channels = acc->impl->get_channels();
  if (acc->mode != mode) {
    acc->mode = mode;
    switch (acc->mode) {
      case MODE_RMS:
        acc->impl = std::make_unique<movaccum_rms>();
        break;
      case MODE_AVG:
        acc->impl = std::make_unique<movaccum_avg>();
        break;
      case MODE_AVG_LOG:
        acc->impl = std::make_unique<movaccum_avg_log>();
        break;
      case MODE_ADB:
        acc->impl = std::make_unique<movaccum_adb>();
        break;
      case MODE_RMS_ASYM:
        acc->impl = std::make_unique<movaccum_rms_asym>();
        break;
      case MODE_AVG_WINDOW:
        acc->impl = std::make_unique<movaccum_avg_window>();
        break;
      case MODE_FILTERED_MAX:
        acc->impl = std::make_unique<movaccum_filtered_max>();
        break;
    }
    acc->impl->set_channels(channels);
  }
}

PeaqMovAccumMode peaq_movaccum_get_mode(PeaqMovAccum* acc)
{
  return acc->mode;
}

void peaq_movaccum_set_tentative(PeaqMovAccum* acc, int tentative)
{
  acc->impl->set_tentative(tentative);
}

void peaq_movaccum_accumulate(PeaqMovAccum* acc, unsigned int c, double val, double weight)
{
  acc->impl->accumulate(c, val, weight);
}

double peaq_movaccum_get_value(PeaqMovAccum const* acc)
{
  return acc->impl->get_value();
}
