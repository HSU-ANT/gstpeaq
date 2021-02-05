/* GstPEAQ
 * Copyright (C) 2006, 2007, 2011, 2012, 2013, 2014, 2015
 * Martin Holters <martin.holters@hsu-hh.de>
 *
 * fftearmodel.c: FFT-based peripheral ear model part.
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

#include "fftearmodel.h"
#include "gstpeaq.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <new>
#include <numeric>

namespace peaq {

enum
{
  PROP_0,
  PROP_BAND_COUNT
};

struct PeaqFFTEarModelClass
{
  PeaqEarModelClass parent;
  PeaqFFTEarModelClass();
};

static void class_init(gpointer klass, gpointer class_data);
static void init(GTypeInstance* obj, gpointer klass);
static void finalize(GObject* obj);
static gdouble get_playback_level(PeaqEarModel const* model);
static void set_playback_level(PeaqEarModel* model, double level);
static gpointer state_alloc(PeaqEarModel const* model);
static void state_free(PeaqEarModel const* model, gpointer state);
static void process_block(PeaqEarModel const* model,
                          gpointer state,
                          gfloat const* sample_data);
static gdouble const* get_excitation(PeaqEarModel const* model, gpointer state);
static gdouble const* get_unsmeared_excitation(PeaqEarModel const* model, gpointer state);
static void get_property(GObject* obj, guint id, GValue* value, GParamSpec* pspec);
static void set_property(GObject* obj, guint id, const GValue* value, GParamSpec* pspec);

const std::array<double, FFTEarModel::FFT_FRAMESIZE> FFTEarModel::hann_window = [] {
  std::array<double, FFTEarModel::FFT_FRAMESIZE> win;
  for (std::size_t k = 0; k < win.size(); k++) {
    win[k] = std::sqrt(8. / 3.) * 0.5 * (1. - std::cos(2 * M_PI * k / (win.size() - 1)));
  }
  return win;
}();

FFTEarModel::FFTEarModel()
{
  /* pre-compute weighting coefficients for outer and middle ear weighting
   * function; (7) in [BS1387], (6) in [Kabal03], but taking the squared value
   * for applying in the power domain */
  auto sampling_rate = peaq_earmodel_get_sampling_rate(PEAQ_EARMODEL(this));
  for (std::size_t k = 0; k <= FFT_FRAMESIZE / 2; k++) {
    outer_middle_ear_weight[k] =
      std::pow(peaq_earmodel_calc_ear_weight(double(k) * sampling_rate / FFT_FRAMESIZE), 2);
  }
}

