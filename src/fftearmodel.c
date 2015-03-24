/* GstPEAQ
 * Copyright (C) 2006, 2007, 2011, 2012, 2013, 2015
 * Martin Holters <martin.holters@hsuhh.de>
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

/**
 * SECTION:fftearmodel
 * @short_description: Transforms a time domain signal into pitch domain 
 * excitation patterns.
 * @title: PeaqFFTEarModel
 *
 * The main processing is performed by calling peaq_earmodel_process_block().
 * The first step is to
 * apply a Hann window and transform the frame to the frequency domain. Then, a
 * filter modelling the effects of the outer and middle ear is applied by
 * weighting the spectral coefficients. These are grouped into frequency bands
 * of one fourth or half the width of the critical bands of auditoriy
 * perception to reach the pitch domain. To model the internal noise of the
 * ear, a pitch-dependent term is added to the power in each band. Finally,
 * spreading in the pitch domain and smearing in the time-domain are performed.
 * The necessary state information for the time smearing is stored in the state
 * data passed between successive calls to peaq_earmodel_process_block().
 *
 * The computation is thoroughly described in section 2 of 
 * <xref linkend="Kabal03" />.
 */

#include "fftearmodel.h"
#include "gstpeaq.h"

#include <math.h>
#include <gst/fft/gstfftf64.h>

#define FFT_FRAMESIZE 2048
#define GAMMA 0.84971762641205
#define LOUDNESS_SCALE 1.07664

typedef struct _PeaqFFTEarModelState PeaqFFTEarModelState;

enum
{
  PROP_0,
  PROP_BAND_COUNT
};

/**
 * PeaqFFTEarModel:
 *
 * The opaque PeaqFFTEarModel structure.
 */
struct _PeaqFFTEarModel
{
  PeaqEarModel parent;
  GstFFTF64 *gstfft;
  gdouble *outer_middle_ear_weight;
  gdouble deltaZ;
  gdouble level_factor;
  guint *band_lower_end;
  guint *band_upper_end;
  gdouble *band_lower_weight;
  gdouble *band_upper_weight;
  gdouble lower_spreading;
  gdouble lower_spreading_exponantiated;
  gdouble *spreading_normalization;
  gdouble *aUC;
  gdouble *gIL;
  gdouble *masking_difference;
};

/**
 * PeaqFFTEarModelClass:
 *
 * The opaque PeaqFFTEarModelClass structure.
 */
struct _PeaqFFTEarModelClass
{
  PeaqEarModelClass parent;
  gdouble *hann_window;
};

struct _PeaqFFTEarModelState {
  gdouble *filtered_excitation;
  gdouble *unsmeared_excitation;
  gdouble *excitation;
  gdouble power_spectrum[FFT_FRAMESIZE / 2 + 1];
  gdouble weighted_power_spectrum[FFT_FRAMESIZE / 2 + 1];
  gboolean energy_threshold_reached;
};

static void class_init (gpointer klass, gpointer class_data);
static void init (GTypeInstance *obj, gpointer klass);
static void finalize (GObject *obj);
static gdouble get_playback_level (PeaqEarModel const *model);
static void set_playback_level (PeaqEarModel *model, double level);
static gpointer state_alloc (PeaqEarModel const *model);
static void state_free (PeaqEarModel const *model, gpointer state);
void process_block (PeaqEarModel const *model, gpointer state,
                    gfloat const *sample_data);
static gdouble const *get_excitation (PeaqEarModel const *model,
                                      gpointer state);
static gdouble const *get_unsmeared_excitation (PeaqEarModel const *model,
                                                gpointer state);
static void do_spreading (PeaqFFTEarModel const *model, gdouble const *Pp,
                          gdouble *E2);
static void get_property (GObject *obj, guint id, GValue *value,
                          GParamSpec *pspec);
static void set_property (GObject *obj, guint id, const GValue *value,
                          GParamSpec *pspec);

