/* GstPEAQ
 * Copyright (C) 2006, 2007, 2010, 2011, 2012, 2013
 * Martin Holters <martin.holters@hsuhh.de>
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
#include "leveladapter.h"

#define FFT_STEPSIZE (FFT_FRAMESIZE/2)
#define FFT_BLOCKSIZE_BYTES (FFT_FRAMESIZE * sizeof(gfloat))
#define FFT_STEPSIZE_BYTES (FFT_STEPSIZE * sizeof(gfloat))
#define FB_BLOCKSIZE_BYTES (FB_FRAMESIZE * sizeof(gfloat))
//#define EHS_ENERGY_THRESHOLD 7.442401884276241e-6
#define EHS_ENERGY_THRESHOLD 7.45058059692383e-06
#define MAXLAG 256

// #define ADVANCED 1

enum
{
  PROP_0,
  PROP_PLAYBACK_LEVEL,
  PROP_DI,
  PROP_ODG,
  PROP_TOTALSNR,
  PROP_CONSOLE_OUTPUT
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
  guint loudness_reached_frame;
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
  gdouble total_signal_energy;
  gdouble total_noise_energy;
#ifdef ADVANCED
  gdouble nmr_local;
#endif
};

struct _GstPeaqAggregatedDataFB
{
  guint loudness_reached_frame;
  gdouble rms_mod_diff;
  gdouble temp_weight;
  guint noise_loud_frame_count;
  gdouble noise_loudness;
  gdouble missing_components;
  gdouble lin_dist;
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

#ifdef ADVANCED
static gdouble amin_advanced[5] = {
  13.298751, 0.041073, -25.018791, 0.061560, 0.02452
};

static gdouble amax_advanced[5] = {
  2166.5, 13.24326, 13.46708, 10.226771, 14.224874
};

static gdouble wx_advanced[5][5] = {
  {21.211773, -39.013052, -1.382553, -14.545348, -0.320899},
  {-8.981803, 19.956049, 0.935389, -1.686586, -3.238586},
  {1.633830, -2.877505, -7.442935, 5.606502, -1.783120},
  {6.103821, 19.587435, -0.240284, 1.088213, -0.511314},
  {11.556344, 3.892028, 9.720441, -3.287205, -11.031250},
};

static double wxb_advanced[5] = { 1.330890, 2.686103, 2.096598, -1.327851, 3.087055 };

static double wy_advanced[5] = { -4.696996, -3.289959, 7.004782, 6.651897, 4.009144 };
#else
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
#endif

static void gst_peaq_finalize (GObject * object);
static void gst_peaq_get_property (GObject * obj, guint id, GValue * value,
				   GParamSpec * pspec);
static void gst_peaq_set_property (GObject * obj, guint id,
				   const GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_peaq_collected (GstCollectPads * pads,
					 gpointer user_data);
static GstStateChangeReturn gst_peaq_change_state (GstElement * element,
						   GstStateChange transition);
static void gst_peaq_process_fft_block (GstPeaq *peaq, gfloat *refdata,
                                        gfloat *testdata);
#ifdef ADVANCED
static void gst_peaq_process_fb_block (GstPeaq *peaq, gfloat *refdata,
                                       gfloat *testdata);
#endif
static void calc_modulation_difference(PeaqEarModelParams const *params,
                                       ModulationProcessorOutput const *ref_mod_output,
                                       ModulationProcessorOutput const *test_mod_output,
                                       gdouble *mod_diff_1b,
                                       gdouble *mod_diff_2b,
                                       gdouble *temp_wt);
#ifdef ADVANCED
static void calc_modulation_difference_A (PeaqEarModelParams const *params,
                                          ModulationProcessorOutput const *ref_mod_output,
                                          ModulationProcessorOutput const *test_mod_output,
                                          gdouble *mod_diff_a, gdouble *temp_wt);
#endif
static gdouble calc_noise_loudness(PeaqEarModelParams const *params,
                                   gdouble alpha, gdouble thres_fac, gdouble S0,
                                   gdouble NLmin,
                                   gdouble const *ref_modulation,
                                   gdouble const *test_modulation,
                                   gdouble const *ref_excitation,
                                   gdouble const *test_excitation);
static void calc_bandwidth(gdouble const *ref_power_spectrum,
                           gdouble const *test_power_spectrum,
                           guint *bw_test, guint *bw_ref);
static void calc_nmr(GstPeaq const *peaq, gdouble const *ref_excitation,
                     gdouble *noise_in_bands, gdouble *nmr, gdouble *nmr_max);
static void calc_prob_detect(guint band_count, gdouble const *ref_excitation,
                             gdouble const *test_excitation,
                             gdouble *detection_probability,
                             gdouble *detection_steps);
static void calc_ehs (GstPeaq const *peaq, gfloat const *refdata,
                      gfloat const *testdata,
                      gdouble const *ref_power_spectrum,
                      gdouble const *test_power_spectrum,
                      gboolean *ehs_valid, gdouble *ehs);
#ifndef ADVANCED
static double gst_peaq_calculate_di (GstPeaq * peaq);
#else
static double gst_peaq_calculate_di_advanced (GstPeaq *peaq);
#endif
static double gst_peaq_calculate_odg (GstPeaq * peaq);
static gboolean is_frame_above_threshold (gfloat *framedata, guint framesize);

gboolean query(GstElement *element, GstQuery *query)
{
  switch (query->type) {
    case GST_QUERY_LATENCY:
      /* we are not live, no latency compensation required */
      gst_query_set_latency (query, FALSE, 0, -1);
      return TRUE;
    default:
      return parent_class->query(element, query);
  }
}


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

  element_class->query = query;

  element_class->change_state = gst_peaq_change_state;
  gobject_class->finalize = gst_peaq_finalize;

  peaq_class->sampling_rate = 48000;
}

