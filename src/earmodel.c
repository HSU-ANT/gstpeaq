/* GstPEAQ
 * Copyright (C) 2006, 2007, 2011, 2012 Martin Holters <martin.holters@hsuhh.de>
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

/**
 * SECTION:earmodel
 * @short_description: Transforms a time domain signal into pitch domain 
 * excitation patterns.
 *
 * The computation is thoroughly described in section 2 of 
 * <xref linkend="Kabal03" />.
 *
 * The main processing is performed in peaq_earmodel_process(), where frames of
 * length %FRAMESIZE samples are processed, which should be overlapped by 50% 
 * from one invocation to the next. The first step is to apply a Hann window 
 * and transform the frame to the frequency domain. Then, a filter modelling
 * the effects of the outer and middle ear is applied by weighting the 
 * spectral coefficients. These are grouped into frequency bands of one fourth 
 * the width of the critical bands of auditoriy perception to reach the pitch 
 * domain. To model the internal noise of the ear, a pitch-dependent term is
 * added to the power in each band. Finally, spreading in the pitch domain and 
 * smearing in the time-domain are performed. The necessary state information 
 * for the time smearing is stored in the #PeaqEarModel.
 *
 * Additionally, the overall loudness of the frame is computed, although this 
 * is not strictly part of the ear model.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "earmodel.h"
#include "earmodel_bands.h"
#include "gstpeaq.h"

#include <math.h>
#include <gst/fft/gstfftf64.h>

#define DELTAZ 0.25
#define GAMMA 0.84971762641205
#define LOUDNESS_SCALE 1.07664

enum
{
  PROP_0,
  PROP_PLAYBACK_LEVEL
};

/**
 * PeaqEarModel:
 *
 * The opaque PeaqEarModel structure.
 */
struct _PeaqEarModel
{
  GObjectClass parent;
  GstFFTF64 *gstfft;
  gdouble level_factor;
  gdouble *filtered_excitation;
};

/**
 * PeaqEarModelClass:
 *
 * The opaque PeaqEarModelClass structure.
 */
struct _PeaqEarModelClass
{
  GObjectClass parent;
  gdouble *hann_window;
  gdouble *outer_middle_ear_weight;
  guint *band_lower_end;
  guint *band_upper_end;
  gdouble *band_lower_weight;
  gdouble *band_upper_weight;
  gdouble *internal_noise_level;
  gdouble lower_spreading;
  gdouble lower_spreading_exponantiated;
  gdouble *spreading_normalization;
  gdouble *aUC;
  gdouble *gIL;
  gdouble *ear_time_constants;
  gdouble *threshold;
  gdouble *excitation_threshold;
  gdouble *loudness_factor;
};

static void peaq_earmodel_class_init (gpointer klass, gpointer class_data);
static void peaq_earmodel_init (GTypeInstance * obj, gpointer klass);
static void peaq_earmodel_finalize (GObject * obj);
static void peaq_earmodel_get_property (GObject * obj, guint id,
					GValue * value, GParamSpec * pspec);
static void peaq_earmodel_set_property (GObject * obj, guint id,
					const GValue * value,
					GParamSpec * pspec);
static void do_spreading (PeaqEarModelClass * ear_class, gdouble * Pp,
			  gdouble * E2);

/*
 * peaq_leveladapter_get_type:
 *
 * Registers the type GstPeaqEarModel if not already done so and returns the
 * respective #GType.
 *
 * Returns: the type for GstPeaqEarModel.
 *
 * TODO: add a class_finalize function to free all the memory allocated in 
 * class_init. Or should all this be done by base_init/base_finalize?
 */
GType
peaq_earmodel_get_type ()
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (PeaqEarModelClass), /* class_size */
      NULL,			  /* base_init */
      NULL,			  /* base_finalize */
      peaq_earmodel_class_init,   /* class_init */
      NULL,			  /* class_finalize */
      NULL,			  /* class_data */
      sizeof (PeaqEarModel),      /* instance_size */
      0,			  /* n_preallocs */
      peaq_earmodel_init	  /* instance_init */
    };
    type = g_type_register_static (G_TYPE_OBJECT,
				   "GstPeaqEarModel", &info, 0);
  }
  return type;
}