void FFTEarModel::set_bandcount(std::size_t bandcount)
{
  deltaZ = 27. / (bandcount - 1);
  auto zL = 7. * std::asinh(80. / 650.);
  auto zU = 7. * std::asinh(18000. / 650.);
  g_assert(bandcount == std::ceil((zU - zL) / deltaZ));

  GArray* fc_array = g_array_sized_new(FALSE, FALSE, sizeof(gdouble), bandcount);
  band_lower_end.resize(bandcount);
  band_upper_end.resize(bandcount);
  band_lower_weight.resize(bandcount);
  band_upper_weight.resize(bandcount);
  spreading_normalization.resize(bandcount);
  aUC.resize(bandcount);
  gIL.resize(bandcount);
  masking_difference.resize(bandcount);

  lower_spreading = std::pow(10., -2.7 * deltaZ); /* 1 / a_L */
  lower_spreading_exponantiated = std::pow(lower_spreading, 0.4);

  auto sampling_rate = peaq_earmodel_get_sampling_rate(PEAQ_EARMODEL(this));

  for (std::size_t band = 0; band < bandcount; band++) {
    auto zl = zL + band * deltaZ;
    auto zu = MIN(zU, zL + (band + 1) * deltaZ);
    auto zc = (zu + zl) / 2.;
    auto curr_fc = 650. * std::sinh(zc / 7.);
    g_array_append_val(fc_array, curr_fc);

    /* pre-compute helper data for group_into_bands()
     * The precomputed data is as proposed in [Kabal03], but the
     * algorithm to compute is somewhat simplified */
    auto fl = 650. * std::sinh(zl / 7.);
    auto fu = 650. * std::sinh(zu / 7.);
    band_lower_end[band] = std::lround(fl / sampling_rate * FFT_FRAMESIZE);
    band_upper_end[band] = std::lround(fu / sampling_rate * FFT_FRAMESIZE);
    auto upper_freq = (2 * band_lower_end[band] + 1) / 2. * sampling_rate / FFT_FRAMESIZE;
    if (upper_freq > fu) {
      upper_freq = fu;
    }
    auto U = upper_freq - fl;
    band_lower_weight[band] = U * FFT_FRAMESIZE / sampling_rate;
    if (band_lower_end[band] == band_upper_end[band]) {
      band_upper_weight[band] = 0;
    } else {
      auto lower_freq = (2 * band_upper_end[band] - 1) / 2. * sampling_rate / FFT_FRAMESIZE;
      U = fu - lower_freq;
      band_upper_weight[band] = U * FFT_FRAMESIZE / sampling_rate;
    }

    /* pre-compute internal noise, time constants for time smearing,
     * thresholds and helper data for spreading */
    const auto aL = lower_spreading;
    aUC[band] = std::pow(10., (-2.4 - 23. / curr_fc) * deltaZ);
    gIL[band] = (1. - std::pow(aL, band + 1)) / (1. - aL);
    spreading_normalization[band] = 1.;

    /* masking weighting function; (25) in [BS1387], (112) in [Kabal03] */
    masking_difference[band] =
      pow(10., (band * deltaZ <= 12. ? 3. : 0.25 * band * deltaZ) / 10.);
  }

  g_object_set(G_OBJECT(this), "band-centers", fc_array, NULL);
  g_array_unref(fc_array);

  double* spread = g_newa(gdouble, bandcount);
  do_spreading(spreading_normalization.data(), spread);
  std::copy_n(spread, bandcount, begin(spreading_normalization));
}

