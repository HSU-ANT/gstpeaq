/* GstPEAQ
 * Copyright (C) 2013, 2015, 2021 Martin Holters <martin.holters@hsu-hh.de>
 *
 * fbearmodel.h: Filer bank-based peripheral ear model part.
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


#ifndef __FBEARMODEL_H__
#define __FBEARMODEL_H__ 1

#include <glib-object.h>
#include "earmodel.h"

#ifdef __cplusplus
#include <array>
#include <complex>
#include <vector>

namespace peaq {

class FilterbankEarModel : PeaqEarModel
{
private:
  static constexpr std::size_t BUFFER_LENGTH = 1456;
  static constexpr std::size_t FB_NUMBANDS = 40;
  static constexpr double SLOPE_FILTER_A = 0.993355506255034; /* exp(-32 / (48000 * 0.1)) */
  static constexpr double DIST = 0.921851456499719; /* pow(0.1,(z[39]-z[0])/(39*20)) */
  static constexpr double CL = 0.0802581846102741;  /* pow(DIST, 31) */
  /* taken from Table 8 in [BS1387] */
  static constexpr std::array<std::size_t, FB_NUMBANDS> filter_length{
    1456, 1438, 1406, 1362, 1308, 1244, 1176, 1104, 1030, 956, 884, 814, 748, 686,
    626,  570,  520,  472,  430,  390,  354,  320,  290,  262, 238, 214, 194, 176,
    158,  144,  130,  118,  106,  96,   86,   78,   70,   64,  58,  52
  };
public:
  static constexpr std::size_t FB_FRAMESIZE = 192;
  struct state_t
  {
    double hpfilter1_x1{};
    double hpfilter1_x2{};
    double hpfilter1_y1{};
    double hpfilter1_y2{};
    double hpfilter2_y1{};
    double hpfilter2_y2{};
    std::array<double, 2 * BUFFER_LENGTH> fb_buf{};
    unsigned int fb_buf_offset{};
    std::array<double, BUFFER_LENGTH> cu{};
    std::array<std::array<double, 11>, FB_NUMBANDS> E0_buf{};
    std::array<double, FB_NUMBANDS> excitation{};
    std::array<double, FB_NUMBANDS> unsmeared_excitation{};
  };
  FilterbankEarModel();
  [[nodiscard]] auto playback_level() const { return 20. * std::log10(level_factor); }
  void set_playback_level(double level)
  {
    /* scale factor for playback level; (27) in [BS1387], (34) in [Kabal03] */
    level_factor = std::pow(10.0, level / 20.0);
  }
  void process_block(state_t& state, std::array<double, FB_FRAMESIZE>& sample_data) const;

private:
  double level_factor;
  std::array<std::vector<std::complex<double>>, 40> fbh;

  static const std::array<double, 6> back_mask_h;
  [[nodiscard]] auto apply_filter_bank(state_t const& state) const;
};
} // namespace peaq

using PeaqFilterbankEarModel = peaq::FilterbankEarModel;
extern "C" {
#else
/**
 * PeaqFilterbankEarModel:
 *
 * The opaque PeaqFilterbankEarModel structure.
 */
typedef struct _PeaqFilterbankEarModel PeaqFilterbankEarModel;
#endif

/**
 * SECTION:fbearmodel
 * @short_description: Filter-bank based ear model.
 * @title: PeaqFilterbankEarModel
 *
 * The processing is performed by calling peaq_earmodel_process_block(). The
 * first step is to
 * apply a DC rejection filter (high pass at 20 Hz) and decompose the signal
 * into 40 bands using an FIR filter bank. After weighting the individual bands
 * with the outer and middle ear filter, the signal energy in spread accross
 * frequency and time. Addition of the internal noise then yields the unsmeared
 * excitation patterns. Another time domain spreading finally gives the
 * excitation patterns.
 */


#define PEAQ_TYPE_FILTERBANKEARMODEL \
  (peaq_filterbankearmodel_get_type ())
#define PEAQ_FILTERBANKEARMODEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST (obj, PEAQ_TYPE_FILTERBANKEARMODEL, \
                               PeaqFilterbankEarModel))
#define PEAQ_FILTERBANKEARMODEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST (klass, PEAQ_TYPE_FILTERBANKEARMODEL, PeaqFilterbankEarModelClass))
#define PEAQ_FILTERBANKEARMODEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS (obj, PEAQ_TYPE_FILTERBANKEARMODEL, PeaqFilterbankEarModelClass))


/**
 * PeaqFilterbankEarModelClass:
 *
 * The opaque PeaqFilterbankEarModelClass structure.
 */
typedef struct _PeaqFilterbankEarModelClass PeaqFilterbankEarModelClass;

GType peaq_filterbankearmodel_get_type ();

#ifdef __cplusplus
}
#endif

#endif
