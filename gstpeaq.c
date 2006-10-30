/* GstPEAQ
 * Copyright (C) 2006 Martin Holters <martin.holters@hsuhh.de>
 *
 * gstpeaq.c: Compute objective audio quality measures
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

#include <glib/gprintf.h>
#include <math.h>
#include <string.h>

#include "gstpeaq.h"
#include "earmodel.h"
#include "leveladapter.h"

#define BLOCKSIZE 2048
#define STEPSIZE (BLOCKSIZE/2)
#define BLOCKSIZE_BYTES (BLOCKSIZE * sizeof(gfloat))
#define STEPSIZE_BYTES (STEPSIZE * sizeof(gfloat))
#define EHS_ENERGY_THRESHOLD 7.442401884276241e-6
#define MAXLAG 256

enum
{
  PROP_0,
  PROP_ODG
};

struct _GstPeaqAggregatedData
{
  guint frame_count;
  guint delayed_frame_count;
  guint distorted_frame_count;
  guint ehs_frame_count;
  guint noise_loud_frame_count;
  guint bandwidth_frame_count;
  guint disturbed_frame_count;
  gboolean loudness_reached;
  gdouble ref_bandwidth_sum;
  gdouble test_bandwidth_sum;
  gdouble nmr_sum;
  gdouble past_mod_diff1[3];
  gdouble win_mod_diff1;
  gdouble avg_mod_diff1;
  gdouble avg_mod_diff2;
  gdouble temp_weight;
  gdouble detection_steps;
  gdouble ehs;
  gdouble noise_loudness;
  gdouble filtered_detection_probability;
  gdouble max_filtered_detection_probability;
};

static const GstElementDetails peaq_details =
GST_ELEMENT_DETAILS ("Perceptual evaluation of audio quality",
		     "Sink/Audio",
		     "Compute objective audio quality measures",
		     "Martin Holters <martin.holters@hsuhh.de>");

GST_BOILERPLATE (GstPeaq, gst_peaq, GstElement, GST_TYPE_ELEMENT);

#define STATIC_CAPS \
  GST_STATIC_CAPS ( \
		    "audio/x-raw-float, " \
		    "rate = (int) 48000, " \
		    "channels = (int) 1, " \
		    "endianness = (int) BYTE_ORDER, " \
		    "width = (int) 32" \
		  )

static GstStaticPadTemplate gst_peaq_ref_template =
GST_STATIC_PAD_TEMPLATE ("ref",
			 GST_PAD_SINK,
			 GST_PAD_ALWAYS,
			 STATIC_CAPS);


static GstStaticPadTemplate gst_peaq_test_template =
GST_STATIC_PAD_TEMPLATE ("test",
			 GST_PAD_SINK,
			 GST_PAD_ALWAYS,
			 STATIC_CAPS);
static gdouble amin[] = {
  393.916656, 361.965332, -24.045116, 1.110661, -0.206623, 0.074318, 1.113683,
  0.950345, 0.029985, 0.000101, 0
};

static gdouble amax[] = {
  921, 881.131226, 16.212030, 107.137772, 2.886017, 13.933351, 63.257874,
  1145.018555, 14.819740, 1, 1
};

static gdouble wx[11][3] = {
  {-0.502657, 0.436333, 1.219602},
  {4.307481, 3.246017, 1.123743},
  {4.984241, -2.211189, -0.192096},
  {0.051056, -1.762424, 4.331315},
  {2.321580, 1.789971, -0.754560},
  {-5.303901, -3.452257, -10.814982},
  {2.730991, -6.111805, 1.519223},
  {0.624950, -1.331523, -5.955151},
  {3.102889, 0.871260, -5.922878},
  {-1.051468, -0.939882, -0.142913},
  {-1.804679, -0.503610, -0.620456}
};

static double wxb[] = { -2.518254, 0.654841, -2.207228 };
static double wy[] = { -3.817048, 4.107138, 4.629582, -0.307594 };

static void gst_peaq_finalize (GObject * object);
static void gst_peaq_get_property (GObject * obj, guint id, GValue * value,
				   GParamSpec * pspec);
static GstFlowReturn gst_peaq_collected (GstCollectPads * pads,
					 gpointer user_data);
static GstStateChangeReturn gst_peaq_change_state (GstElement * element,
						   GstStateChange transition);
static void gst_peaq_process_block (GstPeaq * peaq, gfloat * refdata,
				    gfloat * testdata);
static double gst_peaq_calculate_odg (GstPeaq * peaq);
static gboolean is_frame_above_threshold (gfloat * framedata);

static void
gst_peaq_base_init (gpointer g_class)
{
  GstPeaqClass *peaq_class = GST_PEAQ_CLASS (g_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GObjectClass *gobject_class = G_OBJECT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
				      gst_static_pad_template_get
				      (&gst_peaq_ref_template));
  gst_element_class_add_pad_template (element_class,
				      gst_static_pad_template_get
				      (&gst_peaq_test_template));
  gst_element_class_set_details (element_class, &peaq_details);

  element_class->change_state = gst_peaq_change_state;
  gobject_class->finalize = gst_peaq_finalize;

  peaq_class->window_length = BLOCKSIZE;
  peaq_class->sampling_rate = 48000;
}

static void
gst_peaq_class_init (GstPeaqClass * peaq_class)
{
  guint i;
  GObjectClass *object_class = G_OBJECT_CLASS (peaq_class);

  peaq_class->masking_difference = g_new (gdouble, CRITICAL_BAND_COUNT);
  peaq_class->correlation_window = g_new (gdouble, MAXLAG);
  for (i = 0; i < CRITICAL_BAND_COUNT; i++)
    peaq_class->masking_difference[i] =
      pow (10, (i * 0.25 <= 12 ? 3 : 0.25 * i * 0.25) / 10);
  for (i = 0; i < MAXLAG; i++)
    peaq_class->correlation_window[i] = 0.81649658092773 *
      (1 - cos (2 * M_PI * i / (MAXLAG - 1))) / MAXLAG;
  peaq_class->correlation_fft_data = create_fft_data (MAXLAG);

  object_class->get_property = gst_peaq_get_property;
  g_object_class_install_property (object_class,
				   PROP_ODG,
				   g_param_spec_double ("odg",
							"objective differnece grade",
							"Objective Difference Grade",
							-4, 0, 0,
							G_PARAM_READABLE));
}

static void
gst_peaq_init (GstPeaq * peaq, GstPeaqClass * g_class)
{
  GstPadTemplate *template;

  peaq->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (peaq->collect, gst_peaq_collected, peaq);

  peaq->ref_adapter = gst_adapter_new ();
  peaq->test_adapter = gst_adapter_new ();

  template = gst_static_pad_template_get (&gst_peaq_ref_template);
  peaq->refpad = gst_pad_new_from_template (template, "ref");
  gst_object_unref (template);
  gst_collect_pads_add_pad (peaq->collect, peaq->refpad,
			    sizeof (GstCollectData));
  gst_element_add_pad (GST_ELEMENT (peaq), peaq->refpad);

  template = gst_static_pad_template_get (&gst_peaq_test_template);
  peaq->testpad = gst_pad_new_from_template (template, "test");
  gst_object_unref (template);
  gst_collect_pads_add_pad (peaq->collect, peaq->testpad,
			    sizeof (GstCollectData));
  gst_element_add_pad (GST_ELEMENT (peaq), peaq->testpad);

  peaq->frame_counter = 0;

  peaq->ref_ear_model = g_object_new (PEAQ_TYPE_EARMODEL, NULL);
  peaq->test_ear_model = g_object_new (PEAQ_TYPE_EARMODEL, NULL);
  peaq->level_adapter = g_object_new (PEAQ_TYPE_LEVELADAPTER, NULL);
  peaq->ref_modulation_processor =
    g_object_new (PEAQ_TYPE_MODULATIONPROCESSOR, NULL);
  peaq->test_modulation_processor =
    g_object_new (PEAQ_TYPE_MODULATIONPROCESSOR, NULL);

  peaq->current_aggregated_data = NULL;
  peaq->saved_aggregated_data = NULL;
}

static void
gst_peaq_finalize (GObject * object)
{
  GstPeaq *peaq = GST_PEAQ (object);
  g_object_unref (peaq->collect);
  g_object_unref (peaq->ref_adapter);
  g_object_unref (peaq->test_adapter);
  g_object_unref (peaq->level_adapter);
  g_object_unref (peaq->ref_modulation_processor);
  g_object_unref (peaq->test_modulation_processor);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_peaq_get_property (GObject * obj, guint id, GValue * value,
		       GParamSpec * pspec)
{
  GstPeaq *peaq = GST_PEAQ (obj);
  switch (id) {
    case PROP_ODG:
      g_value_set_double (value, gst_peaq_calculate_odg (peaq));
      break;
  }
}

static GstFlowReturn
gst_peaq_collected (GstCollectPads * pads, gpointer user_data)
{
  GstPeaq *peaq;
  GSList *collected;
  gboolean data_received;

  peaq = GST_PEAQ (user_data);

  data_received = FALSE;
  for (collected = pads->data; collected;
       collected = g_slist_next (collected)) {
    GstBuffer *buf;
    GstCollectData *data;
    guint len;

    data = (GstCollectData *) collected->data;
    buf = gst_collect_pads_pop (pads, data);
    while (buf != NULL) {
      data_received = TRUE;
      len = GST_BUFFER_SIZE (buf);
      if (data->pad == peaq->refpad) {
	gst_adapter_push (peaq->ref_adapter, buf);
      } else if (data->pad == peaq->testpad) {
	gst_adapter_push (peaq->test_adapter, buf);
      }
      buf = gst_collect_pads_pop (pads, data);
    }
  }

  while (gst_adapter_available (peaq->ref_adapter) >= BLOCKSIZE_BYTES
	 && gst_adapter_available (peaq->test_adapter) >= BLOCKSIZE_BYTES) {
    gfloat *refframe = (gfloat *) gst_adapter_peek (peaq->ref_adapter,
						    BLOCKSIZE_BYTES);
    gfloat *testframe = (gfloat *) gst_adapter_peek (peaq->test_adapter,
						     BLOCKSIZE_BYTES);
    gst_peaq_process_block (peaq, refframe, testframe);
    gst_adapter_flush (peaq->ref_adapter, STEPSIZE_BYTES);
    gst_adapter_flush (peaq->test_adapter, STEPSIZE_BYTES);
  }

  if (!data_received) {
    gst_element_post_message (GST_ELEMENT_CAST (peaq),
			      gst_message_new_eos (GST_OBJECT_CAST (peaq)));
  }

  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_peaq_change_state (GstElement * element, GstStateChange transition)
{
  GstPeaq *peaq;
  guint ref_data_left_count, test_data_left_count;

  peaq = GST_PEAQ (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_collect_pads_start (peaq->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      ref_data_left_count = gst_adapter_available (peaq->ref_adapter);
      test_data_left_count = gst_adapter_available (peaq->test_adapter);
      if (ref_data_left_count || test_data_left_count) {
	gfloat padded_ref_frame[BLOCKSIZE];
	gfloat padded_test_frame[BLOCKSIZE];
	gfloat *refframe = (gfloat *) gst_adapter_peek (peaq->ref_adapter,
							MIN
							(ref_data_left_count,
							 BLOCKSIZE_BYTES));
	gfloat *testframe = (gfloat *) gst_adapter_peek (peaq->test_adapter,
							 MIN
							 (test_data_left_count,
							  BLOCKSIZE_BYTES));
	g_memmove (padded_ref_frame, refframe, ref_data_left_count);
	memset (((char *) padded_ref_frame) + ref_data_left_count, 0,
		BLOCKSIZE_BYTES - ref_data_left_count);
	g_memmove (padded_test_frame, testframe, test_data_left_count);
	memset (((char *) padded_test_frame) + test_data_left_count, 0,
		BLOCKSIZE_BYTES - test_data_left_count);
	gst_peaq_process_block (peaq, padded_ref_frame, padded_test_frame);
	gst_adapter_flush (peaq->ref_adapter, ref_data_left_count);
	gst_adapter_flush (peaq->test_adapter, test_data_left_count);
      }
      /* need to unblock the collectpads before calling the
       * parent change_state so that streaming can finish */
      gst_collect_pads_stop (peaq->collect);

      gst_peaq_calculate_odg (peaq);
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static void
gst_peaq_process_block (GstPeaq * peaq, gfloat * refdata, gfloat * testdata)
{
  guint i;
  EarModelOutput ref_ear_output;
  EarModelOutput test_ear_output;
  LevelAdapterOutput level_output;
  ModulationProcessorOutput ref_mod_output;
  ModulationProcessorOutput test_mod_output;
  gdouble noise_spectrum[FRAMESIZE / 2 + 1];
  gdouble noise_in_bands[CRITICAL_BAND_COUNT];
  gdouble mod_diff_1b;
  gdouble mod_diff_2b;
  gdouble temp_wt;
  gdouble noise_loudness;
  gdouble zero_threshold;
  guint bw_ref;
  guint bw_test;
  gdouble detection_probability;
  gdouble detection_steps;
  gdouble nmr;
  gdouble nmr_max;
  gdouble energy;
  gboolean ehs_valid;
  gdouble ehs;

  GstPeaqClass *peaq_class = GST_PEAQ_GET_CLASS (peaq);
  PeaqEarModelClass *ear_class =
    PEAQ_EARMODEL_GET_CLASS (peaq->ref_ear_model);

  peaq_earmodel_process (peaq->ref_ear_model, refdata, &ref_ear_output);
  peaq_earmodel_process (peaq->test_ear_model, testdata, &test_ear_output);
  for (i = 0; i < FRAMESIZE / 2 + 1; i++)
    noise_spectrum[i] =
      ref_ear_output.weighted_power_spectrum[i] -
      2 * sqrt (ref_ear_output.weighted_power_spectrum[i] *
		test_ear_output.weighted_power_spectrum[i]) +
      test_ear_output.weighted_power_spectrum[i];
  peaq_earmodel_group_into_bands (ear_class, noise_spectrum, noise_in_bands);
  peaq_leveladapter_process (peaq->level_adapter, ref_ear_output.excitation,
			     test_ear_output.excitation, &level_output);
  peaq_modulationprocessor_process (peaq->ref_modulation_processor,
				    ref_ear_output.unsmeared_excitation,
				    &ref_mod_output);
  peaq_modulationprocessor_process (peaq->test_modulation_processor,
				    test_ear_output.unsmeared_excitation,
				    &test_mod_output);

  /* modulation difference */
  mod_diff_1b = 0.;
  mod_diff_2b = 0.;
  temp_wt = 0.;
  for (i = 0; i < CRITICAL_BAND_COUNT; i++) {
    gdouble w;
    gdouble diff = ABS (test_mod_output.modulation[i] -
			ref_mod_output.modulation[i]);
    mod_diff_1b += diff / (1 + ref_mod_output.modulation[i]);
    w =
      test_mod_output.modulation[i] >= ref_mod_output.modulation[i] ? 1 : .1;
    mod_diff_2b += w * diff / (0.01 + ref_mod_output.modulation[i]);
    temp_wt += ref_mod_output.average_loudness[i] /
      (ref_mod_output.average_loudness[i] + 100 *
       pow (ear_class->internal_noise_level[i], 0.3));
  }
  mod_diff_1b *= 100. / CRITICAL_BAND_COUNT;
  mod_diff_2b *= 100. / CRITICAL_BAND_COUNT;

  /* noise loudness */
  noise_loudness = 0.;
  for (i = 0; i < CRITICAL_BAND_COUNT; i++) {
    gdouble sref = 0.15 * ref_mod_output.modulation[i] + 0.5;
    gdouble stest = 0.15 * test_mod_output.modulation[i] + 0.5;
    gdouble ethres = ear_class->internal_noise_level[i];
    gdouble ep_ref = level_output.spectrally_adapted_ref_patterns[i];
    gdouble ep_test = level_output.spectrally_adapted_test_patterns[i];
    gdouble beta = exp (-1.5 * (ep_test - ep_ref) / ep_ref);
    noise_loudness += pow (1. / stest * ethres, 0.23) *
      (pow (1 + MAX (stest * ep_test - sref * ep_ref, 0) /
	    (ethres + sref * ep_ref * beta), 0.23) - 1.);
  }
  noise_loudness *= 24. / CRITICAL_BAND_COUNT;

  /* bandwidth */
  zero_threshold = test_ear_output.power_spectrum[921];
  for (i = 922; i < 1024; i++)
    if (test_ear_output.power_spectrum[i] > zero_threshold)
      zero_threshold = test_ear_output.power_spectrum[i];
  bw_ref = 0;
  for (i = 921; i > 0; i--)
    if (ref_ear_output.power_spectrum[i - 1] > 10 * zero_threshold) {
      bw_ref = i;
      break;
    }
  bw_test = 0;
  for (i = bw_ref; i > 0; i--)
    if (test_ear_output.power_spectrum[i - 1] >
	3.16227766016838 * zero_threshold) {
      bw_test = i;
      break;
    }

  /* noise-to-mask ratio */
  nmr = 0.;
  nmr_max = 0.;
  for (i = 0; i < CRITICAL_BAND_COUNT; i++) {
    gdouble mask = ref_ear_output.excitation[i] /
      GST_PEAQ_GET_CLASS (peaq)->masking_difference[i];
    gdouble curr_nmr = noise_in_bands[i] / mask;
    nmr += curr_nmr;
    if (curr_nmr > nmr_max)
      nmr_max = curr_nmr;
  }
  nmr /= CRITICAL_BAND_COUNT;

  /* probability of detection */
  detection_probability = 1.;
  detection_steps = 0.;
  for (i = 0; i < CRITICAL_BAND_COUNT; i++) {
    gdouble eref_db = 10 * log10 (ref_ear_output.excitation[i]);
    gdouble etest_db = 10 * log10 (test_ear_output.excitation[i]);
    gdouble l = 0.3 * MAX (eref_db, etest_db) + 0.7 * etest_db;
    gdouble s = l > 0 ? 5.95072 * pow (6.39468 / l, 1.71332) +
      9.01033e-11 * pow (l, 4) + 5.05622e-6 * pow (l, 3) -
      0.00102438 * l * l + 0.0550197 * l - 0.198719 : 1e30;
    gdouble e = eref_db - etest_db;
    gdouble b = eref_db > etest_db ? 4 : 6;
    gdouble pc = 1 - pow (0.5, pow (e / s, b));
    gdouble qc = ABS ((gint) e) / s;
    detection_probability *= 1 - pc;
    detection_steps += qc;
  }
  detection_probability = 1 - detection_probability;

  /* error harmonic structure */
  energy = 0.;
  ehs_valid = FALSE;
  for (i = FRAMESIZE / 2; i < FRAMESIZE; i++)
    energy += refdata[i] * refdata[i];
  if (energy > EHS_ENERGY_THRESHOLD)
    ehs_valid = TRUE;
  else {
    energy = 0.;
    for (i = FRAMESIZE / 2; i < FRAMESIZE; i++)
      energy += testdata[i] * testdata[i];
    if (energy > EHS_ENERGY_THRESHOLD)
      ehs_valid = TRUE;
  }
  if (ehs_valid) {
    gdouble d[FRAMESIZE / 2 + 1];
    gdouble c[MAXLAG];
    gdouble d0;
    gdouble dk;
    gdouble cavg;
    gdouble s_real[MAXLAG];
    gdouble s_imag[MAXLAG];
    gdouble s;
    for (i = 0; i < FRAMESIZE / 2 + 1; i++) {
      gdouble fref = ref_ear_output.power_spectrum[i];
      gdouble ftest = test_ear_output.power_spectrum[i];
      if (fref > 0)
	d[i] = log (ftest / fref);
      else
	d[i] = 0.;
    }
    for (i = 0; i < MAXLAG; i++) {
      guint k;
      c[i] = 0;
      for (k = 0; k < MAXLAG; k++)
	c[i] += d[k] * d[k + i];
    }
    d0 = c[0];
    dk = d0;
    cavg = 0;
    for (i = 0; i < MAXLAG; i++) {
      c[i] /= sqrt (d0 * dk);
      cavg += c[i];
      dk += d[i + MAXLAG] * d[i + MAXLAG] - d[i] * d[i];
    }
    cavg /= MAXLAG;
    for (i = 0; i < MAXLAG; i++)
      c[i] = (c[i] - cavg) * peaq_class->correlation_window[i];
    compute_real_fft (peaq_class->correlation_fft_data, c, s_real, s_imag);
    ehs = 0;
    s = s_real[0] * s_real[0] + s_imag[0] * s_imag[0];
    for (i = 1; i < MAXLAG / 2 + 1; i++) {
      gdouble new_s = s_real[i] * s_real[i] + s_imag[i] * s_imag[i];
      if (new_s > s && new_s > ehs)
	ehs = new_s;
      s = new_s;
    }
  }

  g_printf ("  Ntot   : %f %f\n"
	    "  ModDiff: %f %f\n"
	    "  NL     : %f\n"
	    "  BW     : %d %d\n"
	    "  NMR    : %f %f\n"
	    "  PD     : %f %f\n"
	    "  EHS    : %f\n",
	    ref_ear_output.overall_loudness, test_ear_output.overall_loudness,
	    mod_diff_1b, mod_diff_2b,
	    noise_loudness,
	    bw_ref, bw_test,
	    nmr, nmr_max,
	    detection_probability, detection_steps,
	    ehs_valid ? 1000 * ehs : -1);

  if (is_frame_above_threshold (refdata)) {
    if (peaq->current_aggregated_data == NULL) {
      peaq->current_aggregated_data = g_new (GstPeaqAggregatedData, 1);
      peaq->current_aggregated_data->frame_count = 0;
      peaq->current_aggregated_data->delayed_frame_count = 0;
      peaq->current_aggregated_data->distorted_frame_count = 0;
      peaq->current_aggregated_data->ehs_frame_count = 0;
      peaq->current_aggregated_data->bandwidth_frame_count = 0;
      peaq->current_aggregated_data->noise_loud_frame_count = 0;
      peaq->current_aggregated_data->disturbed_frame_count = 0;
      peaq->current_aggregated_data->loudness_reached = FALSE;
      peaq->current_aggregated_data->ref_bandwidth_sum = 0;
      peaq->current_aggregated_data->test_bandwidth_sum = 0;
      peaq->current_aggregated_data->nmr_sum = 0;
      peaq->current_aggregated_data->win_mod_diff1 = 0;
      peaq->current_aggregated_data->past_mod_diff1[0] = 0;
      peaq->current_aggregated_data->past_mod_diff1[1] = 0;
      peaq->current_aggregated_data->past_mod_diff1[2] = 0;
      peaq->current_aggregated_data->avg_mod_diff1 = 0;
      peaq->current_aggregated_data->avg_mod_diff2 = 0;
      peaq->current_aggregated_data->temp_weight = 0;
      peaq->current_aggregated_data->detection_steps = 0;
      peaq->current_aggregated_data->ehs = 0;
      peaq->current_aggregated_data->noise_loudness = 0;
      peaq->current_aggregated_data->filtered_detection_probability = 0;
      peaq->current_aggregated_data->max_filtered_detection_probability = 0;
    }
    if (peaq->saved_aggregated_data != NULL) {
      g_free (peaq->saved_aggregated_data);
      peaq->saved_aggregated_data = NULL;
    }
  } else {
    if (peaq->saved_aggregated_data == NULL) {
      peaq->saved_aggregated_data = g_memdup (peaq->current_aggregated_data,
					      sizeof (GstPeaqAggregatedData));
    }
  }

  if (peaq->current_aggregated_data != NULL) {
    peaq->current_aggregated_data->frame_count++;
    if (bw_ref > 346) {
      peaq->current_aggregated_data->ref_bandwidth_sum += bw_ref;
      peaq->current_aggregated_data->test_bandwidth_sum += bw_test;
      peaq->current_aggregated_data->bandwidth_frame_count++;
    }
    peaq->current_aggregated_data->nmr_sum += nmr;
    if (ref_ear_output.overall_loudness > 0.1 ||
	test_ear_output.overall_loudness > 0.1)
      peaq->current_aggregated_data->loudness_reached = TRUE;
    if (peaq->frame_counter >= 24) {
      gdouble winsum;
      peaq->current_aggregated_data->delayed_frame_count++;
      winsum = sqrt (mod_diff_1b);
      for (i = 0; i < 3; i++) {
	winsum += sqrt (peaq->current_aggregated_data->past_mod_diff1[i]);
      }
      for (i = 0; i < 2; i++) {
	peaq->current_aggregated_data->past_mod_diff1[i] =
	  peaq->current_aggregated_data->past_mod_diff1[i + 1];
      }
      peaq->current_aggregated_data->past_mod_diff1[2] = mod_diff_1b;
      if (peaq->frame_counter >= 27)
	peaq->current_aggregated_data->win_mod_diff1 += pow (winsum / 4, 4);
      peaq->current_aggregated_data->avg_mod_diff1 += temp_wt * mod_diff_1b;
      peaq->current_aggregated_data->avg_mod_diff2 += temp_wt * mod_diff_2b;
      peaq->current_aggregated_data->temp_weight += temp_wt;
      if (peaq->current_aggregated_data->loudness_reached) {
	peaq->current_aggregated_data->noise_loud_frame_count++;
	peaq->current_aggregated_data->noise_loudness +=
	  noise_loudness * noise_loudness;
      }
    }
    if (detection_probability > 0.5) {
      peaq->current_aggregated_data->distorted_frame_count++;
      peaq->current_aggregated_data->detection_steps += detection_steps;
    }
    if (ehs_valid) {
      peaq->current_aggregated_data->ehs_frame_count++;
      peaq->current_aggregated_data->ehs += ehs;
    }
    peaq->current_aggregated_data->filtered_detection_probability =
      0.9 * peaq->current_aggregated_data->filtered_detection_probability +
      0.1 * detection_probability;
    if (peaq->current_aggregated_data->filtered_detection_probability >
	peaq->current_aggregated_data->max_filtered_detection_probability)
      peaq->current_aggregated_data->max_filtered_detection_probability =
	peaq->current_aggregated_data->filtered_detection_probability;
    if (nmr_max > 1.41253754462275)
      peaq->current_aggregated_data->disturbed_frame_count++;
  }
  peaq->frame_counter++;
}