void FFTEarModel::process_block(state_t* state,
                                std::array<float, FFT_FRAMESIZE> const& sample_data) const
{
  PeaqEarModel const* model = PEAQ_EARMODEL(this);
  std::array<double, FFT_FRAMESIZE> windowed_data;
  std::array<std::complex<double>, FFT_FRAMESIZE / 2 + 1> fftoutput;
  double* band_power = g_newa(double, peaq_earmodel_get_band_count(model));
  double* noisy_band_power = g_newa(double, peaq_earmodel_get_band_count(model));

  /* apply a Hann window to the input data frame; (3) in [BS1387], part of (4)
   * in [Kabal03] */
  std::transform(cbegin(hann_window),
                 cend(hann_window),
                 cbegin(sample_data),
                 begin(windowed_data),
                 std::multiplies<>());

  /* apply FFT to windowed data; (4) in [BS1387] and part of (4) in [Kabal03],
   * but without division by FFT_FRAMESIZE, which is subsumed in the
   * level_factor applied next */
  gst_fft_f64_fft(gstfft.get(),
                  windowed_data.data(),
                  reinterpret_cast<GstFFTF64Complex*>(fftoutput.data()));

  for (std::size_t k = 0; k < FFT_FRAMESIZE / 2 + 1; k++) {
    /* compute power spectrum and apply scaling depending on playback level; in
     * [BS1387], the scaling is applied on the magnitudes, so the factor is
     * squared when comparing to [BS1387] (and also includes the squared
     * division by FFT_FRAMESIZE) */
    state->power_spectrum[k] = std::norm(fftoutput[k]) * level_factor;

    /* apply outer and middle ear weighting; (9) in [BS1387] (but in the power
     * domain), (8) in [Kabal03] */
    state->weighted_power_spectrum[k] =
      state->power_spectrum[k] * outer_middle_ear_weight[k];
  }

  /* group the outer ear weighted FFT outputs into critical bands according to
   * section 2.1.5 of [BS1387] / section 2.6 of [Kabal03] */
  group_into_bands(state->weighted_power_spectrum, band_power);

  /* add the internal noise to obtain the pitch patters; (14) in [BS1387], (17)
   * in [Kabal03] */
  for (std::size_t i = 0; i < model->band_count; i++) {
    noisy_band_power[i] = band_power[i] + peaq_earmodel_get_internal_noise(model, i);
  }

  /* do (frequency) spreading according to section 2.1.7 in [BS1387] / section
   * 2.8 in [Kabal03] */
  do_spreading(noisy_band_power, state->unsmeared_excitation.data());

  /* do time domain spreading according to section 2.1.8 of [BS1387] / section
   * 2.9 of [Kabal03]
   * NOTE: according to [BS1387], the filtered_excitation after processing the
   * first frame should be all zero; we follow the interpretation of [Kabal03]
   * and only initialize to zero before the first frame. */
  for (std::size_t i = 0; i < model->band_count; i++) {
    auto a = peaq_earmodel_get_ear_time_constant(model, i);
    state->filtered_excitation[i] =
      a * state->filtered_excitation[i] + (1. - a) * state->unsmeared_excitation[i];
    state->excitation[i] =
      std::max(state->filtered_excitation[i], state->unsmeared_excitation[i]);
  }

  /* check whether energy threshold has been reached, see section 5.2.4.3 in
   * [BS1387] */
  auto energy = std::accumulate(cbegin(sample_data) + FFT_FRAMESIZE / 2,
                                cend(sample_data),
                                0.0,
                                [](auto e, auto x) { return e + x * x; });
  if (energy >= 8000. / (32768. * 32768.)) {
    state->energy_threshold_reached = TRUE;
  } else {
    state->energy_threshold_reached = FALSE;
  }
}

void FFTEarModel::group_into_bands(
  std::array<double, FFT_FRAMESIZE / 2 + 1> const& spectrum,
  double* band_power) const
{
  for (std::size_t i = 0; i < PEAQ_EARMODEL(this)->band_count; i++) {
    auto edge_power = band_lower_weight[i] * spectrum[band_lower_end[i]] +
                      band_upper_weight[i] * spectrum[band_upper_end[i]];
    band_power[i] = std::accumulate(cbegin(spectrum) + band_lower_end[i] + 1,
                                    cbegin(spectrum) + band_upper_end[i],
                                    edge_power);
    if (band_power[i] < 1e-12) {
      band_power[i] = 1e-12;
    }
  }
}

/* this computation follows the algorithm in [Kabal03] where the
 * correspondances between variables in the code and in [Kabal[03] are as
 * follows:
 *
 *    code     | [Kabal03]
 *    ---------+----------
 *    aLe      | a_L^-0.4
 *    aUCE     | a_Ua_C[l]a_E(E)
 *    gIU      | (1-(a_Ua_C[l]a_E(E))^(N_c-l)) / (1-a_Ua_C[l]a_E(E))
 *    En       | E[l] / A(l,E)
 *    aUCEe    | (a_Ua_C[l]a_E(E))^0.4
 *    Ene      | (E[l] / A(l,E))^0.4
 *    E2       | Es[l]
 */