/*
 * peaq_fftearmodel_get_type:
 *
 * Registers the type GstPeaqFFTEarModel if not already done so and
 * returns the respective #GType.
 *
 * Returns: the type for GstPeaqFFTEarModel.
 *
 * TODO: add a class_finalize function to free all the memory allocated in 
 * class_init. Or should all this be done by base_init/base_finalize?
 */
GType
peaq_fftearmodel_get_type ()
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (PeaqFFTEarModelClass),      /* class_size */
      NULL,                               /* base_init */
      NULL,                               /* base_finalize */
      class_init,                         /* class_init */
      NULL,                               /* class_finalize */
      NULL,                               /* class_data */
      sizeof (PeaqFFTEarModel),           /* instance_size */
      0,                                  /* n_preallocs */
      init                                /* instance_init */
    };
    type = g_type_register_static (PEAQ_TYPE_EARMODEL,
                                   "PeaqFFTEarModel", &info, 0);
  }
  return type;
}

/*
 * class_init:
 * @klass: pointer to the uninitialized class structure.
 * @class_data: pointer to data specified when registering the class (unused).
 *
 * Sets up the class data which mainly involves pre-computing helper data.
 */
static void
class_init (gpointer klass, gpointer class_data)
{
  guint k;
  guint N = FFT_FRAMESIZE;
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PeaqEarModelClass *ear_model_class =
    PEAQ_EARMODEL_CLASS (klass);
  PeaqFFTEarModelClass *fft_model_class =
    PEAQ_FFTEARMODEL_CLASS (klass);

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
  g_object_class_install_property (object_class,
                                   PROP_BAND_COUNT,
                                   g_param_spec_uint ("number-of-bands",
                                                      "number of bands",
                                                      "Number of bands (55 or 109)",
                                                      55, 109, 109,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_CONSTRUCT));

  ear_model_class->get_playback_level = get_playback_level;
  ear_model_class->set_playback_level = set_playback_level;
  ear_model_class->state_alloc = state_alloc;
  ear_model_class->state_free = state_free;
  ear_model_class->process_block = process_block;
  ear_model_class->get_excitation = get_excitation;
  ear_model_class->get_unsmeared_excitation = get_unsmeared_excitation;

  ear_model_class->loudness_scale = LOUDNESS_SCALE;
  ear_model_class->frame_size = FFT_FRAMESIZE;
  ear_model_class->step_size = FFT_FRAMESIZE / 2;
  ear_model_class->tau_min = 0.008;
  ear_model_class->tau_100 = 0.030;

  /* pre-compute Hann window; (2) in [BS1387], (1) and (3) in [Kabal03] */
  fft_model_class->hann_window = g_new (gdouble, N);
  for (k = 0; k < N; k++) {
    fft_model_class->hann_window[k] =
      sqrt(8./3.) * 0.5 * (1. - cos (2 * M_PI * k / (N - 1)));
  }
}

/*
 * init:
 * @obj: Pointer to the unitialized #PeaqEarModel structure.
 * @klass: The class structure of the class being instantiated.
 *
 * Initializes one instance of #PeaqFFTEarModel, in particular, the state
 * variables for the time smearing are allocated and initialized to zero.
 */
static void
init (GTypeInstance *obj, gpointer klass)
{
  PeaqFFTEarModel *model = PEAQ_FFTEARMODEL (obj);

  model->gstfft = gst_fft_f64_new (FFT_FRAMESIZE, FALSE);

  /* pre-compute weighting coefficients for outer and middle ear weighting 
   * function; (7) in [BS1387], (6) in [Kabal03], but taking the squared value
   * for applying in the power domain */
  guint N = FFT_FRAMESIZE;
  guint k;
  model->outer_middle_ear_weight = g_new (gdouble, N / 2 + 1);
  gdouble sampling_rate = peaq_earmodel_get_sampling_rate (PEAQ_EARMODEL (obj));
  for (k = 0; k <= N / 2; k++) {
    model->outer_middle_ear_weight[k] = 
      pow (peaq_earmodel_calc_ear_weight ((gdouble) k * sampling_rate / N), 2);
  }
}