static double
gst_peaq_calculate_odg (GstPeaq * peaq)
{
  GstPeaqAggregatedData *agg_data;

  if (peaq->saved_aggregated_data)
    agg_data = peaq->saved_aggregated_data;
  else
    agg_data = peaq->current_aggregated_data;

  if (agg_data) {
    guint i;
    gdouble movs[11];
    gdouble x[3];
    gdouble distortion_index;
    gdouble odg;
    gdouble bw_ref_b =
      agg_data->bandwidth_frame_count ?
      agg_data->ref_bandwidth_sum / agg_data->bandwidth_frame_count : 0;
    gdouble bw_test_b =
      agg_data->bandwidth_frame_count ?
      agg_data->test_bandwidth_sum / agg_data->bandwidth_frame_count : 0;
    gdouble total_nmr_b =
      10 * log10 (agg_data->nmr_sum / agg_data->frame_count);
    gdouble win_mod_diff1_b =
      sqrt (agg_data->win_mod_diff1 / (agg_data->delayed_frame_count - 3));
    gdouble adb_b =
      agg_data->distorted_frame_count > 0 ? (agg_data->detection_steps ==
					     0 ? -0.5 : log10 (agg_data->
							       detection_steps
							       /
							       agg_data->
							       distorted_frame_count))
      : 0;
    gdouble ehs_b = 1000 * agg_data->ehs / agg_data->ehs_frame_count;
    gdouble avg_mod_diff1_b = agg_data->avg_mod_diff1 / agg_data->temp_weight;
    gdouble avg_mod_diff2_b = agg_data->avg_mod_diff2 / agg_data->temp_weight;
    gdouble rms_noise_loud_b =
      sqrt (agg_data->noise_loudness / agg_data->noise_loud_frame_count);
    gdouble mfpd_b = agg_data->max_filtered_detection_probability;
    gdouble rel_dist_frames_b =
      (gdouble) agg_data->disturbed_frame_count / agg_data->frame_count;
    movs[0] = bw_ref_b;
    movs[1] = bw_test_b;
    movs[2] = total_nmr_b;
    movs[3] = win_mod_diff1_b;
    movs[4] = adb_b;
    movs[5] = ehs_b;
    movs[6] = avg_mod_diff1_b;
    movs[7] = avg_mod_diff2_b;
    movs[8] = rms_noise_loud_b;
    movs[9] = mfpd_b;
    movs[10] = rel_dist_frames_b;
    for (i = 0; i < 3; i++)
      x[i] = 0;
    for (i = 0; i <= 10; i++) {
      guint j;
      gdouble m = (movs[i] - amin[i]) / (amax[i] - amin[i]);
      for (j = 0; j < 3; j++)
	x[j] += wx[i][j] * m;
    }
    distortion_index = -0.307594;
    for (i = 0; i < 3; i++)
      distortion_index += wy[i] / (1 + exp (-(wxb[i] + x[i])));
    odg = -3.98 + 4.2 / (1 + exp (-distortion_index));

    g_printf ("   BandwidthRefB: %f\n"
	      "  BandwidthTestB: %f\n"
	      "      Total NMRB: %f\n"
	      "    WinModDiff1B: %f\n"
	      "            ADBB: %f\n"
	      "            EHSB: %f\n"
	      "    AvgModDiff1B: %f\n"
	      "    AvgModDiff2B: %f\n"
	      "   RmsNoiseLoudB: %f\n"
	      "           MFPDB: %f\n"
	      "  RelDistFramesB: %f\n"
	      "Objective Difference Grade: %.3f\n",
	      bw_ref_b, bw_test_b, total_nmr_b, win_mod_diff1_b, adb_b,
	      ehs_b, avg_mod_diff1_b, avg_mod_diff2_b, rms_noise_loud_b,
	      mfpd_b, rel_dist_frames_b, odg);
    return odg;
  }
  return 0;
}

static gboolean
is_frame_above_threshold (gfloat * framedata)
{
  gfloat sum;
  guint i;

  sum = 0;
  for (i = 0; i < 5; i++)
    sum += ABS (framedata[i]);
  while (i < FRAMESIZE) {
    sum += ABS (framedata[i]) - ABS (framedata[i - 5]);
    if (sum >= 200. / 32768)
      return TRUE;
    i++;
  }
  return FALSE;
}