static void
gst_peaq_class_init (GstPeaqClass * peaq_class)
{
  guint i;
  GObjectClass *object_class = G_OBJECT_CLASS (peaq_class);

  /* centering the window of the correlation in the EHS computation at lag zero
   * (as considered in [Kabal03] to be more reasonable) improves conformance */
  peaq_class->correlation_window = g_new (gdouble, MAXLAG);
  for (i = 0; i < MAXLAG; i++)
#if 1
    peaq_class->correlation_window[i] = 0.81649658092773 *
      (1 + cos (2 * M_PI * i / (2 * MAXLAG - 1))) / MAXLAG;
#else
    peaq_class->correlation_window[i] = 0.81649658092773 *
      (1 - cos (2 * M_PI * i / (MAXLAG - 1))) / MAXLAG;
#endif

  object_class->get_property = gst_peaq_get_property;
  object_class->set_property = gst_peaq_set_property;
  g_object_class_install_property (object_class,
				   PROP_PLAYBACK_LEVEL,
				   g_param_spec_double ("playback_level",
							"playback level",
							"Playback level in dB",
							0, 130, 92,
							G_PARAM_READWRITE |
							G_PARAM_CONSTRUCT));
  g_object_class_install_property (object_class,
				   PROP_DI,
				   g_param_spec_double ("di",
							"distortion index",
							"Distortion Index",
							-10., 3.5, 0,
							G_PARAM_READABLE));
  g_object_class_install_property (object_class,
				   PROP_ODG,
				   g_param_spec_double ("odg",
							"objective differnece grade",
							"Objective Difference Grade",
							-4, 0, 0,
							G_PARAM_READABLE));
  g_object_class_install_property (object_class,
				   PROP_TOTALSNR,
				   g_param_spec_double ("totalsnr",
							"the overall SNR in dB",
							"the overall signal to noise ratio in dB",
							-1000, 1000, 0,
							G_PARAM_READABLE));
  g_object_class_install_property (object_class,
				   PROP_CONSOLE_OUTPUT,
				   g_param_spec_boolean ("console-output",
							 "console output",
							 "Enable or disable console output",
							 TRUE,
							 G_PARAM_READWRITE |
							 G_PARAM_CONSTRUCT));
}

static void
gst_peaq_init (GstPeaq * peaq, GstPeaqClass * g_class)
{
  guint band_count, i;
  GstPadTemplate *template;
  PeaqEarModelParams *fft_model_params;
  PeaqEarModelParams *fb_model_params;

  peaq->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (peaq->collect, gst_peaq_collected, peaq);

  peaq->ref_adapter_fft = gst_adapter_new ();
  peaq->test_adapter_fft = gst_adapter_new ();
  peaq->ref_adapter_fb = gst_adapter_new ();
  peaq->test_adapter_fb = gst_adapter_new ();

  peaq->correlation_fft = gst_fft_f64_new (MAXLAG, FALSE);
  peaq->correlator_fft = gst_fft_f64_new (2 * MAXLAG, FALSE);
  peaq->correlator_inverse_fft = gst_fft_f64_new (2 * MAXLAG, TRUE);

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

  GST_OBJECT_FLAG_SET (peaq, GST_ELEMENT_IS_SINK);

  peaq->frame_counter = 0;
  peaq->frame_counter_fb = 0;

#ifdef ADVANCED
  peaq->ref_ear = g_object_new (PEAQ_TYPE_FFTEARMODEL, "number-of-bands", 55,
                                NULL);
  peaq->test_ear = g_object_new (PEAQ_TYPE_FFTEARMODEL, "number-of-bands", 55,
                                 NULL);
#else
  peaq->ref_ear = g_object_new (PEAQ_TYPE_FFTEARMODEL, "number-of-bands", 109,
                                 NULL);
  peaq->test_ear = g_object_new (PEAQ_TYPE_FFTEARMODEL, "number-of-bands", 109,
                                 NULL);
#endif
  peaq->ref_ear_fb = g_object_new (PEAQ_TYPE_FILTERBANKEARMODEL, NULL);
  peaq->test_ear_fb = g_object_new (PEAQ_TYPE_FILTERBANKEARMODEL, NULL);

  fft_model_params =
    peaq_earmodel_get_model_params (PEAQ_EARMODEL(peaq->ref_ear));
  fb_model_params =
    peaq_earmodel_get_model_params (PEAQ_EARMODEL(peaq->ref_ear_fb));

  band_count =
    peaq_earmodelparams_get_band_count (fft_model_params);

  peaq->masking_difference = g_new (gdouble, band_count);
  for (i = 0; i < band_count; i++)
    peaq->masking_difference[i] =
      pow (10, (i * 0.25 <= 12 ? 3 : 0.25 * i * 0.25) / 10);

  peaq->level_adapter_fft = peaq_leveladapter_new (fft_model_params);
  peaq->level_adapter_fb = peaq_leveladapter_new (fb_model_params);
  peaq->ref_modulation_processor =
    peaq_modulationprocessor_new (fft_model_params);
  peaq->test_modulation_processor =
    peaq_modulationprocessor_new (fft_model_params);
  peaq->ref_modulation_processor_fb =
    peaq_modulationprocessor_new (fb_model_params);
  peaq->test_modulation_processor_fb =
    peaq_modulationprocessor_new (fb_model_params);

  peaq->current_aggregated_data = NULL;
  peaq->saved_aggregated_data = NULL;
}

