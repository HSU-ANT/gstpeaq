/* GstPEAQ
 * Copyright (C) 2013, 2014, 2015, 2021 Martin Holters <martin.holters@hsu-hh.de>
 *
 * movaccum.h: Model out variable (MOV) accumulation.
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

#ifndef __MOVACCUM_H__
#define __MOVACCUM_H__ 1

/**
 * SECTION:movaccum
 * @short_description: Model output variable accumulator.
 * @title: PeaqMovAccum
 *
 * For each frame of audio data, the diverse analysis methods yield a range of
 * intermediate values. These have to be accumulated to obtain the final Model
 * Output Variables (MOVs). Depending on the MOV, accumulation has to be
 * carried out in different ways, all of which can be handled by a
 * #PeaqMovAccum instance by setting the correct #PeaqMovAccumMode with
 * peaq_movaccum_set_mode().
 *
 * Ignoring quiet frames at the end of the data is supported with a tentative
 * mode. Once a quiet frame is detected, tentative mode can be activated and
 * any accumulation done will not be reflected in the accumulator value. Thus,
 * if the file ends while still in tentative mode (no louder frames have
 * occurred), the value before the first quiet frame will be used.  If,
 * however, a louder frame occurs, tentative mode can be deactived to commit
 * all accumulation done in the mean time.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <vector>

namespace peaq {

namespace detail {
template<typename Strategy>
class movaccumimpl
{
public:
  void set_channels(std::size_t channels)
  {
    this->channels = channels;
    data.resize(channels);
    data_saved.resize(channels);
  }
  [[nodiscard]] auto get_channels() const -> std::size_t { return channels; }
  void set_tentative(bool tentative)
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
  template<typename... Args>
  void accumulate(std::size_t c, Args... args) {
    if (status != Status::INIT) {
      Strategy::accumulate(data[c], args...);
    }
  }

  [[nodiscard]] auto get_value() const -> double
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
  enum class Status
  {
    INIT,
    NORMAL,
    TENTATIVE
  };
  Status status{ Status::INIT };
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

struct fraction
{
  double num{};
  double den{};
};

struct weighted_sum_strategy : base_strategy<fraction>
{
  static void accumulate(fraction& data, double val, double weight)
  {
    data.num += weight * val;
    data.den += weight;
  }
};

struct movaccum_adb_strategy : weighted_sum_strategy
{
  static auto get_value(fraction const& frac)
  {
    if (frac.den <= 0) {
      return 0.0;
    }
    return frac.num == 0. ? -0.5 : std::log10(frac.num / frac.den);
  }
};

struct movaccum_avg_strategy : weighted_sum_strategy
{
  static auto get_value(fraction const& frac) { return frac.num / frac.den; }
};

struct movaccum_avg_log_strategy : weighted_sum_strategy
{
  static auto get_value(fraction const& frac)
  {
    return 10.0 * std::log10(frac.num / frac.den);
  }
};

struct winavgdata
{
  fraction frac{};
  std::array<double, 3> past_sqrts{ NAN, NAN, NAN };
};

struct movaccum_avg_window_strategy : base_strategy<winavgdata, fraction>
{
  static void accumulate(Tdata& data, double val)
  {
    auto val_sqrt = std::sqrt(val);
    if (!std::isnan(data.past_sqrts[0])) {
      auto winsum =
        std::accumulate(cbegin(data.past_sqrts), cend(data.past_sqrts), val_sqrt) / 4.0;
      auto winsum_squared = winsum * winsum;
      data.frac.num += winsum_squared * winsum_squared;
      data.frac.den += 1.;
    }
    for (auto i = 0; i < 2; i++) {
      data.past_sqrts[i] = data.past_sqrts[i + 1];
    }
    data.past_sqrts[2] = val_sqrt;
  }
  static auto const& save_data(Tdata const& d) { return d.frac; }
  static auto get_value(fraction const& frac) { return std::sqrt(frac.num / frac.den); }
  static auto get_value(Tdata const& winavgdat) { return get_value(winavgdat.frac); }
};

struct filtmaxdata
{
  double max{};
  double filt_state{};
};

struct movaccum_filtered_max_strategy : base_strategy<filtmaxdata, double>
{
  static void accumulate(Tdata& data, double val)
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

struct movaccum_rms_strategy : base_strategy<fraction>
{
  static void accumulate(fraction& data, double val, double weight)
  {
    auto squared_weight = weight * weight;
    data.num += squared_weight * val * val;
    data.den += squared_weight;
  }
  static auto get_value(fraction const& frac) { return std::sqrt(frac.num / frac.den); }
};

struct twinfraction
{
  double num1{};
  double num2{};
  double den{};
};

struct movaccum_rms_asym_strategy : base_strategy<twinfraction>
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

} // namespace detail

using movaccum_adb = detail::movaccumimpl<detail::movaccum_adb_strategy>;
using movaccum_avg = detail::movaccumimpl<detail::movaccum_avg_strategy>;
using movaccum_avg_log = detail::movaccumimpl<detail::movaccum_avg_log_strategy>;
using movaccum_avg_window = detail::movaccumimpl<detail::movaccum_avg_window_strategy>;
using movaccum_filtered_max = detail::movaccumimpl<detail::movaccum_filtered_max_strategy>;
using movaccum_rms = detail::movaccumimpl<detail::movaccum_rms_strategy>;
using movaccum_rms_asym = detail::movaccumimpl<detail::movaccum_rms_asym_strategy>;

} // namespace peaq

/**
 * PeaqMovAccumMode:
 * @MODE_AVG: Linear averaging as decribed in section 5.2.1 of <xref
 * linkend="BS1387" />, used for Segmental NMR, Error Harmonic Structure,
 * Average Linear Distortion, Bandwidth, Average Moduation Difference, and
 * Relative Distorted Frames model output variables:
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>X</mi><mi>c</mi></msub>
 *   <mo>=</mo>
 *   <mfrac>
 *     <mrow>
 *       <munderover>
 *         <mo>&sum;</mo>
 *         <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *         <mi>N</mi>
 *       </munderover>
 *       <msub><mi>w</mi><mi>i</mi></msub>
 *       <mo>&InvisibleTimes;</mo>
 *       <msub><mi>x</mi><mi>i</mi></msub>
 *     </mrow>
 *     <mrow>
 *       <munderover>
 *         <mo>&sum;</mo>
 *         <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *         <mi>N</mi>
 *       </munderover>
 *       <msub><mi>w</mi><mi>i</mi></msub>
 *     </mrow>
 *   </mfrac>
 * </math></informalequation>
 * @MODE_AVG_LOG: A variant of linear averaging which takes a logarithm in the
 * end as needed for the Total NMR model output variable, see section 4.5.1 of
 * <xref linkend="BS1387" />:
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mrow>
 *     <msub><mi>X</mi><mi>c</mi></msub>
 *     <mo>=</mo>
 *     <mn>10</mn>
 *     <mo>&InvisibleTimes;</mo>
 *     <msub><mi>log</mi><mn>10</mn></msub>
 *     <mo>&ApplyFunction;</mo>
 *     <mfenced open="(" close=")">
 *       <mfrac>
 *         <mrow>
 *           <munderover>
 *             <mo>&sum;</mo>
 *             <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *             <mi>N</mi>
 *           </munderover>
 *           <msub><mi>w</mi><mi>i</mi></msub>
 *           <mo>&InvisibleTimes;</mo>
 *           <msub><mi>x</mi><mi>i</mi></msub>
 *         </mrow>
 *         <mrow>
 *           <munderover>
 *             <mo>&sum;</mo>
 *             <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *             <mi>N</mi>
 *           </munderover>
 *           <msub><mi>w</mi><mi>i</mi></msub>
 *         </mrow>
 *       </mfrac>
 *     </mfenced>
 *   </mrow>
 * </math></informalequation>
 * @MODE_RMS: Root-mean-square averaging as described in section 5.2.2 of <xref
 * linkend="BS1387" />, used for Modulation Difference and Noise Loudness model
 * output variables:
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>X</mi><mi>c</mi></msub>
 *   <mo>=</mo>
 *   <msqrt><mfrac>
 *     <mrow>
 *       <munderover>
 *         <mo>&sum;</mo>
 *         <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *         <mi>N</mi>
 *       </munderover>
 *       <msup><msub><mi>w</mi><mi>i</mi></msub><mn>2</mn></msup>
 *       <mo>&InvisibleTimes;</mo>
 *       <msup><msub><mi>x</mi><mi>i</mi></msub><mn>2</mn></msup>
 *     </mrow>
 *     <mrow>
 *       <munderover>
 *         <mo>&sum;</mo>
 *         <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *         <mi>N</mi>
 *       </munderover>
 *       <msup><msub><mi>w</mi><mi>i</mi></msub><mn>2</mn></msup>
 *     </mrow>
 *   </mfrac></msqrt>
 * </math></informalequation>
 * Note that the factor <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msqrt><mi>Z</mi></msqrt>
 * </math></inlineequation>
 * introduced in <xref linkend="BS1387" /> for the weighted case only is not
 * included here but has be included in the calculation of <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>x</mi><mi>i</mi></msub>
 * </math></inlineequation>
 * or when using the output of the accumulator for further calculations.
 * @MODE_RMS_ASYM: A variant of root-mean-square averaging used for the
 * Asymmetric Noise Loudness model output variable, see section 4.3.3 of <xref
 * linkend="BS1387" />:
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mrow>
 *     <msub><mi>X</mi><mi>c</mi></msub>
 *     <mo>=</mo>
 *     <msqrt>
 *       <mrow>
 *         <mfrac><mn>1</mn><mi>N</mi></mfrac>
 *         <mo>&InvisibleTimes;</mo>
 *         <munderover>
 *           <mo>&sum;</mo>
 *           <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *           <mi>N</mi>
 *         </munderover>
 *         <msup><msub><mi>x</mi><mi>i</mi></msub><mn>2</mn></msup>
 *       </mrow>
 *     </msqrt>
 *     <mo>+</mo>
 *     <mfrac><mn>1</mn><mn>2</mn></mfrac>
 *     <mo>&InvisibleTimes;</mo>
 *     <msqrt>
 *       <mrow>
 *         <mfrac><mn>1</mn><mi>N</mi></mfrac>
 *         <mo>&InvisibleTimes;</mo>
 *         <munderover>
 *           <mo>&sum;</mo>
 *           <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *           <mi>N</mi>
 *         </munderover>
 *         <msup><msub><mi>w</mi><mi>i</mi></msub><mn>2</mn></msup>
 *       </mrow>
 *     </msqrt>
 *   </mrow>
 * </math></informalequation>
 * @MODE_AVG_WINDOW: Windowed averaging as described in section 5.2.3 of <xref
 * linkend="BS1387" />, used for Modulation Difference model output variable:
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>X</mi><mi>c</mi></msub>
 *   <mo>=</mo>
 *   <msqrt>
 *     <mrow>
 *       <mfrac><mn>1</mn><mi>N</mi></mfrac>
 *       <mo>&InvisibleTimes;</mo>
 *       <munderover>
 *         <mo>&sum;</mo>
 *         <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *         <mi>N</mi>
 *       </munderover>
 *       <msup>
 *         <mfenced open="(" close=")">
 *           <mrow>
 *             <mfrac><mn>1</mn><mn>4</mn></mfrac>
 *             <mo>&InvisibleTimes;</mo>
 *             <munderover>
 *               <mo>&sum;</mo>
 *               <mrow><mi>j</mi><mo>=</mo><mn>i</mn><mo>-</mo><mn>3</mn></mrow>
 *               <mi>i</mi>
 *             </munderover>
 *             <msqrt><msub><mi>x</mi><mi>j</mi></msub></msqrt>
 *           </mrow>
 *         </mfenced>
 *         <mn>4</mn>
 *       </msup>
 *     </mrow>
 *   </msqrt>
 * </math></informalequation>
 * Note that no model output variable obtained from the filter bank ear model
 * uses windowed averaging, hence the longer averaging window defined in <xref
 * linkend="BS1387" /> is not supported.
 * @MODE_FILTERED_MAX: Filtered maximum as used by the Maximum Filtered
 * Probability of Detection model output variable, see section 4.7.1 in <xref
 * linkend="BS1387" />:
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mrow>
 *     <msub><mi>X</mi><mi>c</mi></msub>
 *     <mo>=</mo>
 *     <mi>max</mi>
 *     <mo>&ApplyFunction;</mo>
 *     <mfenced open="{" close="}">
 *       <msub><mi>y</mi><mi>i</mi></msub>
 *     </mfenced>
 *     <mo> where </mo>
 *     <msub><mi>y</mi><mi>i</mi></msub>
 *     <mo>=</mo>
 *     <mn>0.9</mn>
 *     <mo>&InvisibleTimes;</mo>
 *     <msub><mi>y</mi><mrow><mi>i</mi><mo>-</mo><mn>1</mn></mrow></msub>
 *     <mo>+</mo>
 *     <mn>0.1</mn>
 *     <mo>&InvisibleTimes;</mo>
 *     <msub><mi>x</mi><mi>i</mi></msub>
 *   </mrow>
 * </math></informalequation>
 * @MODE_ADB: Special accumulation mode for the Average Distorted Block model
 * output variable, see section 4.7.2 in <xref linkend="BS1387" /> and note
 * that <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>w</mi><mi>i</mi></msub></math></inlineequation> should always be
 * set to one:
 * <informalequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mrow>
 *     <msub><mi>X</mi><mi>c</mi></msub>
 *     <mo>=</mo>
 *     <mfenced open="{" close="">
 *       <mtable>
 *         <mtr>
 *           <mtd><mn>0</mn></mtd>
 *           <mtd columnalign="left">
 *             <mo>for </mo>
 *             <munderover>
 *               <mo>&sum;</mo>
 *               <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *               <mi>N</mi>
 *             </munderover>
 *             <msub><mi>w</mi><mi>i</mi></msub>
 *             <mo>=</mo><mn>0</mn>
 *           </mtd>
 *         </mtr>
 *         <mtr>
 *           <mtd><mn>-0.5</mn></mtd>
 *           <mtd columnalign="left">
 *             <mo>for </mo>
 *             <munderover>
 *               <mo>&sum;</mo>
 *               <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *               <mi>N</mi>
 *             </munderover>
 *             <msub><mi>w</mi><mi>i</mi></msub>
 *             <mo>&InvisibleTimes;</mo>
 *             <msub><mi>x</mi><mi>i</mi></msub>
 *             <mo>=</mo><mn>0</mn>
 *             <mtext>,</mtext><mspace width="1ex" />
 *             <munderover>
 *               <mo>&sum;</mo>
 *               <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *               <mi>N</mi>
 *             </munderover>
 *             <msub><mi>w</mi><mi>i</mi></msub>
 *             <mo>&ne;</mo><mn>0</mn>
 *           </mtd>
 *         </mtr>
 *         <mtr>
 *           <mtd>
 *             <msub><mi>log</mi><mn>10</mn></msub>
 *             <mo>&ApplyFunction;</mo>
 *             <mfenced open="(" close=")">
 *               <mfrac>
 *                 <mrow>
 *                   <munderover>
 *                     <mo>&sum;</mo>
 *                     <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *                     <mi>N</mi>
 *                   </munderover>
 *                   <msub><mi>w</mi><mi>i</mi></msub>
 *                   <mo>&InvisibleTimes;</mo>
 *                   <msub><mi>x</mi><mi>i</mi></msub>
 *                 </mrow>
 *                 <mrow>
 *                   <munderover>
 *                     <mo>&sum;</mo>
 *                     <mrow><mi>i</mi><mo>=</mo><mn>1</mn></mrow>
 *                     <mi>N</mi>
 *                   </munderover>
 *                   <msub><mi>w</mi><mi>i</mi></msub>
 *                 </mrow>
 *               </mfrac>
 *             </mfenced>
 *           </mtd>
 *           <mtd columnalign="left"><mo>else</mo></mtd>
 *         </mtr>
 *       </mtable>
 *     </mfenced>
 *   </mrow>
 * </math></informalequation>
 *
 * Accumulation mode of the model output variable. For all channels <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>c</mi>
 * </math></inlineequation>, the accumulation over time steps <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>i</mi>
 * </math></inlineequation> is performed independently according to the
 * formulas below, where <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>x</mi><mi>i</mi></msub></math></inlineequation> and <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>w</mi><mi>i</mi></msub></math></inlineequation> denote the inputs
 * to peaq_movaccum_accumulate(). The resulting per-channel values <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>X</mi><mi>c</mi></msub></math></inlineequation> are averaged to
 * obtain the final result as returned by peaq_movaccum_get_value().
 */

/**
 * peaq_movaccum_set_tentative:
 * @acc: The @PeaqMovAccum of which to control the tentative state.
 * @tentative: Whether to enable or disable the tentative state.
 *
 * Enables or disables tentative state. In tentative state, values are
 * accumulated as usual when calling peaq_movaccum_accumulate(), but the value
 * returned by peaq_movaccum_get_value() is kept at the value it had
 * immediately before setting tentative state. Once tentative state is
 * disabled, the final value is updated to include all values accumulated
 * during tentative state.
 */

#endif