/*
 * peaq_earmodel_class_init:
 * @klass: pointer to the uninitialized class structure.
 * @class_data: pointer to data specified when registering the class (unused).
 *
 * Sets up the class data which mainly involves pre-computing helper data.
 */
static void
peaq_earmodel_class_init (gpointer klass, gpointer class_data)
{
  guint k;
  guint N = FRAMESIZE;
  gdouble *spread;
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  PeaqEarModelClass *ear_class = PEAQ_EARMODEL_CLASS (klass);

  /* override finalize method */
  object_class->finalize = peaq_earmodel_finalize;

  /* set property setter/getter functions and install property for playback 
   * level */
  object_class->set_property = peaq_earmodel_set_property;
  object_class->get_property = peaq_earmodel_get_property;
  g_object_class_install_property (object_class,
				   PROP_PLAYBACK_LEVEL,
				   g_param_spec_double ("playback_level",
							"playback level",
							"Playback level in dB",
							0, 130, 92,
							G_PARAM_READWRITE |
							G_PARAM_CONSTRUCT));

  /* pre-compute Hann window */
  ear_class->hann_window = g_new (gdouble, N);
  for (k = 0; k < N; k++) {
    ear_class->hann_window[k] = 0.5 * (1. - cos (2 * M_PI * k / (N - 1)));
  }

  /* pre-compute weighting coefficients for outer and middle ear weighting 
   * function */
  ear_class->outer_middle_ear_weight = g_new (gdouble, N / 2 + 1);
  for (k = 0; k <= N / 2; k++) {
    gdouble f_kHz = (gdouble) k * SAMPLINGRATE / N / 1000;
    gdouble W_dB =
      (-0.6 * 3.64 * pow (f_kHz, -0.8)) +
      (6.5 * exp (-0.6 * pow (f_kHz - 3.3, 2))) - (1e-3 * pow (f_kHz, 3.6));
    ear_class->outer_middle_ear_weight[k] = pow (10, W_dB / 10);
  }

  /* pre-compute helper data for peaq_earmodel_group_into_bands */
  ear_class->band_lower_end = g_new (guint, CRITICAL_BAND_COUNT);
  ear_class->band_upper_end = g_new (guint, CRITICAL_BAND_COUNT);
  ear_class->band_lower_weight = g_new (gdouble, CRITICAL_BAND_COUNT);
  ear_class->band_upper_weight = g_new (gdouble, CRITICAL_BAND_COUNT);
  for (k = 0; k < CRITICAL_BAND_COUNT; k++) {
    guint l;
    ear_class->band_lower_end[k] = 0;
    for (l = 0; l <= FRAMESIZE / 2; l++) {
      gdouble U;
      gdouble upper_freq = (2 * l + 1) / 2. * SAMPLINGRATE / FRAMESIZE;
      gdouble lower_freq = (2 * l - 1) / 2. * SAMPLINGRATE / FRAMESIZE;
      if (upper_freq > fu[k])
	upper_freq = fu[k];
      if (lower_freq < fl[k])
	lower_freq = fl[k];
      U = upper_freq - lower_freq;
      if (U <= 0) {
	if (l == ear_class->band_lower_end[k])
	  ear_class->band_lower_end[k] = l + 1;
      } else {
	U /= (gdouble) SAMPLINGRATE / FRAMESIZE;
	if (l == ear_class->band_lower_end[k]) {
	  ear_class->band_lower_weight[k] = U;
	  ear_class->band_upper_weight[k] = 0;
	} else {
	  ear_class->band_upper_weight[k] = U;
	}
	ear_class->band_upper_end[k] = l;
      }
    }
  }


  /* pre-compute internal noise, time constants for time smearing, thresholds 
   * and helper data for spreading */
  ear_class->internal_noise_level = g_new (gdouble, CRITICAL_BAND_COUNT);
  ear_class->ear_time_constants = g_new (gdouble, CRITICAL_BAND_COUNT);
  ear_class->spreading_normalization = g_new (gdouble, CRITICAL_BAND_COUNT);
  ear_class->aUC = g_new (gdouble, CRITICAL_BAND_COUNT);
  ear_class->gIL = g_new (gdouble, CRITICAL_BAND_COUNT);
  ear_class->threshold = g_new (gdouble, CRITICAL_BAND_COUNT);
  ear_class->excitation_threshold = g_new (gdouble, CRITICAL_BAND_COUNT);
  ear_class->loudness_factor = g_new (gdouble, CRITICAL_BAND_COUNT);
  ear_class->lower_spreading = pow (10, -2.7 * DELTAZ);
  ear_class->lower_spreading_exponantiated =
    pow (ear_class->lower_spreading, 0.4);
  for (k = 0; k < CRITICAL_BAND_COUNT; k++) {
    const gdouble aL = ear_class->lower_spreading;
    gdouble curr_fc;
    gdouble tau;
    curr_fc = fc[k];
    ear_class->internal_noise_level[k] =
      pow (10, 0.4 * 0.364 * pow (curr_fc / 1000, -0.8));
    tau = 0.008 + 100 / curr_fc * (0.03 - 0.008);
    ear_class->ear_time_constants[k] =
      exp (-(gdouble) N / (2 * SAMPLINGRATE) / tau);
    ear_class->spreading_normalization[k] = 1.;
    ear_class->aUC[k] = pow (10, (-2.4 - 23 / curr_fc) * DELTAZ);
    ear_class->gIL[k] = (1 - pow (aL, k + 1)) / (1 - aL);
    ear_class->excitation_threshold[k] =
      pow (10, 0.364 * pow (curr_fc / 1000, -0.8));
    ear_class->threshold[k] =
      pow (10, 0.1 * (-2 - 2.05 * atan (curr_fc / 4000) -
		      0.75 * atan (curr_fc / 1600 * curr_fc / 1600)));
    ear_class->loudness_factor[k]
      = LOUDNESS_SCALE * pow (ear_class->excitation_threshold[k]
                              / (1e4 * ear_class->threshold[k]),
                              0.23);
  }
  spread = g_newa (gdouble, CRITICAL_BAND_COUNT);
  do_spreading (ear_class, ear_class->spreading_normalization, spread);
  for (k = 0; k < CRITICAL_BAND_COUNT; k++)
    ear_class->spreading_normalization[k] = spread[k];
}