/*
 * finalize:
 * @obj: Pointer to the #PeaqFFTEarModel to be finalized.
 *
 * Disposes the given instance of #PeaqFFTEarModel.
 */
static void
finalize (GObject *obj)
{
  PeaqFFTEarModel *model = PEAQ_FFTEARMODEL (obj);
  GObjectClass *parent_class =
    G_OBJECT_CLASS (g_type_class_peek_parent (g_type_class_peek
                                              (PEAQ_TYPE_FFTEARMODEL)));
  gst_fft_f64_free (model->gstfft);
  g_free (model->outer_middle_ear_weight);
  g_free (model->band_lower_end);
  g_free (model->band_upper_end);
  g_free (model->band_lower_weight);
  g_free (model->band_upper_weight);
  g_free (model->spreading_normalization);
  g_free (model->aUC);
  g_free (model->gIL);
  g_free (model->masking_difference);

  parent_class->finalize (obj);
}

/*
 * get_property:
 * @obj: the object structure.
 * @id: the id of the property queried.
 * @value: the value to be filled in.
 * @pspec: the #GParamSpec of the property queried.
 *
 * Fills the given @value with the value currently set for the property 
 * specified by @id.
 */
static gdouble
get_playback_level (PeaqEarModel const *model)
{
  PeaqFFTEarModel *fft_model = PEAQ_FFTEARMODEL (model);
  return 10. * log10 (fft_model->level_factor *
                      (GAMMA / 4 * (FFT_FRAMESIZE - 1) / FFT_FRAMESIZE));
}

static void
set_playback_level (PeaqEarModel *model, gdouble level)
{
  PeaqFFTEarModel *fft_model = PEAQ_FFTEARMODEL (model);
  /* level_factor is the square of fac/N in [BS1387], which equals G_Li/N_F in
   * [Kabal03] except for a factor of sqrt(8/3) which is part of the Hann
   * window in [BS1387] but not in [Kabal03]; see [Kabal03] for the derivation
   * of the denominator and the meaning of GAMMA */
  fft_model->level_factor = pow (10, level / 10) /
    (8. / 3. * (GAMMA / 4 * (FFT_FRAMESIZE - 1)) * (GAMMA / 4 * (FFT_FRAMESIZE - 1)));
}

static
gpointer state_alloc (PeaqEarModel const *model)
{
  PeaqFFTEarModelState *state = g_new0 (PeaqFFTEarModelState, 1);
  state->filtered_excitation = g_new0 (gdouble, model->band_count);
  state->unsmeared_excitation = g_new0 (gdouble, model->band_count);
  state->excitation = g_new0 (gdouble, model->band_count);
  return state;
}

static
void state_free (PeaqEarModel const *model, gpointer state)
{
  g_free (((PeaqFFTEarModelState *) state)->filtered_excitation);
  g_free (((PeaqFFTEarModelState *) state)->unsmeared_excitation);
  g_free (((PeaqFFTEarModelState *) state)->excitation);
  g_free (state);
}