void FFTEarModel::do_spreading(gdouble const* Pp, gdouble* E2) const
{
  auto band_count = peaq_earmodel_get_band_count(PEAQ_EARMODEL(this));
  auto* aUCEe = g_newa(gdouble, parent.band_count);
  auto* Ene = g_newa(gdouble, parent.band_count);
  const auto aLe = lower_spreading_exponantiated;

  g_assert(band_count > 0);

  for (std::size_t i = 0; i < band_count; i++) {
    /* from (23) in [Kabal03] */
    auto aUCE = aUC[i] * std::pow(Pp[i], 0.2 * deltaZ);
    /* part of (24) in [Kabal03] */
    auto gIU = (1. - std::pow(aUCE, band_count - i)) / (1. - aUCE);
    /* Note: (24) in [Kabal03] is wrong; indeed it gives A(l,E) instead of
     * A(l,E)^-1 */
    auto En = Pp[i] / (gIL[i] + gIU - 1.);
    aUCEe[i] = std::pow(aUCE, 0.4);
    Ene[i] = std::pow(En, 0.4);
  }
  /* first fill E2 with E_sL according to (28) in [Kabal03] */
  E2[band_count - 1] = Ene[band_count - 1];
  for (auto i = band_count - 1; i > 0; i--) {
    E2[i - 1] = aLe * E2[i] + Ene[i - 1];
  }
  /* now add E_sU to E2 according to (27) in [Kabal03] (with rearranged
   * ordering) */
  for (std::size_t i = 0; i < band_count - 1; i++) {
    std::transform(
      E2 + i + 1, E2 + band_count, E2 + i + 1, [&r = Ene[i], aUCEe_i = aUCEe[i]](auto E2j) {
        r *= aUCEe_i;
        return E2j + r;
      });
  }
  /* compute end result by normalizing according to (25) in [Kabal03] */
  std::transform(
    E2,
    E2 + band_count,
    cbegin(spreading_normalization),
    E2,
    [](auto E2i, auto spread_norm) { return std::pow(E2i, 1.0 / 0.4) / spread_norm; });
}

PeaqFFTEarModelClass::PeaqFFTEarModelClass()
{
  GObjectClass* object_class = G_OBJECT_CLASS(this);
  PeaqEarModelClass* ear_model_class = PEAQ_EARMODEL_CLASS(this);

  /* override finalize method */
  object_class->finalize = finalize;
  object_class->set_property = set_property;
  object_class->get_property = get_property;
  /**
   * PeaqFFTEarModel:number-of-bands:
   *
   * The number of frequency bands to use. Should be 109 for the basic PEAQ
   * version and 55 for the advanced PEAQ version.
   */
  g_object_class_install_property(
    object_class,
    PROP_BAND_COUNT,
    g_param_spec_uint("number-of-bands",
                      "number of bands",
                      "Number of bands (55 or 109)",
                      55,
                      109,
                      109,
                      GParamFlags(G_PARAM_READWRITE | G_PARAM_CONSTRUCT)));

  ear_model_class->get_playback_level = get_playback_level;
  ear_model_class->set_playback_level = set_playback_level;
  ear_model_class->state_alloc = state_alloc;
  ear_model_class->state_free = state_free;
  ear_model_class->process_block = process_block;
  ear_model_class->get_excitation = get_excitation;
  ear_model_class->get_unsmeared_excitation = get_unsmeared_excitation;

  ear_model_class->loudness_scale = FFTEarModel::LOUDNESS_SCALE;
  ear_model_class->frame_size = FFTEarModel::FFT_FRAMESIZE;
  ear_model_class->step_size = FFTEarModel::FFT_FRAMESIZE / 2;
  ear_model_class->tau_min = 0.008;
  ear_model_class->tau_100 = 0.030;
}

/*
 * class_init:
 * @klass: pointer to the uninitialized class structure.
 * @class_data: pointer to data specified when registering the class (unused).
 *
 * Sets up the class data which mainly involves pre-computing helper data.
 */
static void class_init(gpointer klass, gpointer /*class_data*/)
{
  new (klass) PeaqFFTEarModelClass;
}

/*
 * init:
 * @obj: Pointer to the unitialized #PeaqEarModel structure.
 * @klass: The class structure of the class being instantiated.
 *
 * Initializes one instance of #FFTEarModel, in particular, the state
 * variables for the time smearing are allocated and initialized to zero.
 */