/*
 * peaq_earmodel_init:
 * @obj: Pointer to the unitialized #PeaqEarModel structure.
 * @klass: The class structure of the class being instantiated.
 *
 * Initializes one instance of #PeaqEarModel, in particular, the state 
 * variables for the time smearing are allocated and initialized to zero.
 */
static void
peaq_earmodel_init (GTypeInstance * obj, gpointer klass)
{
  PeaqEarModel *ear = PEAQ_EARMODEL (obj);

  /* setup data for FFT */
  ear->gstfft = gst_fft_f64_new (FRAMESIZE, FALSE);

  ear->filtered_excitation = g_new0 (gdouble, CRITICAL_BAND_COUNT);
}

/*
 * peaq_earmodel_finalize:
 * @obj: Pointer to the #PeaqEarModel to be finalized.
 *
 * Disposes the given instance of #PeaqEarModel, in particular, the state 
 * variables for the time smearing are deallocated.
 */
static void 
peaq_earmodel_finalize (GObject * obj)
{
  PeaqEarModel *ear = PEAQ_EARMODEL (obj);
  GObjectClass *parent_class = 
    G_OBJECT_CLASS (g_type_class_peek_parent (g_type_class_peek
					      (PEAQ_TYPE_EARMODEL)));
  g_free (ear->filtered_excitation);
  gst_fft_f64_free (ear->gstfft);
  parent_class->finalize(obj);
}