/*
 * process_block:
 * @model: the #PeaqFFTEarModel instance structure.
 * @state: the state data of type PeaqFFTEarModelState.
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
void
process_block (PeaqEarModel const *model, gpointer state,
               gfloat const *sample_data)
{
  guint k, i;
  PeaqFFTEarModelState *fft_state = (PeaqFFTEarModelState *) state;
  PeaqFFTEarModel const *fft_model = PEAQ_FFTEARMODEL (model);
  PeaqFFTEarModelClass const *fft_model_class =
    PEAQ_FFTEARMODEL_GET_CLASS (fft_model);
  gdouble *windowed_data = g_newa (gdouble, FFT_FRAMESIZE);
  GstFFTF64Complex *fftoutput =
    g_newa (GstFFTF64Complex, FFT_FRAMESIZE / 2 + 1);
  gdouble *band_power =
    g_newa (gdouble, peaq_earmodel_get_band_count (model));
  gdouble *noisy_band_power =
    g_newa (gdouble, peaq_earmodel_get_band_count (model));

  /* apply a Hann window to the input data frame; (3) in [BS1387], part of (4)
   * in [Kabal03] */
  for (k = 0; k < FFT_FRAMESIZE; k++)
    windowed_data[k] = fft_model_class->hann_window[k] * sample_data[k];

  /* apply FFT to windowed data; (4) in [BS1387] and part of (4) in [Kabal03],
   * but without division by FFT_FRAMESIZE, which is subsumed in the
   * level_factor applied next */
  gst_fft_f64_fft (fft_model->gstfft, windowed_data, fftoutput);

  for (k = 0; k < FFT_FRAMESIZE / 2 + 1; k++) {
    /* compute power spectrum and apply scaling depending on playback level; in
     * [BS1387], the scaling is applied on the magnitudes, so the factor is
     * squared when comparing to [BS1387] (and also includes the squared
     * division by FFT_FRAMESIZE) */
    fft_state->power_spectrum[k] =
      (fftoutput[k].r * fftoutput[k].r + fftoutput[k].i * fftoutput[k].i) *
      fft_model->level_factor;

    /* apply outer and middle ear weighting; (9) in [BS1387] (but in the power
     * domain), (8) in [Kabal03] */
    fft_state->weighted_power_spectrum[k] =
      fft_state->power_spectrum[k] *
      fft_model->outer_middle_ear_weight[k];
  }

  /* group the outer ear weighted FFT outputs into critical bands according to
   * section 2.1.5 of [BS1387] / section 2.6 of [Kabal03] */
  peaq_fftearmodel_group_into_bands (fft_model,
                                     fft_state->weighted_power_spectrum,
                                     band_power);

  /* add the internal noise to obtain the pitch patters; (14) in [BS1387], (17)
   * in [Kabal03] */
  for (i = 0; i < model->band_count; i++)
    noisy_band_power[i] =
      band_power[i] + peaq_earmodel_get_internal_noise (model, i);

  /* do (frequency) spreading according to section 2.1.7 in [BS1387] / section
   * 2.8 in [Kabal03] */
  do_spreading (fft_model, noisy_band_power, fft_state->unsmeared_excitation);

  /* do time domain spreading according to section 2.1.8 of [BS1387] / section
   * 2.9 of [Kabal03]
   * NOTE: according to [BS1387], the filtered_excitation after processing the
   * first frame should be all zero; we follow the interpretation of [Kabal03]
   * and only initialize to zero before the first frame. */
  for (i = 0; i < model->band_count; i++) {
    gdouble a = peaq_earmodel_get_ear_time_constant (model, i);
    fft_state->filtered_excitation[i] =
      a * fft_state->filtered_excitation[i] +
      (1. - a) * fft_state->unsmeared_excitation[i];
    fft_state->excitation[i] =
      fft_state->filtered_excitation[i] > fft_state->unsmeared_excitation[i] ?
      fft_state->filtered_excitation[i] : fft_state->unsmeared_excitation[i];
  }

  /* check whether energy threshold has been reached, see section 5.2.4.3 in
   * [BS1387] */
  gdouble energy = 0.;
  for (k = FFT_FRAMESIZE / 2; k < FFT_FRAMESIZE; k++)
    energy += sample_data[k] * sample_data[k];
  if (energy >= 8000. / (32768. * 32768.))
    fft_state->energy_threshold_reached = TRUE;
  else
    fft_state->energy_threshold_reached = FALSE;
}

static gdouble const *
get_excitation (PeaqEarModel const *model, gpointer state)
{
  return ((PeaqFFTEarModelState *) state)->excitation;
}

static gdouble const *
get_unsmeared_excitation (PeaqEarModel const *model, gpointer state)
{
  return ((PeaqFFTEarModelState *) state)->unsmeared_excitation;
}