static void init(GTypeInstance* obj, gpointer /*klass*/)
{
  new (obj) FFTEarModel;
}

/*
 * finalize:
 * @obj: Pointer to the #FFTEarModel to be finalized.
 *
 * Disposes the given instance of #FFTEarModel.
 */
static void finalize(GObject* obj)
{
  PEAQ_FFTEARMODEL(obj)->~FFTEarModel();
  GObjectClass* parent_class =
    G_OBJECT_CLASS(g_type_class_peek_parent(g_type_class_peek(PEAQ_TYPE_FFTEARMODEL)));
  parent_class->finalize(obj);
}

static gdouble get_playback_level(PeaqEarModel const* model)
{
  return PEAQ_FFTEARMODEL(model)->playback_level();
}

static void set_playback_level(PeaqEarModel* model, gdouble level)
{
  PEAQ_FFTEARMODEL(model)->set_playback_level(level);
}

static gpointer state_alloc(PeaqEarModel const* model)
{
  return new FFTEarModel::state_t{ model->band_count };
}

static void state_free(PeaqEarModel const* /*model*/, gpointer state)
{
  delete ((FFTEarModel::state_t*)state);
}

/*
 * process_block:
 * @model: the #FFTEarModel instance structure.
 * @state: the state data
 * @sample_data: pointer to a frame of #FFT_FRAMESIZE samples to be processed.
 *
 * Performs the computation described in section 2.1 of <xref linkend="BS1387"
 * /> and section 2 of <xref linkend="Kabal03" /> for one single frame of
 * single-channel data. The input is assumed to be sampled at 48 kHz. To follow
 * the specification, the frames of successive invocations of
 * peaq_ear_process() have to overlap by 50%.
 *
 * The first step is to apply a Hann window to the input frame and transform
 * it to the frequency domain using FFT. The squared magnitude of the frequency
 * coefficients
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msup>
 *     <mfenced open="|" close="|"><mrow>
 *       <mi>F</mi>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow></mfenced>
 *     <mn>2</mn>
 *   </msup>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />,
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msub><mi>G</mi><mi>L</mi></msub>
 *   <mo>&InvisibleTimes;</mo>
 *   <msup>
 *     <mfenced open="|" close="|"><mrow>
 *       <mi>X</mi>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow></mfenced>
 *     <mn>2</mn>
 *   </msup>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />) up to half the frame length are stored in
 * <structfield>power_spectrum</structfield> of @output. Next, the outer and
 * middle ear weights are applied in the frequency domain and the result
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msup>
 *     <mfenced open="(" close=")">
 *       <mrow>
 *         <msub> <mi>F</mi> <mi>e</mi> </msub>
 *         <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *       </mrow>
 *     </mfenced>
 *     <mn>2</mn>
 *   </msup>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />,
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msup>
 *     <mfenced open="|" close="|"><mrow>
 *       <msub><mi>X</mi><mi>w</mi></msub>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow></mfenced>
 *     <mn>2</mn>
 *   </msup>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />) is stored in
 * <structfield>weighted_power_spectrum</structfield> of @output. These
 * weighted spectral coefficients are then grouped into critical bands, the
 * internal noise is added and the result is spread over frequecies to obtain
 * the unsmeared excitation patterns
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mrow>
 *     <msub> <mi>E</mi> <mn>2</mn> </msub>
 *     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *   </mrow>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />,
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mrow>
 *     <msub> <mi>E</mi> <mn>s</mn> </msub>
 *     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *   </mrow>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />), which are is stored in
 * <structfield>unsmeared_excitation</structfield> of @output.
 * Finally, further spreading over time yields the excitation patterns
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mrow>
 *     <mi>E</mi>
 *     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *   </mrow>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />,
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mrow>
 *     <msub> <mover accent="true"><mi>E</mi><mo>~</mo></mover> <mn>s</mn> </msub>
 *     <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *   </mrow>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />), which are is stored in
 * <structfield>excitation</structfield> of @output.
 */