static void
gst_peaq_finalize (GObject * object)
{
  GstPeaq *peaq = GST_PEAQ (object);
  g_object_unref (peaq->collect);
  g_object_unref (peaq->ref_adapter_fft);
  g_object_unref (peaq->test_adapter_fft);
  g_object_unref (peaq->ref_adapter_fb);
  g_object_unref (peaq->test_adapter_fb);
  g_object_unref (peaq->ref_ear);
  g_object_unref (peaq->test_ear);
  g_object_unref (peaq->level_adapter_fft);
  g_object_unref (peaq->level_adapter_fb);
  g_object_unref (peaq->ref_modulation_processor);
  g_object_unref (peaq->test_modulation_processor);
  g_object_unref (peaq->ref_modulation_processor_fb);
  g_object_unref (peaq->test_modulation_processor_fb);
  gst_fft_f64_free (peaq->correlation_fft);
  gst_fft_f64_free (peaq->correlator_fft);
  gst_fft_f64_free (peaq->correlator_inverse_fft);
  if (peaq->current_aggregated_data != NULL)
    g_free (peaq->current_aggregated_data);
  if (peaq->saved_aggregated_data != NULL)
    g_free (peaq->saved_aggregated_data);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_peaq_get_property (GObject * obj, guint id, GValue * value,
		       GParamSpec * pspec)
{
  GstPeaq *peaq = GST_PEAQ (obj);
  switch (id) {
    case PROP_PLAYBACK_LEVEL:
      g_object_get_property (G_OBJECT (peaq_earmodel_get_model_params(PEAQ_EARMODEL(peaq->ref_ear))),
			     "playback_level", value);
      break;
    case PROP_DI:
#ifdef ADVANCED
      g_value_set_double (value, gst_peaq_calculate_di_advanced (peaq));
#else
      g_value_set_double (value, gst_peaq_calculate_di (peaq));
#endif
      break;
    case PROP_ODG:
      g_value_set_double (value, gst_peaq_calculate_odg (peaq));
      break;
    case PROP_TOTALSNR:
      {
	GstPeaqAggregatedData *agg_data;

	if (peaq->saved_aggregated_data)
	  agg_data = peaq->saved_aggregated_data;
	else
	  agg_data = peaq->current_aggregated_data;

	if (agg_data)
	  g_value_set_double (value, 10 * log10 (agg_data->total_signal_energy 
						 / agg_data->total_noise_energy));
	else
	  g_value_set_double (value, 0);
      }
      break;
    case PROP_CONSOLE_OUTPUT:
      g_value_set_boolean (value, peaq->console_output);
      break;
  }
}

static void
gst_peaq_set_property (GObject * obj, guint id, const GValue * value,
		       GParamSpec * pspec)
{
  GstPeaq *peaq = GST_PEAQ (obj);
  switch (id) {
    case PROP_PLAYBACK_LEVEL:
      g_object_set_property (G_OBJECT (peaq_earmodel_get_model_params(PEAQ_EARMODEL(peaq->ref_ear))),
			     "playback_level", value);
      g_object_set_property (G_OBJECT (peaq_earmodel_get_model_params(PEAQ_EARMODEL(peaq->test_ear))),
			     "playback_level", value);
      break;
    case PROP_CONSOLE_OUTPUT:
      peaq->console_output = g_value_get_boolean (value);
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

    data = (GstCollectData *) collected->data;
    buf = gst_collect_pads_pop (pads, data);
    while (buf != NULL) {
      data_received = TRUE;
      if (data->pad == peaq->refpad) {
	gst_adapter_push (peaq->ref_adapter_fb, gst_buffer_copy (buf));
	gst_adapter_push (peaq->ref_adapter_fft, buf);
      } else if (data->pad == peaq->testpad) {
	gst_adapter_push (peaq->test_adapter_fb, gst_buffer_copy (buf));
	gst_adapter_push (peaq->test_adapter_fft, buf);
      }
      buf = gst_collect_pads_pop (pads, data);
    }
  }

  while (gst_adapter_available (peaq->ref_adapter_fft) >= FFT_BLOCKSIZE_BYTES &&
         gst_adapter_available (peaq->test_adapter_fft) >= FFT_BLOCKSIZE_BYTES)
  {
    gfloat *refframe = (gfloat *) gst_adapter_peek (peaq->ref_adapter_fft,
                                                    FFT_BLOCKSIZE_BYTES);
    gfloat *testframe = (gfloat *) gst_adapter_peek (peaq->test_adapter_fft,
						     FFT_BLOCKSIZE_BYTES);
    gst_peaq_process_fft_block (peaq, refframe, testframe);
    g_assert (gst_adapter_available (peaq->ref_adapter_fft) >=
              FFT_BLOCKSIZE_BYTES);
    gst_adapter_flush (peaq->ref_adapter_fft, FFT_STEPSIZE_BYTES);
    g_assert (gst_adapter_available (peaq->test_adapter_fft) >=
              FFT_BLOCKSIZE_BYTES);
    gst_adapter_flush (peaq->test_adapter_fft, FFT_STEPSIZE_BYTES);
  }

  while (gst_adapter_available (peaq->ref_adapter_fb) >= FB_BLOCKSIZE_BYTES &&
         gst_adapter_available (peaq->test_adapter_fb) >= FB_BLOCKSIZE_BYTES) {
#ifdef ADVANCED
    gfloat *refframe = (gfloat *) gst_adapter_peek (peaq->ref_adapter_fb,
						    FB_BLOCKSIZE_BYTES);
    gfloat *testframe = (gfloat *) gst_adapter_peek (peaq->test_adapter_fb,
						     FB_BLOCKSIZE_BYTES);
    gst_peaq_process_fb_block (peaq, refframe, testframe);
#endif
    g_assert (gst_adapter_available (peaq->ref_adapter_fb) >=
              FB_BLOCKSIZE_BYTES);
    gst_adapter_flush (peaq->ref_adapter_fb, FB_BLOCKSIZE_BYTES);
    g_assert (gst_adapter_available (peaq->test_adapter_fb) >=
              FB_BLOCKSIZE_BYTES);
    gst_adapter_flush (peaq->test_adapter_fb, FB_BLOCKSIZE_BYTES);
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
      /* need to unblock the collectpads before calling the
       * parent change_state so that streaming can finish */
      gst_collect_pads_stop (peaq->collect);

      // TODO: do something similar for the filter bank adapters
      ref_data_left_count = gst_adapter_available (peaq->ref_adapter_fft);
      test_data_left_count = gst_adapter_available (peaq->test_adapter_fft);
      if (ref_data_left_count || test_data_left_count) {
        gfloat padded_ref_frame[FFT_FRAMESIZE];
        gfloat padded_test_frame[FFT_FRAMESIZE];
        guint ref_data_count = MIN (ref_data_left_count, FFT_BLOCKSIZE_BYTES);
        guint test_data_count = MIN (test_data_left_count, FFT_BLOCKSIZE_BYTES);
        gfloat *refframe = (gfloat *) gst_adapter_peek (peaq->ref_adapter_fft,
							ref_data_count);
        gfloat *testframe = (gfloat *) gst_adapter_peek (peaq->test_adapter_fft,
							 test_data_count);
	g_memmove (padded_ref_frame, refframe, ref_data_count);
	memset (((char *) padded_ref_frame) + ref_data_count, 0,
                FFT_BLOCKSIZE_BYTES - ref_data_count);
	g_memmove (padded_test_frame, testframe, test_data_count);
	memset (((char *) padded_test_frame) + test_data_count, 0,
                FFT_BLOCKSIZE_BYTES - test_data_count);
	gst_peaq_process_fft_block (peaq, padded_ref_frame, padded_test_frame);
	g_assert (gst_adapter_available (peaq->ref_adapter_fft) >= 
		  ref_data_count);
	gst_adapter_flush (peaq->ref_adapter_fft, ref_data_count);
	g_assert (gst_adapter_available (peaq->test_adapter_fft) >= 
		  test_data_count);
	gst_adapter_flush (peaq->test_adapter_fft, test_data_count);
      }

      gst_peaq_calculate_odg (peaq);
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static void
gst_peaq_process_fft_block (GstPeaq *peaq, gfloat *refdata, gfloat *testdata)
{
  guint i;
  FFTEarModelOutput ref_ear_output;
  FFTEarModelOutput test_ear_output;
  LevelAdapterOutput level_output;
  ModulationProcessorOutput ref_mod_output;
  ModulationProcessorOutput test_mod_output;
  gdouble noise_spectrum[FFT_FRAMESIZE / 2 + 1];
  gdouble *noise_in_bands;
  gdouble mod_diff_1b;
  gdouble mod_diff_2b;
  gdouble temp_wt;
  gdouble noise_loudness;
  guint bw_ref;
  guint bw_test;
  gdouble detection_probability;
  gdouble detection_steps;
  gdouble nmr;
  gdouble nmr_max;
  gboolean ehs_valid;
  gdouble ehs = 0.;

  PeaqEarModelParams *ear_params =
    peaq_earmodel_get_model_params(PEAQ_EARMODEL(peaq->ref_ear));
  PeaqFFTEarModelParams *fft_ear_params =
    peaq_fftearmodel_get_fftmodel_params(peaq->ref_ear);
  guint band_count = peaq_earmodelparams_get_band_count (ear_params);

  noise_in_bands = g_newa (gdouble, band_count);

  ref_ear_output.ear_model_output.unsmeared_excitation =
    g_newa (gdouble, band_count);
  ref_ear_output.ear_model_output.excitation = g_newa (gdouble, band_count);
  test_ear_output.ear_model_output.unsmeared_excitation =
    g_newa (gdouble, band_count);
  test_ear_output.ear_model_output.excitation = g_newa (gdouble, band_count);
  peaq_fftearmodel_process (peaq->ref_ear, refdata, &ref_ear_output);
  peaq_fftearmodel_process (peaq->test_ear, testdata, &test_ear_output);
  for (i = 0; i < FFT_FRAMESIZE / 2 + 1; i++)
    noise_spectrum[i] =
      ref_ear_output.weighted_power_spectrum[i] -
      2 * sqrt (ref_ear_output.weighted_power_spectrum[i] *
		test_ear_output.weighted_power_spectrum[i]) +
      test_ear_output.weighted_power_spectrum[i];
  peaq_fftearmodelparams_group_into_bands (fft_ear_params, noise_spectrum,
                                           noise_in_bands);

  level_output.spectrally_adapted_ref_patterns = g_newa (gdouble, band_count);
  level_output.spectrally_adapted_test_patterns = g_newa (gdouble, band_count);
  peaq_leveladapter_process (peaq->level_adapter_fft,
                             ref_ear_output.ear_model_output.excitation,
			     test_ear_output.ear_model_output.excitation,
                             &level_output);

  ref_mod_output.modulation = g_newa (gdouble, band_count);
  peaq_modulationprocessor_process (peaq->ref_modulation_processor,
                                    ref_ear_output.ear_model_output.unsmeared_excitation,
				    &ref_mod_output);
  test_mod_output.modulation = g_newa (gdouble, band_count);
  peaq_modulationprocessor_process (peaq->test_modulation_processor,
                                    test_ear_output.ear_model_output.unsmeared_excitation,
				    &test_mod_output);

  /* modulation difference */
  calc_modulation_difference (ear_params, &ref_mod_output, &test_mod_output,
                              &mod_diff_1b, &mod_diff_2b, &temp_wt);

  /* noise loudness */
  noise_loudness = calc_noise_loudness (ear_params,
                                        1.5, 0.15, 0.5, 0.,
                                        ref_mod_output.modulation,
                                        test_mod_output.modulation,
                                        level_output.spectrally_adapted_ref_patterns,
                                        level_output.spectrally_adapted_test_patterns);

  /* bandwidth */
  calc_bandwidth (ref_ear_output.power_spectrum,
                  test_ear_output.power_spectrum, &bw_test, &bw_ref);

  /* noise-to-mask ratio */
  calc_nmr (peaq, ref_ear_output.ear_model_output.excitation,
            noise_in_bands, &nmr, &nmr_max);

  /* probability of detection */
  calc_prob_detect(band_count, ref_ear_output.ear_model_output.excitation,
                   test_ear_output.ear_model_output.excitation,
                   &detection_probability, &detection_steps);

  /* error harmonic structure */
  calc_ehs (peaq, refdata, testdata, ref_ear_output.power_spectrum,
            test_ear_output.power_spectrum, &ehs_valid, &ehs);

  if (peaq->console_output) {
    g_printf ("  Ntot   : %f %f\n"
	      "  ModDiff: %f %f\n"
	      "  NL     : %f\n"
	      "  BW     : %d %d\n"
	      "  NMR    : %f %f\n"
	      "  PD     : %f %f\n"
	      "  EHS    : %f\n",
	      ref_ear_output.ear_model_output.overall_loudness,
	      test_ear_output.ear_model_output.overall_loudness,
	      mod_diff_1b, mod_diff_2b,
	      noise_loudness,
	      bw_ref, bw_test,
	      nmr, nmr_max,
	      detection_probability, detection_steps,
	      ehs_valid ? 1000 * ehs : -1);
  }

  if (is_frame_above_threshold (refdata, FFT_FRAMESIZE)) {
    if (peaq->current_aggregated_data == NULL) {
      peaq->current_aggregated_data = g_new (GstPeaqAggregatedData, 1);
      peaq->current_aggregated_data->frame_count = 0;
      peaq->current_aggregated_data->delayed_frame_count = 0;
      peaq->current_aggregated_data->distorted_frame_count = 0;
      peaq->current_aggregated_data->ehs_frame_count = 0;
      peaq->current_aggregated_data->bandwidth_frame_count = 0;
      peaq->current_aggregated_data->noise_loud_frame_count = 0;
      peaq->current_aggregated_data->disturbed_frame_count = 0;
      peaq->current_aggregated_data->loudness_reached_frame = G_MAXUINT;
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
      peaq->current_aggregated_data->total_signal_energy = 0;
      peaq->current_aggregated_data->total_noise_energy = 0;
#ifdef ADVANCED
      peaq->current_aggregated_data->nmr_local = 0.;
#endif
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
#ifdef ADVANCED
    peaq->current_aggregated_data->nmr_local += 10. * log10 (nmr);
#endif
    if (peaq->current_aggregated_data->loudness_reached_frame == G_MAXUINT &&
        ref_ear_output.ear_model_output.overall_loudness > 0.1 &&
	test_ear_output.ear_model_output.overall_loudness > 0.1)
      peaq->current_aggregated_data->loudness_reached_frame = peaq->frame_counter;
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
      if (peaq->frame_counter >= peaq->current_aggregated_data->loudness_reached_frame + 3) {
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

    for (i = 0; i < FFT_FRAMESIZE / 2; i++) {
      peaq->current_aggregated_data->total_signal_energy
        += refdata[i] * refdata[i];
      peaq->current_aggregated_data->total_noise_energy 
	+= (refdata[i] - testdata[i]) * (refdata[i] - testdata[i]);
    }
  }
  peaq->frame_counter++;
}

#ifdef ADVANCED
static void
gst_peaq_process_fb_block (GstPeaq *peaq, gfloat *refdata, gfloat *testdata)
{
  EarModelOutput ref_ear_output;
  EarModelOutput test_ear_output;
  LevelAdapterOutput level_output;
  ModulationProcessorOutput ref_mod_output;
  ModulationProcessorOutput test_mod_output;
  gdouble mod_diff_a;
  gdouble temp_wt;
  gdouble noise_loudness;
  gdouble missing_components;
  gdouble lin_dist;

  PeaqEarModelParams *ear_params =
    peaq_earmodel_get_model_params(PEAQ_EARMODEL(peaq->ref_ear_fb));
  guint band_count = peaq_earmodelparams_get_band_count (ear_params);

  ref_ear_output.unsmeared_excitation = g_newa (gdouble, band_count);
  ref_ear_output.excitation = g_newa (gdouble, band_count);
  test_ear_output.unsmeared_excitation = g_newa (gdouble, band_count);
  test_ear_output.excitation = g_newa (gdouble, band_count);
  peaq_filterbankearmodel_process (peaq->ref_ear_fb, refdata, &ref_ear_output);
  peaq_filterbankearmodel_process (peaq->test_ear_fb, testdata,
                                   &test_ear_output);

  level_output.spectrally_adapted_ref_patterns = g_newa (gdouble, band_count);
  level_output.spectrally_adapted_test_patterns = g_newa (gdouble, band_count);
  peaq_leveladapter_process (peaq->level_adapter_fb, ref_ear_output.excitation,
                             test_ear_output.excitation, &level_output);

  ref_mod_output.modulation = g_newa (gdouble, band_count);
  peaq_modulationprocessor_process (peaq->ref_modulation_processor_fb,
                                    ref_ear_output.unsmeared_excitation,
                                    &ref_mod_output);
  test_mod_output.modulation = g_newa (gdouble, band_count);
  peaq_modulationprocessor_process (peaq->test_modulation_processor_fb,
                                    test_ear_output.unsmeared_excitation,
                                    &test_mod_output);

  /* modulation difference */
  calc_modulation_difference_A (ear_params, &ref_mod_output, &test_mod_output,
                                &mod_diff_a, &temp_wt);

  /* noise loudness */
  noise_loudness =
    calc_noise_loudness (ear_params, 2.5, 0.3, 1, 0.1,
                         ref_mod_output.modulation, test_mod_output.modulation,
                         level_output.spectrally_adapted_ref_patterns,
                         level_output.spectrally_adapted_test_patterns);
  /* TODO: should the modulation patterns really also be swapped? */
  missing_components =
    calc_noise_loudness (ear_params, 1.5, 0.15, 1, 0.,
                         test_mod_output.modulation, ref_mod_output.modulation,
                         level_output.spectrally_adapted_test_patterns,
                         level_output.spectrally_adapted_ref_patterns);
  lin_dist = calc_noise_loudness (ear_params,
                                  1.5, 0.15, 1, 0.,
                                  ref_mod_output.modulation,
                                  ref_mod_output.modulation,
                                  level_output.spectrally_adapted_ref_patterns,
                                  ref_ear_output.excitation);

#if 0
  if (peaq->console_output) {
    g_printf ("  Ntot   : %f %f\n"
	      "  ModDiff: %f %f\n"
	      "  NL     : %f\n"
	      "  BW     : %d %d\n"
	      "  NMR    : %f %f\n"
	      "  PD     : %f %f\n"
	      "  EHS    : %f\n",
	      ref_ear_output.overall_loudness,
	      test_ear_output.overall_loudness,
	      mod_diff_1b, mod_diff_2b,
	      noise_loudness,
	      bw_ref, bw_test,
	      nmr, nmr_max,
	      detection_probability, detection_steps,
	      ehs_valid ? 1000 * ehs : -1);
  }
#endif

  if (is_frame_above_threshold (refdata, FB_FRAMESIZE)) {
    if (peaq->current_aggregated_data_fb == NULL) {
      peaq->current_aggregated_data_fb = g_new (GstPeaqAggregatedDataFB, 1);
      peaq->current_aggregated_data_fb->loudness_reached_frame = G_MAXUINT;
      peaq->current_aggregated_data_fb->rms_mod_diff = 0;
      peaq->current_aggregated_data_fb->temp_weight = 0;
      peaq->current_aggregated_data_fb->noise_loud_frame_count = 0;
      peaq->current_aggregated_data_fb->noise_loudness = 0.;
      peaq->current_aggregated_data_fb->missing_components = 0.;
      peaq->current_aggregated_data_fb->lin_dist = 0.;
    }
    if (peaq->saved_aggregated_data_fb != NULL) {
      g_free (peaq->saved_aggregated_data_fb);
      peaq->saved_aggregated_data_fb = NULL;
    }
  } else {
    if (peaq->saved_aggregated_data_fb == NULL) {
      peaq->saved_aggregated_data_fb =
        g_memdup (peaq->current_aggregated_data_fb,
                  sizeof (GstPeaqAggregatedDataFB));
    }
  }

  if (peaq->current_aggregated_data != NULL) {
    if (peaq->current_aggregated_data_fb->loudness_reached_frame == G_MAXUINT &&
        ref_ear_output.overall_loudness > 0.1 &&
	test_ear_output.overall_loudness > 0.1)
      peaq->current_aggregated_data_fb->loudness_reached_frame =
        peaq->frame_counter_fb;
    if (peaq->frame_counter_fb >= 125) {
      peaq->current_aggregated_data_fb->rms_mod_diff +=
        temp_wt * temp_wt * mod_diff_a * mod_diff_a;
      peaq->current_aggregated_data_fb->temp_weight += temp_wt * temp_wt;
      if (peaq->frame_counter_fb >=
          peaq->current_aggregated_data_fb->loudness_reached_frame + 13) {
	peaq->current_aggregated_data_fb->noise_loud_frame_count++;
	peaq->current_aggregated_data_fb->noise_loudness +=
	  noise_loudness * noise_loudness;
	peaq->current_aggregated_data_fb->missing_components +=
	  missing_components * missing_components;
	peaq->current_aggregated_data_fb->lin_dist += lin_dist;
      }
    }
  }
  peaq->frame_counter_fb++;
}
#endif

static void
calc_modulation_difference (PeaqEarModelParams const *params,
                            ModulationProcessorOutput const *ref_mod_output,
                            ModulationProcessorOutput const *test_mod_output,
                            gdouble *mod_diff_1b, gdouble *mod_diff_2b,
                            gdouble *temp_wt)
{
  guint i;
  guint band_count = peaq_earmodelparams_get_band_count (params);
  *mod_diff_1b = 0.;
  *mod_diff_2b = 0.;
  *temp_wt = 0.;
  for (i = 0; i < band_count; i++) {
    gdouble w;
    gdouble diff = ABS (test_mod_output->modulation[i] -
			ref_mod_output->modulation[i]);
    *mod_diff_1b += diff / (1 + ref_mod_output->modulation[i]);
    w =
      test_mod_output->modulation[i] >= ref_mod_output->modulation[i] ? 1 : .1;
    *mod_diff_2b += w * diff / (0.01 + ref_mod_output->modulation[i]);
    *temp_wt += ref_mod_output->average_loudness[i] /
      (ref_mod_output->average_loudness[i] + 100 *
       pow (peaq_earmodelparams_get_internal_noise (params, i), 0.3));
  }
  *mod_diff_1b *= 100. / band_count;
  *mod_diff_2b *= 100. / band_count;
}

#ifdef ADVANCED
static void
calc_modulation_difference_A (PeaqEarModelParams const *params,
                              ModulationProcessorOutput const *ref_mod_output,
                              ModulationProcessorOutput const *test_mod_output,
                              gdouble *mod_diff_a, gdouble *temp_wt)
{
  guint i;
  guint band_count = peaq_earmodelparams_get_band_count (params);
  *mod_diff_a = 0.;
  *temp_wt = 0.;
  for (i = 0; i < band_count; i++) {
    gdouble diff = ABS (test_mod_output->modulation[i] -
			ref_mod_output->modulation[i]);
    *mod_diff_a += diff / (1 + ref_mod_output->modulation[i]);
    *temp_wt += ref_mod_output->average_loudness[i] /
      (ref_mod_output->average_loudness[i] +
       pow (peaq_earmodelparams_get_internal_noise (params, i), 0.3));
  }
  *mod_diff_a *= 100. / band_count;
}
#endif

static gdouble
calc_noise_loudness (PeaqEarModelParams const *params,
                     gdouble alpha, gdouble thres_fac, gdouble S0,
                     gdouble NLmin,
                     gdouble const *ref_modulation,
                     gdouble const *test_modulation,
                     gdouble const *ref_excitation,
                     gdouble const *test_excitation)
{
  guint i;
  gdouble noise_loudness = 0.;
  guint band_count = peaq_earmodelparams_get_band_count (params);
  for (i = 0; i < band_count; i++) {
    gdouble sref = thres_fac * ref_modulation[i] + S0;
    gdouble stest = thres_fac * test_modulation[i] + S0;
    gdouble ethres = peaq_earmodelparams_get_internal_noise (params, i);
    gdouble ep_ref = ref_excitation[i];
    gdouble ep_test = test_excitation[i];
    gdouble beta = exp (-alpha * (ep_test - ep_ref) / ep_ref);
    noise_loudness += pow (1. / stest * ethres, 0.23) *
      (pow (1 + MAX (stest * ep_test - sref * ep_ref, 0) /
	    (ethres + sref * ep_ref * beta), 0.23) - 1.);
  }
  noise_loudness *= 24. / band_count;
  if (noise_loudness < NLmin)
    noise_loudness = 0.;
  return noise_loudness;
}

static void
calc_bandwidth (gdouble const *ref_power_spectrum,
                gdouble const *test_power_spectrum,
                guint *bw_test, guint *bw_ref)
{
  guint i;
  gdouble zero_threshold = test_power_spectrum[921];
  for (i = 922; i < 1024; i++)
    if (test_power_spectrum[i] > zero_threshold)
      zero_threshold = test_power_spectrum[i];
  *bw_ref = 0;
  for (i = 921; i > 0; i--)
    if (ref_power_spectrum[i - 1] > 10 * zero_threshold) {
      *bw_ref = i;
      break;
    }
  *bw_test = 0;
  for (i = *bw_ref; i > 0; i--)
    if (test_power_spectrum[i - 1] >
	3.16227766016838 * zero_threshold) {
      *bw_test = i;
      break;
    }
}

static void
calc_nmr (GstPeaq const *peaq, gdouble const *ref_excitation,
          gdouble *noise_in_bands, gdouble *nmr, gdouble *nmr_max)
{
  guint i, band_count;
  band_count
    = peaq_earmodelparams_get_band_count (peaq_earmodel_get_model_params
                                          (PEAQ_EARMODEL(peaq->ref_ear)));
  *nmr = 0.;
  *nmr_max = 0.;
  for (i = 0; i < band_count; i++) {
    gdouble mask = ref_excitation[i] /
      peaq->masking_difference[i];
    gdouble curr_nmr = noise_in_bands[i] / mask;
    *nmr += curr_nmr;
    if (curr_nmr > *nmr_max)
      *nmr_max = curr_nmr;
  }
  *nmr /= band_count;
}

static void
calc_prob_detect (guint band_count, gdouble const *ref_excitation,
                  gdouble const *test_excitation,
                  gdouble *detection_probability, gdouble *detection_steps)
{
  guint i;
  *detection_probability = 1.;
  *detection_steps = 0.;
  for (i = 0; i < band_count; i++) {
    gdouble eref_db = 10 * log10 (ref_excitation[i]);
    gdouble etest_db = 10 * log10 (test_excitation[i]);
    gdouble l = 0.3 * MAX (eref_db, etest_db) + 0.7 * etest_db;
    gdouble s = l > 0 ? 5.95072 * pow (6.39468 / l, 1.71332) +
      9.01033e-11 * pow (l, 4) + 5.05622e-6 * pow (l, 3) -
      0.00102438 * l * l + 0.0550197 * l - 0.198719 : 1e30;
    gdouble e = eref_db - etest_db;
    gdouble b = eref_db > etest_db ? 4 : 6;
    gdouble pc = 1 - pow (0.5, pow (e / s, b));
    gdouble qc = fabs (trunc(e)) / s;
    *detection_probability *= 1 - pc;
    *detection_steps += qc;
  }
  *detection_probability = 1 - *detection_probability;
}

static void
do_xcorr(GstPeaq const * peaq, gdouble const* d, gdouble * c)
{
  /*
   * the follwing uses an equivalent computation in the frequency domain to
   * determine the correlation like function:
   * for (i = 0; i < MAXLAG; i++) {
   *   c[i] = 0;
   *   for (k = 0; k < MAXLAG; k++)
   *     c[i] += d[k] * d[k + i];
   * }
  */
  guint k;
  gdouble timedata[2 * MAXLAG];
  GstFFTF64Complex freqdata1[MAXLAG + 1];
  GstFFTF64Complex freqdata2[MAXLAG + 1];
  memcpy (timedata, d, 2 * MAXLAG * sizeof(gdouble));
  gst_fft_f64_fft (peaq->correlator_fft, timedata, freqdata1);
  memset (timedata + MAXLAG, 0, MAXLAG * sizeof(gdouble));
  gst_fft_f64_fft (peaq->correlator_fft, timedata, freqdata2);
  for (k = 0; k < MAXLAG + 1; k++) {
    /* multiply freqdata1 with the conjugate of freqdata2 */
    gdouble r = (freqdata1[k].r * freqdata2[k].r
                 + freqdata1[k].i * freqdata2[k].i) / (2 * MAXLAG);
    gdouble i = (freqdata2[k].r * freqdata1[k].i
                 - freqdata1[k].r * freqdata2[k].i) / (2 * MAXLAG);
    freqdata1[k].r = r;
    freqdata1[k].i = i;
  }
  gst_fft_f64_inverse_fft (peaq->correlator_inverse_fft, freqdata1, timedata);
  memcpy (c, timedata, MAXLAG * sizeof(gdouble));
}

static void
calc_ehs (GstPeaq const *peaq, gfloat const *refdata, gfloat const *testdata,
          gdouble const *ref_power_spectrum, gdouble const *test_power_spectrum,
          gboolean *ehs_valid, gdouble *ehs)
{
  guint i;
  gdouble energy = 0.;
  *ehs_valid = FALSE;
  GstPeaqClass *peaq_class = GST_PEAQ_GET_CLASS (peaq);
  for (i = FFT_FRAMESIZE / 2; i < FFT_FRAMESIZE; i++)
    energy += refdata[i] * refdata[i];
  if (energy >= EHS_ENERGY_THRESHOLD)
    *ehs_valid = TRUE;
  else {
    energy = 0.;
    for (i = FFT_FRAMESIZE / 2; i < FFT_FRAMESIZE; i++)
      energy += testdata[i] * testdata[i];
    if (energy >= EHS_ENERGY_THRESHOLD)
      *ehs_valid = TRUE;
  }
  if (*ehs_valid) {
    gdouble d[FFT_FRAMESIZE / 2 + 1];
    gdouble c[MAXLAG];
    gdouble d0;
    gdouble dk;
    gdouble cavg;
    GstFFTF64Complex c_fft[MAXLAG / 2 + 1];
    gdouble s;
    for (i = 0; i < FFT_FRAMESIZE / 2 + 1; i++) {
      gdouble fref = ref_power_spectrum[i];
      gdouble ftest = test_power_spectrum[i];
      if (fref > 0)
	d[i] = log (ftest / fref);
      else
	d[i] = 0.;
    }

    do_xcorr(peaq, d, c);

    /* in the following, the mean is subtracted before the window is applied as
     * suggested by [Kabal03], although this contradicts [BS1387]; however, the
     * results thus obtained are closer to the reference */
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
    gst_fft_f64_fft (peaq->correlation_fft, c, c_fft);
    *ehs = 0.;
    s = c_fft[0].r * c_fft[0].r + c_fft[0].i * c_fft[0].i;
    for (i = 1; i < MAXLAG / 2 + 1; i++) {
      gdouble new_s = c_fft[i].r * c_fft[i].r + c_fft[i].i * c_fft[i].i;
      if (new_s > s && new_s > *ehs)
	*ehs = new_s;
      s = new_s;
    }
  }
}

#ifndef ADVANCED
static double
gst_peaq_calculate_di (GstPeaq * peaq)
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
      /* according to [Kabal03], it is unclear whether the MOVs should be
       * clipped to within [amin, amax], although [BS1387] does not mention
       * clipping at all; doing so slightly improves the results of the
       * conformance test */
#if 1
      if (m < 0.)
        m = 0.;
      if (m > 1.)
        m = 1.;
#endif
      for (j = 0; j < 3; j++)
	x[j] += wx[i][j] * m;
    }
    distortion_index = -0.307594;
    for (i = 0; i < 3; i++)
      distortion_index += wy[i] / (1 + exp (-(wxb[i] + x[i])));

    if (peaq->console_output) {
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
		"  RelDistFramesB: %f\n",
		bw_ref_b, bw_test_b, total_nmr_b, win_mod_diff1_b, adb_b,
		ehs_b, avg_mod_diff1_b, avg_mod_diff2_b, rms_noise_loud_b,
		mfpd_b, rel_dist_frames_b);
    }
    return distortion_index;
  }
  return 0;
}

#else

static double
gst_peaq_calculate_di_advanced (GstPeaq *peaq)
{
  GstPeaqAggregatedData *agg_data;
  GstPeaqAggregatedDataFB *agg_data_fb;

  if (peaq->saved_aggregated_data_fb)
    agg_data_fb = peaq->saved_aggregated_data_fb;
  else
    agg_data_fb = peaq->current_aggregated_data_fb;

  if (peaq->saved_aggregated_data)
    agg_data = peaq->saved_aggregated_data;
  else
    agg_data = peaq->current_aggregated_data;

  if (agg_data && agg_data_fb) {
    guint i;
    gdouble x[5];
    gdouble movs[5];

    guint band_count
      = peaq_earmodelparams_get_band_count (peaq_earmodel_get_model_params
                                            (PEAQ_EARMODEL(peaq->ref_ear_fb)));
    gdouble rms_mod_diff_a = sqrt(band_count * agg_data_fb->rms_mod_diff /
                                  agg_data_fb->temp_weight);
    gdouble rms_noise_loud_a =
      sqrt (agg_data_fb->noise_loudness / agg_data_fb->noise_loud_frame_count);
    gdouble rms_missing_components_a =
      sqrt (agg_data_fb->missing_components /
            agg_data_fb->noise_loud_frame_count);
    gdouble rms_noise_loud_asym_a =
      rms_noise_loud_a + 0.5 * rms_missing_components_a;
    gdouble avg_lin_dist_a =
      agg_data_fb->lin_dist / agg_data_fb->noise_loud_frame_count;

    gdouble distortion_index;
    gdouble segmental_nmr_b = agg_data->nmr_local / agg_data->frame_count;
    gdouble ehs_b = 1000 * agg_data->ehs / agg_data->ehs_frame_count;
    movs[0] = rms_mod_diff_a;
    movs[1] = rms_noise_loud_asym_a;
    movs[2] = segmental_nmr_b;
    movs[3] = ehs_b;
    movs[4] = avg_lin_dist_a;
    for (i = 0; i < 5; i++)
      x[i] = 0;
    for (i = 0; i <= 4; i++) {
      guint j;
      gdouble m =
        (movs[i] - amin_advanced[i]) / (amax_advanced[i] - amin_advanced[i]);
      /* according to [Kabal03], it is unclear whether the MOVs should be
       * clipped to within [amin, amax], although [BS1387] does not mention
       * clipping at all; doing so slightly improves the results of the
       * conformance test */
#if 1
      if (m < 0.)
        m = 0.;
      if (m > 1.)
        m = 1.;
#endif
      for (j = 0; j < 5; j++)
	x[j] += wx_advanced[i][j] * m;
    }
    distortion_index = -1.360308;
    for (i = 0; i < 5; i++)
      distortion_index +=
        wy_advanced[i] / (1 + exp (-(wxb_advanced[i] + x[i])));

    if (peaq->console_output) {
      g_printf("RmsNoiseLoudAsymA = %f\n"
               "RmsModDiffA = %f\n"
               "SegmentalNMRB = %f\n"
               "EHSB = %f\n"
               "AvgLinDistA = %f\n",
               rms_noise_loud_asym_a,
               rms_mod_diff_a,
               segmental_nmr_b,
               ehs_b,
               avg_lin_dist_a);
    }
    return distortion_index;
  }
  return 0;
}
#endif

static double
gst_peaq_calculate_odg (GstPeaq * peaq)
{
  if (peaq->saved_aggregated_data || peaq->current_aggregated_data) {
#ifdef ADVANCED
    gdouble distortion_index = gst_peaq_calculate_di_advanced (peaq);
#else
    gdouble distortion_index = gst_peaq_calculate_di (peaq);
#endif
    gdouble odg = -3.98 + 4.2 / (1 + exp (-distortion_index));
    if (peaq->console_output) {
      g_printf ("Objective Difference Grade: %.3f\n", odg);
    }
    return odg;
  }
  return 0;
}

static gboolean
is_frame_above_threshold (gfloat *framedata, guint framesize)
{
  gfloat sum;
  guint i;

  sum = 0;
  for (i = 0; i < 5; i++)
    sum += fabs (framedata[i]);
  while (i < framesize) {
    sum += fabs (framedata[i]) - fabs (framedata[i - 5]);
    if (sum >= 200. / 32768)
      return TRUE;
    i++;
  }
  return FALSE;
}