/**
 * peaq_fftearmodel_get_power_spectrum:
 * @state: The #PeaqFFTEarModel's state from which to obtain the current power
 * spectrum.
 *
 * Returns the power spectrum as computed during the last call to
 * peaq_earmodel_process_block() with the given @state.
 *
 * Returns: The power spectrum, up to half the sampling rate
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msup><mfenced open="|" close="|"><mrow><mi>F</mi><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced><mn>2</mn></msup>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msubsup><mi>G</mi><mi>L</mi><mn>2</mn></msubsup><mo>&InvisibleTimes;</mo><msup><mfenced open="|" close="|"><mrow><mi>X</mi><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced><mn>2</mn></msup>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 */
gdouble const *
peaq_fftearmodel_get_power_spectrum (gpointer state)
{
  return ((PeaqFFTEarModelState *) state)->power_spectrum;
}

/**
 * peaq_fftearmodel_get_weighted_power_spectrum:
 * @state: The #PeaqFFTEarModel's state from which to obtain the current
 * weighted power spectrum.
 *
 * Returns the power spectrum weighted with the outer ear weighting function as
 * computed during the last call to peaq_earmodel_process_block() with the
 * given @state.
 *
 * Returns: The weighted power spectrum, up to half the sampling rate
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msup><mfenced open="(" close=")"><mrow><msub> <mi>F</mi> <mi>e</mi> </msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced><mn>2</mn></msup>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msup><mfenced open="|" close="|"><mrow><msub><mi>X</mi><mi>w</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfenced><mn>2</mn></msup>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 */
gdouble const *
peaq_fftearmodel_get_weighted_power_spectrum (gpointer state)
{
  return ((PeaqFFTEarModelState *) state)->weighted_power_spectrum;
}

/**
 * peaq_fftearmodel_is_energy_threshold_reached:
 * @state: The #PeaqFFTEarModel's state to query whether the energy threshold
 * has been reached.
 *
 * Returns whether the last frame processed with to
 * peaq_earmodel_process_block() with the given @state reached the energy
 * threshold described in section 5.2.4.3 of in <xref linkend="BS1387" />.
 *
 * Returns: TRUE if the energy threshold was reached, FALSE otherwise.
 */
gboolean
peaq_fftearmodel_is_energy_threshold_reached (gpointer state)
{
  return ((PeaqFFTEarModelState *) state)->energy_threshold_reached;
}

/**
 * peaq_fftearmodel_group_into_bands:
 * @model: the #PeaqFFTEarModel instance structure.
 * @spectrum: pointer to an array of the spectral coefficients with
 * frame_size / 2 + 1 elements, where frame_size is as returned by
 * peaq_earmodel_get_frame_size().
 * @band_power: pointer to an array in which the power of the individual bands
 * is stored; must have as many entries as there are bands in the underlying
 * model.
 *
 * The given spectral data is distributed into frequency bands according to the
 * underlying #PeaqEarModel. The grouping into bands follows the
 * algorithm proposed in <xref linkend="Kabal03" />.
 */