/*
 * peaq_earmodel_get_property:
 * @obj: the object structure.
 * @id: the id of the property queried.
 * @value: the value to be filled in.
 * @pspec: the #GParamSpec of the property queried.
 *
 * Fills the given @value with the value currently set for the property 
 * specified by @id.
 */
static void
peaq_earmodel_get_property (GObject * obj, guint id, GValue * value,
			    GParamSpec * pspec)
{
  PeaqEarModel *ear = PEAQ_EARMODEL (obj);
  switch (id) {
    case PROP_PLAYBACK_LEVEL:
      g_value_set_double (value, 10 * log10 (ear->level_factor *
					     (GAMMA / 4 * (FRAMESIZE - 1) /
					      FRAMESIZE)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
      break;
  }
}

/*
 * peaq_earmodel_get_property:
 * @obj: the object structure.
 * @id: the id of the property to be set.
 * @value: the value to set the property to.
 * @pspec: the #GParamSpec of the property to be set.
 *
 * Sets the property specified by @id to the given @value.
 */
static void
peaq_earmodel_set_property (GObject * obj, guint id, const GValue * value,
			    GParamSpec * pspec)
{
  PeaqEarModel *ear = PEAQ_EARMODEL (obj);
  switch (id) {
    case PROP_PLAYBACK_LEVEL:
      ear->level_factor = pow (10, g_value_get_double (value) / 10) /
	((GAMMA / 4 * (FRAMESIZE - 1) / FRAMESIZE) *
	 (GAMMA / 4 * (FRAMESIZE - 1) / FRAMESIZE));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, id, pspec);
      break;
  }
}

/**
 * peaq_earmodel_process:
 * @ear: the #PeaqEarModel instance structure.
 * @sample_data: pointer to a frame of #FRAMESIZE samples to be processed.
 * @output: pointer to a #EarModelOutput structure which is filled with the 
 * computed output data.
 *
 * Performs the computation described in section 2 of 
 * <xref linkend="Kabal03" /> for one single frame. The input is assumed to be 
 * sampled at 48 kHz. To follow the specification, the frames of successive 
 * invocations of peaq_earmodel_process() have to overlap by 50%.
 *
 * The first step is to apply a Hann window to the input frame and transform 
 * it to the frequency domain using FFT. The squared magnitude 
 * <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML">
 *   <msup>
 *     <mfenced open="|" close="|"><mrow>
 *       <mi>X</mi>
 *       <mfenced open="[" close="]"><mi>k</mi></mfenced>
 *     </mrow></mfenced>
 *     <mn>2</mn>
 *   </msup>
 * </math></inlineequation>
 * of the frequency coefficients up to half the frame length are stored in 
 * <structfield>power_spectrum</structfield> of @output.
 */
void
peaq_earmodel_process (PeaqEarModel * ear, gfloat * sample_data,
		       EarModelOutput * output)
{
  guint k, i;
  PeaqEarModelClass *ear_class = PEAQ_EARMODEL_GET_CLASS (ear);
  gdouble *windowed_data = g_newa (gdouble, FRAMESIZE);
  GstFFTF64Complex *fftoutput = g_newa (GstFFTF64Complex, FRAMESIZE / 2 + 1);
  gdouble *noisy_band_power = g_newa (gdouble, CRITICAL_BAND_COUNT);

  g_assert (output);

  /* apply a Hann window to the input data frame */
  for (k = 0; k < FRAMESIZE; k++)
    windowed_data[k] = ear_class->hann_window[k] * sample_data[k];

  gst_fft_f64_fft (ear->gstfft, windowed_data, fftoutput);
  for (k = 0; k < FRAMESIZE / 2 + 1; k++) {
    output->power_spectrum[k] =
      (fftoutput[k].r * fftoutput[k].r + fftoutput[k].i * fftoutput[k].i) *
      ear->level_factor / (FRAMESIZE * FRAMESIZE);
    output->weighted_power_spectrum[k] =
      output->power_spectrum[k] * ear_class->outer_middle_ear_weight[k];
  }

  peaq_earmodel_group_into_bands (ear_class, output->weighted_power_spectrum,
				  output->band_power);
  for (i = 0; i < CRITICAL_BAND_COUNT; i++)
    noisy_band_power[i] =
      output->band_power[i] + ear_class->internal_noise_level[i];

  do_spreading (ear_class, noisy_band_power, output->unsmeared_excitation);

  for (i = 0; i < CRITICAL_BAND_COUNT; i++) {
    ear->filtered_excitation[i] = ear_class->ear_time_constants[i] *
      ear->filtered_excitation[i] + (1 - ear_class->ear_time_constants[i]) *
      output->unsmeared_excitation[i];
    output->excitation[i] =
      ear->filtered_excitation[i] > output->unsmeared_excitation[i] ?
      ear->filtered_excitation[i] : output->unsmeared_excitation[i];
  }

  output->overall_loudness = 0;
  for (i = 0; i < CRITICAL_BAND_COUNT; i++) {
    gdouble loudness = ear_class->loudness_factor[i]
      * (pow (1. - ear_class->threshold[i] +
              ear_class->threshold[i] * output->excitation[i] /
              ear_class->excitation_threshold[i], 0.23) - 1.);
    output->overall_loudness += MAX (loudness, 0.);
  }
  output->overall_loudness *= 24. / CRITICAL_BAND_COUNT;
}

void
peaq_earmodel_group_into_bands (PeaqEarModelClass * ear_class,
				gdouble * spectrum, gdouble * band_power)
{
  guint i;
  guint k;
  for (i = 0; i < CRITICAL_BAND_COUNT; i++) {
    band_power[i] =
      (ear_class->band_lower_weight[i] *
       spectrum[ear_class->band_lower_end[i]]) +
      (ear_class->band_upper_weight[i] *
       spectrum[ear_class->band_upper_end[i]]);
    for (k = ear_class->band_lower_end[i] + 1;
	 k < ear_class->band_upper_end[i]; k++)
      band_power[i] += spectrum[k];
    if (band_power[i] < 1e-12)
      band_power[i] = 1e-12;
  }
}

static void
do_spreading (PeaqEarModelClass * ear_class, gdouble * Pp, gdouble * E2)
{
  guint i;
  gdouble *aUCEe = g_newa (gdouble, CRITICAL_BAND_COUNT);
  gdouble *Ene = g_newa (gdouble, CRITICAL_BAND_COUNT);
  const gdouble aLe = ear_class->lower_spreading_exponantiated;

  for (i = 0; i < CRITICAL_BAND_COUNT; i++) {
    gdouble aUCE = ear_class->aUC[i] * pow (Pp[i], 0.2 * DELTAZ);
    gdouble gIU = (1 - pow (aUCE, CRITICAL_BAND_COUNT - i)) / (1 - aUCE);
    gdouble En = Pp[i] / (ear_class->gIL[i] + gIU - 1);
    aUCEe[i] = pow (aUCE, 0.4);
    Ene[i] = pow (En, 0.4);
  }
  E2[CRITICAL_BAND_COUNT - 1] = Ene[CRITICAL_BAND_COUNT - 1];
  for (i = CRITICAL_BAND_COUNT - 1; i > 0; i--)
    E2[i - 1] = aLe * E2[i] + Ene[i - 1];
  for (i = 0; i < CRITICAL_BAND_COUNT - 1; i++) {
    guint j;
    gdouble r = Ene[i];
    for (j = i + 1; j < CRITICAL_BAND_COUNT; j++) {
      r *= aUCEe[i];
      E2[j] += r;
    }
  }
  for (i = 0; i < CRITICAL_BAND_COUNT; i++) {
    E2[i] = pow (E2[i], 1 / 0.4) / ear_class->spreading_normalization[i];
  }
}

gdouble
peaq_earmodel_get_band_center_frequency (guint band)
{
  return fc[band];
}

gdouble peaq_earmodel_get_internal_noise (PeaqEarModelClass const * ear_class,
					  guint band)
{
  return ear_class->internal_noise_level[band];
}