static void process_block(PeaqEarModel const* model,
                          gpointer state,
                          gfloat const* sample_data)
{
  std::array<float, FFTEarModel::FFT_FRAMESIZE> data;
  std::copy_n(sample_data, FFTEarModel::FFT_FRAMESIZE, begin(data));
  PEAQ_FFTEARMODEL(model)->process_block((FFTEarModel::state_t*)state, data);
}

static gdouble const* get_excitation(PeaqEarModel const* /*model*/, gpointer state)
{
  return ((peaq::FFTEarModel::state_t*)state)->excitation.data();
}

static gdouble const* get_unsmeared_excitation(PeaqEarModel const* /*model*/,
                                               gpointer state)
{
  return ((peaq::FFTEarModel::state_t*)state)->unsmeared_excitation.data();
}

static void get_property(GObject* obj, guint id, GValue* value, GParamSpec* pspec)
{
  PeaqEarModel* model = PEAQ_EARMODEL(obj);
  switch (id) {
    case peaq::PROP_BAND_COUNT:
      g_value_set_uint(value, peaq_earmodel_get_band_count(model));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, pspec);
      break;
  }
}

static void set_property(GObject* obj, guint id, const GValue* value, GParamSpec* pspec)
{
  switch (id) {
    case peaq::PROP_BAND_COUNT:
      PEAQ_FFTEARMODEL(obj)->set_bandcount(g_value_get_uint(value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, id, pspec);
      break;
  }
}
} // namespace peaq

/*
 * peaq_fftearmodel_get_type:
 *
 * Registers the type GstPeaqFFTEarModel if not already done so and
 * returns the respective #GType.
 *
 * Returns: the type for GstPeaqFFTEarModel.
 */
GType peaq_fftearmodel_get_type()
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof(peaq::PeaqFFTEarModelClass), /* class_size */
      nullptr,                            /* base_init */
      nullptr,                            /* base_finalize */
      peaq::class_init,                   /* class_init */
      nullptr,                            /* class_finalize */
      nullptr,                            /* class_data */
      sizeof(peaq::FFTEarModel),          /* instance_size */
      0,                                  /* n_preallocs */
      peaq::init                          /* instance_init */
    };
    type =
      g_type_register_static(PEAQ_TYPE_EARMODEL, "PeaqFFTEarModel", &info, GTypeFlags(0));
  }
  return type;
}

void peaq_fftearmodel_group_into_bands(PeaqFFTEarModel const* model,
                                       gdouble const* spectrum,
                                       gdouble* band_power)
{
  std::array<double, peaq::FFTEarModel::FFT_FRAMESIZE / 2 + 1> spec;
  std::copy_n(spectrum, peaq::FFTEarModel::FFT_FRAMESIZE / 2 + 1, begin(spec));
  model->group_into_bands(spec, band_power);
}

gdouble const* peaq_fftearmodel_get_power_spectrum(gpointer state)
{
  return peaq::FFTEarModel::get_power_spectrum(
           *reinterpret_cast<peaq::FFTEarModel::state_t*>(state))
    .data();
}

gdouble const* peaq_fftearmodel_get_weighted_power_spectrum(gpointer state)
{
  return peaq::FFTEarModel::get_weighted_power_spectrum(
           *reinterpret_cast<peaq::FFTEarModel::state_t*>(state))
    .data();
}

gboolean peaq_fftearmodel_is_energy_threshold_reached(gpointer state)
{
  return peaq::FFTEarModel::is_energy_threshold_reached(
           *reinterpret_cast<peaq::FFTEarModel::state_t*>(state))
           ? TRUE
           : FALSE;
}

gdouble const* peaq_fftearmodel_get_masking_difference(PeaqFFTEarModel const* model)
{
  return model->get_masking_difference().data();
}