void
peaq_fftearmodel_group_into_bands (PeaqFFTEarModel const *model,
                                   gdouble const *spectrum,
                                   gdouble *band_power)
{
  guint i;
  for (i = 0; i < PEAQ_EARMODEL (model)->band_count; i++) {
    guint k;
    band_power[i] =
      model->band_lower_weight[i] * spectrum[model->band_lower_end[i]] +
      model->band_upper_weight[i] * spectrum[model->band_upper_end[i]];
    for (k = model->band_lower_end[i] + 1;
         k < model->band_upper_end[i]; k++)
      band_power[i] += spectrum[k];
    if (band_power[i] < 1e-12)
      band_power[i] = 1e-12;
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
static void
do_spreading (PeaqFFTEarModel const *model, gdouble const *Pp, gdouble *E2)
{
  guint i;
  guint band_count = peaq_earmodel_get_band_count (PEAQ_EARMODEL (model));
  gdouble *aUCEe = g_newa (gdouble, model->parent.band_count);
  gdouble *Ene = g_newa (gdouble, model->parent.band_count);
  const gdouble aLe = model->lower_spreading_exponantiated;

  g_assert (band_count > 0);

  for (i = 0; i < band_count; i++) {
    /* from (23) in [Kabal03] */
    gdouble aUCE = model->aUC[i] * pow (Pp[i], 0.2 * model->deltaZ);
    /* part of (24) in [Kabal03] */
    gdouble gIU = (1. - pow (aUCE, band_count - i)) / (1. - aUCE);
    /* Note: (24) in [Kabal03] is wrong; indeed it gives A(l,E) instead of
     * A(l,E)^-1 */
    gdouble En = Pp[i] / (model->gIL[i] + gIU - 1.);
    aUCEe[i] = pow (aUCE, 0.4);
    Ene[i] = pow (En, 0.4);
  }
  /* first fill E2 with E_sL according to (28) in [Kabal03] */
  E2[band_count - 1] = Ene[band_count - 1];
  for (i = band_count - 1; i > 0; i--)
    E2[i - 1] = aLe * E2[i] + Ene[i - 1];
  /* now add E_sU to E2 according to (27) in [Kabal03] (with rearranged
   * ordering) */
  for (i = 0; i < band_count - 1; i++) {
    guint j;
    gdouble r = Ene[i];
    for (j = i + 1; j < band_count; j++) {
      r *= aUCEe[i];
      E2[j] += r;
    }
  }
  /* compute end result by normalizing according to (25) in [Kabal03] */
  for (i = 0; i < band_count; i++) {
    E2[i] = pow (E2[i], 1. / 0.4) / model->spreading_normalization[i];
  }
}

static void
get_property (GObject *obj, guint id, GValue *value, GParamSpec *pspec)
{
  PeaqEarModel *model = PEAQ_EARMODEL (obj);
  switch (id) {
    case PROP_BAND_COUNT:
      g_value_set_uint (value, peaq_earmodel_get_band_count (model));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
      break;
  }
}

static void
set_property (GObject *obj, guint id, const GValue *value, GParamSpec *pspec)
{
  switch (id) {
    case PROP_BAND_COUNT:
      {
        guint band;
        PeaqFFTEarModel *model = PEAQ_FFTEARMODEL (obj);

        model->deltaZ = 27. / (g_value_get_uint(value) - 1);
        gdouble zL = 7. * asinh (80. / 650.);
        gdouble zU = 7. * asinh (18000. / 650.);
        guint band_count = ceil ((zU - zL) / model->deltaZ);
        g_assert (band_count == g_value_get_uint(value));
        GArray *fc_array = g_array_sized_new (FALSE, FALSE, sizeof (gdouble),
                                              band_count);
        model->band_lower_end =
          g_renew (guint, model->band_lower_end, band_count);
        model->band_upper_end =
          g_renew (guint, model->band_upper_end, band_count);
        model->band_lower_weight =
          g_renew (gdouble, model->band_lower_weight, band_count);
        model->band_upper_weight =
          g_renew (gdouble, model->band_upper_weight, band_count);
        model->spreading_normalization =
          g_renew (gdouble, model->spreading_normalization, band_count);
        model->aUC = g_renew (gdouble, model->aUC, band_count);
        model->gIL = g_renew (gdouble, model->gIL, band_count);
        model->masking_difference =
          g_renew (gdouble, model->masking_difference, band_count);

        model->lower_spreading = pow (10., -2.7 * model->deltaZ); /* 1 / a_L */
        model->lower_spreading_exponantiated =
          pow (model->lower_spreading, 0.4);

        gdouble sampling_rate =
          peaq_earmodel_get_sampling_rate (PEAQ_EARMODEL (obj));

        for (band = 0; band < band_count; band++) {
          gdouble zl = zL + band * model->deltaZ;
          gdouble zu = MIN(zU, zL + (band + 1) * model->deltaZ);
          gdouble zc = (zu + zl) / 2.;
          gdouble curr_fc = 650. * sinh (zc / 7.);
          g_array_append_val (fc_array, curr_fc);

          /* pre-compute helper data for peaq_fftearmodel_group_into_bands()
           * The precomputed data is as proposed in [Kabal03], but the
           * algorithm to compute is somewhat simplified */
          gdouble fl = 650. * sinh (zl / 7.);
          gdouble fu = 650. * sinh (zu / 7.);
          model->band_lower_end[band]
            = (guint) round (fl / sampling_rate * FFT_FRAMESIZE);
          model->band_upper_end[band]
            = (guint) round (fu / sampling_rate * FFT_FRAMESIZE);
          gdouble upper_freq =
            (2 * model->band_lower_end[band] + 1) / 2. * sampling_rate /
            FFT_FRAMESIZE;
          if (upper_freq > fu)
            upper_freq = fu;
          gdouble U = upper_freq - fl;
          model->band_lower_weight[band] = U * FFT_FRAMESIZE / sampling_rate;
          if (model->band_lower_end[band] == model->band_upper_end[band]) {
            model->band_upper_weight[band] = 0;
          } else {
            gdouble lower_freq = (2 * model->band_upper_end[band] - 1) / 2.
              * sampling_rate / FFT_FRAMESIZE;
            U = fu - lower_freq;
            model->band_upper_weight[band] = U * FFT_FRAMESIZE / sampling_rate;
          }

          /* pre-compute internal noise, time constants for time smearing,
           * thresholds and helper data for spreading */
          const gdouble aL = model->lower_spreading;
          model->aUC[band] = pow (10., (-2.4 - 23. / curr_fc) * model->deltaZ);
          model->gIL[band] = (1. - pow (aL, band + 1)) / (1. - aL);
          model->spreading_normalization[band] = 1.;

          /* masking weighting function; (25) in [BS1387], (112) in [Kabal03] */
          model->masking_difference[band] =
            pow (10., (band * model->deltaZ <= 12. ?
                      3. : 0.25 * band * model->deltaZ) / 10.);
        }

        g_object_set (obj, "band-centers", fc_array, NULL);
        g_array_unref (fc_array);

        gdouble *spread = g_newa (gdouble, band_count);
        do_spreading (model, model->spreading_normalization, spread);
        for (band = 0; band < band_count; band++)
          model->spreading_normalization[band] = spread[band];
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
      break;
  }
}

/**
 * peaq_fftearmodel_get_masking_difference:
 * @model: The #PeaqFFTEarModel instance structure.
 *
 * Returns the masking weighting function <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msup><mn>10</mn><mfrac><mrow><mi>m</mi><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow><mn>10</mn></mfrac></msup>
 * </math></inlineequation>
 * with
 * <informalequation><math display="block" xmlns="http://www.w3.org/1998/Math/MathML">
 *   <mi>m</mi>
 *   <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *   <mo>=</mo>
 *   <mfenced open="{" close="">
 *     <mtable>
 *       <mtr>
 *         <mtd> <mn>3.0</mn> </mtd>
 *         <mtd>
 *           <mtext>for </mtext><mspace width="1ex" />
 *           <mi>k</mi> <mo>&sdot;</mo> <mi>res</mi> <mo>&le;</mo> <mn>12</mn>
 *         </mtd>
 *       </mtr>
 *       <mtr>
 *         <mtd>
 *           <mn>0.25</mn><mo>&sdot;</mo><mi>k</mi><mo>&sdot;</mo><mi>res</mi>
 *         </mtd>
 *         <mtd>
 *           <mtext>for </mtext><mspace width="1ex" />
 *           <mi>k</mi> <mo>&sdot;</mo> <mi>res</mi> <mo>&gt;</mo> <mn>12</mn>
 *         </mtd>
 *       </mtr>
 *     </mtable>
 *   </mfenced>
 * </math></informalequation>
 * for all bands <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>k</mi>
 * </math></inlineequation>.
 *
 * Returns: The masking weighting function <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msup><mn>10</mn><mfrac><mrow><mi>m</mi><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow><mn>10</mn></mfrac></msup>
 * </math></inlineequation>.
 * The pointer points to internal data of the PeaqFFTEarModel and must not be
 * freed.
 */
gdouble const *
peaq_fftearmodel_get_masking_difference (PeaqFFTEarModel const *model)
{
  return model->masking_difference;
}
