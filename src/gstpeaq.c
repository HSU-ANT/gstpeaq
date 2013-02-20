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

enum
{
  PROP_0,
  PROP_PLAYBACK_LEVEL,
  PROP_MODE_ADVANCED,
  PROP_DI,
  PROP_ODG,
  PROP_TOTALSNR,
  PROP_CONSOLE_OUTPUT
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
		    "channels = (int) { 1, 2 }, " \
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
static void gst_peaq_set_property (GObject * obj, guint id,
				   const GValue * value, GParamSpec * pspec);
static GstCaps *get_caps (GstPad *pad);
static gboolean set_caps (GstPad *pad, GstCaps *caps);
static GstFlowReturn pads_buffer (GstCollectPads2 *pads, GstCollectData2 *data,
                                  GstBuffer *buffer, gpointer user_data);
static GstStateChangeReturn gst_peaq_change_state (GstElement * element,
						   GstStateChange transition);
static void gst_peaq_process_fft_block_basic (GstPeaq *peaq, gfloat *refdata,
                                              gfloat *testdata);
static void gst_peaq_process_fft_block_advanced (GstPeaq *peaq, gfloat *refdata,
                                                 gfloat *testdata);
static void gst_peaq_process_fb_block (GstPeaq *peaq, gfloat *refdata,
                                       gfloat *testdata);
static void calc_modulation_difference(PeaqEarModelParams const *params,
                                       ModulationProcessorOutput const *ref_mod_output,
                                       ModulationProcessorOutput const *test_mod_output,
                                       gdouble *mod_diff_1b,
                                       gdouble *mod_diff_2b,
                                       gdouble *temp_wt);
static void calc_modulation_difference_A (PeaqEarModelParams const *params,
                                          ModulationProcessorOutput const *ref_mod_output,
                                          ModulationProcessorOutput const *test_mod_output,
                                          gdouble *mod_diff_a, gdouble *temp_wt);
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
static double gst_peaq_calculate_di (GstPeaq * peaq);
static double gst_peaq_calculate_di_advanced (GstPeaq *peaq);
static double gst_peaq_calculate_odg (GstPeaq * peaq);
static gboolean is_frame_above_threshold (gfloat *framedata, guint framesize,
                                          guint channels);

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
				   PROP_MODE_ADVANCED,
				   g_param_spec_boolean ("advanced",
                                                         "Advanced mode enabled",
                                                         "True if advanced mode is used",
                                                         FALSE,
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
  guint i;
  GstPadTemplate *template;
  PeaqEarModelParams *model_params;

  peaq->collect = gst_collect_pads2_new ();
  gst_collect_pads2_set_buffer_function (peaq->collect, pads_buffer, peaq);

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
  gst_collect_pads2_add_pad (peaq->collect, peaq->refpad,
                             sizeof (GstCollectData2));
  gst_pad_set_setcaps_function (peaq->refpad, set_caps);
  gst_pad_set_getcaps_function (peaq->refpad, get_caps);
  gst_element_add_pad (GST_ELEMENT (peaq), peaq->refpad);

  template = gst_static_pad_template_get (&gst_peaq_test_template);
  peaq->testpad = gst_pad_new_from_template (template, "test");
  gst_object_unref (template);
  gst_collect_pads2_add_pad (peaq->collect, peaq->testpad,
                             sizeof (GstCollectData2));
  gst_pad_set_setcaps_function (peaq->testpad, set_caps);
  gst_pad_set_getcaps_function (peaq->testpad, get_caps);
  gst_element_add_pad (GST_ELEMENT (peaq), peaq->testpad);

  GST_OBJECT_FLAG_SET (peaq, GST_ELEMENT_IS_SINK);

  peaq->frame_counter = 0;
  peaq->frame_counter_fb = 0;
  peaq->loudness_reached_frame = G_MAXUINT;
  peaq->total_signal_energy = 0.;
  peaq->total_noise_energy = 0.;

  peaq->ref_ear[0] = g_object_new (PEAQ_TYPE_FFTEARMODEL, NULL);
  peaq->ref_ear[1] = g_object_new (PEAQ_TYPE_FFTEARMODEL, NULL);
  peaq->test_ear[0] = g_object_new (PEAQ_TYPE_FFTEARMODEL, NULL);
  peaq->test_ear[1] = g_object_new (PEAQ_TYPE_FFTEARMODEL, NULL);
  model_params =
    peaq_earmodel_get_model_params (PEAQ_EARMODEL (peaq->ref_ear[0]));

  peaq->ref_ear_fb[0] = NULL;
  peaq->ref_ear_fb[1] = NULL;
  peaq->test_ear_fb[0] = NULL;
  peaq->test_ear_fb[1] = NULL;
  peaq->masking_difference = NULL;
  peaq->level_adapter[0] = peaq_leveladapter_new (model_params);
  peaq->level_adapter[1] = peaq_leveladapter_new (model_params);
  peaq->ref_modulation_processor[0] =
    peaq_modulationprocessor_new (model_params);
  peaq->ref_modulation_processor[1] =
    peaq_modulationprocessor_new (model_params);
  peaq->test_modulation_processor[0] =
    peaq_modulationprocessor_new (model_params);
  peaq->test_modulation_processor[1] =
    peaq_modulationprocessor_new (model_params);
  for (i = 0; i < COUNT_MOV_BASIC; i++)
    peaq->mov_accum[i] = peaq_movaccum_new ();
}

static void
gst_peaq_finalize (GObject * object)
{
  guint i;
  GstPeaq *peaq = GST_PEAQ (object);
  g_object_unref (peaq->collect);
  g_object_unref (peaq->ref_adapter_fft);
  g_object_unref (peaq->test_adapter_fft);
  g_object_unref (peaq->ref_adapter_fb);
  g_object_unref (peaq->test_adapter_fb);
  g_object_unref (peaq->ref_ear[0]);
  g_object_unref (peaq->ref_ear[1]);
  g_object_unref (peaq->test_ear[0]);
  g_object_unref (peaq->test_ear[1]);
  g_object_unref (peaq->level_adapter[0]);
  g_object_unref (peaq->level_adapter[1]);
  g_object_unref (peaq->ref_modulation_processor[0]);
  g_object_unref (peaq->ref_modulation_processor[1]);
  g_object_unref (peaq->test_modulation_processor[0]);
  g_object_unref (peaq->test_modulation_processor[1]);
  gst_fft_f64_free (peaq->correlation_fft);
  gst_fft_f64_free (peaq->correlator_fft);
  gst_fft_f64_free (peaq->correlator_inverse_fft);
  for (i = 0; i < COUNT_MOV_BASIC; i++)
    g_object_unref (peaq->mov_accum[i]);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_peaq_get_property (GObject * obj, guint id, GValue * value,
		       GParamSpec * pspec)
{
  GstPeaq *peaq = GST_PEAQ (obj);
  switch (id) {
    case PROP_PLAYBACK_LEVEL:
      g_object_get_property (G_OBJECT (peaq_earmodel_get_model_params(PEAQ_EARMODEL(peaq->ref_ear[0]))),
			     "playback_level", value);
      break;
    case PROP_DI:
      if (peaq->advanced)
        g_value_set_double (value, gst_peaq_calculate_di_advanced (peaq));
      else
        g_value_set_double (value, gst_peaq_calculate_di (peaq));
      break;
    case PROP_ODG:
      g_value_set_double (value, gst_peaq_calculate_odg (peaq));
      break;
    case PROP_TOTALSNR:
      {
        gdouble snr = peaq->total_signal_energy / peaq->total_noise_energy;
        g_value_set_double (value, 10 * log10 (snr));
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
      g_object_set_property (G_OBJECT (peaq_earmodel_get_model_params(PEAQ_EARMODEL(peaq->ref_ear[0]))),
			     "playback_level", value);
      g_object_set_property (G_OBJECT (peaq_earmodel_get_model_params(PEAQ_EARMODEL(peaq->test_ear[0]))),
			     "playback_level", value);
      g_object_set_property (G_OBJECT (peaq_earmodel_get_model_params(PEAQ_EARMODEL(peaq->ref_ear[1]))),
			     "playback_level", value);
      g_object_set_property (G_OBJECT (peaq_earmodel_get_model_params(PEAQ_EARMODEL(peaq->test_ear[1]))),
			     "playback_level", value);
      // TODO: also set playback level for filterbank ears
      break;
    case PROP_MODE_ADVANCED:
      {
        guint band_count;
        guint i;
        gdouble deltaZ;
        PeaqEarModelParams *model_params;
        peaq->advanced = g_value_get_boolean (value);
        if (peaq->advanced) {
          band_count = 55;
          deltaZ = 0.5;
        } else {
          band_count = 109;
          deltaZ = 0.25;
        }
        g_object_set (peaq->ref_ear[0], "number-of-bands", band_count, NULL);
        g_object_set (peaq->ref_ear[1], "number-of-bands", band_count, NULL);
        g_object_set (peaq->test_ear[0], "number-of-bands", band_count, NULL);
        g_object_set (peaq->test_ear[1], "number-of-bands", band_count, NULL);
        if (peaq->advanced) {
          if (peaq->ref_ear_fb[0] == NULL)
            peaq->ref_ear_fb[0] = g_object_new (PEAQ_TYPE_FILTERBANKEARMODEL,
                                                NULL);
          if (peaq->ref_ear_fb[1] == NULL)
            peaq->ref_ear_fb[1] = g_object_new (PEAQ_TYPE_FILTERBANKEARMODEL,
                                                NULL);
          if (peaq->test_ear_fb[0] == NULL)
            peaq->test_ear_fb[0] = g_object_new (PEAQ_TYPE_FILTERBANKEARMODEL,
                                                 NULL);
          if (peaq->test_ear_fb[1] == NULL)
            peaq->test_ear_fb[1] = g_object_new (PEAQ_TYPE_FILTERBANKEARMODEL,
                                                 NULL);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVADV_RMS_MOD_DIFF],
                                  MODE_RMS);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVADV_SEGMENTAL_NMR],
                                  MODE_AVG);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVADV_EHS], MODE_AVG);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVADV_AVG_LIN_DIST],
                                  MODE_AVG);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVADVEXTRA_NOISE_LOUD],
                                  MODE_RMS);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVADVEXTRA_MISSING_COMPONENTS],
                                  MODE_RMS);
          model_params =
            peaq_earmodel_get_model_params (PEAQ_EARMODEL(peaq->ref_ear_fb[0]));
        } else {
          if (peaq->ref_ear_fb[0]) {
            g_object_unref (peaq->ref_ear_fb[0]);
            peaq->ref_ear_fb[0] = NULL;
          }
          if (peaq->ref_ear_fb[1]) {
            g_object_unref (peaq->ref_ear_fb[1]);
            peaq->ref_ear_fb[1] = NULL;
          }
          if (peaq->test_ear_fb[0]) {
            g_object_unref (peaq->test_ear_fb[0]);
            peaq->test_ear_fb[0] = NULL;
          }
          if (peaq->test_ear_fb[1]) {
            g_object_unref (peaq->test_ear_fb[1]);
            peaq->test_ear_fb[1] = NULL;
          }
          peaq_movaccum_set_mode (peaq->mov_accum[MOVBASIC_BANDWIDTH_REF],
                                  MODE_AVG);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVBASIC_BANDWIDTH_TEST],
                                  MODE_AVG);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVBASIC_TOTAL_NMR],
                                  MODE_AVG_LOG);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVBASIC_WIN_MOD_DIFF],
                                  MODE_AVG_WINDOW);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVBASIC_ADB], MODE_ADB);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVBASIC_EHS], MODE_AVG);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVBASIC_AVG_MOD_DIFF_1],
                                  MODE_AVG);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVBASIC_AVG_MOD_DIFF_2],
                                  MODE_AVG);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVBASIC_RMS_NOISE_LOUD],
                                  MODE_RMS);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVBASIC_MFPD],
                                  MODE_FILTERED_MAX);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVBASIC_REL_DIST_FRAMES],
                                  MODE_AVG);
          model_params =
            peaq_earmodel_get_model_params (PEAQ_EARMODEL(peaq->ref_ear[0]));
        }
        if (peaq->masking_difference)
          g_free (peaq->masking_difference);
        peaq->masking_difference = g_new (gdouble, band_count);
        for (i = 0; i < band_count; i++)
          peaq->masking_difference[i] =
            pow (10, (i * deltaZ <= 12 ? 3 : deltaZ * i * deltaZ) / 10);
        peaq_leveladapter_set_ear_model_params (peaq->level_adapter[0],
                                                model_params);
        peaq_leveladapter_set_ear_model_params (peaq->level_adapter[1],
                                                model_params);
        peaq_modulationprocessor_set_ear_model_params (peaq->ref_modulation_processor[0],
                                                       model_params);
        peaq_modulationprocessor_set_ear_model_params (peaq->ref_modulation_processor[1],
                                                       model_params);
        peaq_modulationprocessor_set_ear_model_params (peaq->test_modulation_processor[0],
                                                       model_params);
        peaq_modulationprocessor_set_ear_model_params (peaq->test_modulation_processor[1],
                                                       model_params);
      }
      break;
    case PROP_CONSOLE_OUTPUT:
      peaq->console_output = g_value_get_boolean (value);
      break;
  }
}

static GstCaps *
get_caps (GstPad *pad)
{
  GstPeaq *peaq = GST_PEAQ (gst_pad_get_parent_element (pad));
  GstCaps *caps;
  GstCaps *mycaps;
  GstCaps *peercaps;
  if (pad == peaq->refpad) {
    mycaps = gst_static_pad_template_get_caps (&gst_peaq_ref_template);
    peercaps = gst_pad_peer_get_caps_reffed (peaq->testpad);
  } else {
    mycaps = gst_static_pad_template_get_caps (&gst_peaq_test_template);
    peercaps = gst_pad_peer_get_caps_reffed (peaq->refpad);
  }
  if (peercaps) {
    caps = gst_caps_intersect (mycaps, peercaps);
    gst_caps_unref (peercaps);
    gst_caps_unref (mycaps);
  } else {
    caps = mycaps;
  }
  return caps;
}

static gboolean
set_caps (GstPad *pad, GstCaps *caps)
{
  GstPeaq *peaq = GST_PEAQ (gst_pad_get_parent_element (pad));
  gst_structure_get_int (gst_caps_get_structure (caps, 0),
                         "channels", &(peaq->channels));
  return TRUE;
}


static GstFlowReturn
pads_buffer (GstCollectPads2 *pads, GstCollectData2 *data, GstBuffer *buffer,
             gpointer user_data)
{
  GstPeaq *peaq;

  peaq = GST_PEAQ (user_data);
  GstElement *element = GST_ELEMENT (user_data);

  if (element->pending_state != GST_STATE_VOID_PENDING) {
    guint i;

    element->current_state = element->pending_state;
    element->pending_state = GST_STATE_VOID_PENDING;

    for (i = 0; i < COUNT_MOV_BASIC; i++)
      peaq_movaccum_set_channels (peaq->mov_accum[i], peaq->channels);
  }

  if (buffer == NULL) {
    gst_element_post_message (GST_ELEMENT_CAST (peaq),
                              gst_message_new_eos (GST_OBJECT_CAST (peaq)));
    return GST_FLOW_OK;
  }

  if (buffer->caps != NULL) {
    gint channels;
    gst_structure_get_int (gst_caps_get_structure (buffer->caps, 0),
                           "channels", &channels);
    if (channels != peaq->channels || peaq->channels == 0) {
      return GST_FLOW_NOT_NEGOTIATED;
    }
  }
  if (data->pad == peaq->refpad) {
    if (peaq->advanced)
      gst_adapter_push (peaq->ref_adapter_fb, gst_buffer_copy (buffer));
    gst_adapter_push (peaq->ref_adapter_fft, buffer);
  } else if (data->pad == peaq->testpad) {
    if (peaq->advanced)
      gst_adapter_push (peaq->test_adapter_fb, gst_buffer_copy (buffer));
    gst_adapter_push (peaq->test_adapter_fft, buffer);
  }

  while (gst_adapter_available (peaq->ref_adapter_fft) >=
         peaq->channels * FFT_BLOCKSIZE_BYTES &&
         gst_adapter_available (peaq->test_adapter_fft) >=
         peaq->channels * FFT_BLOCKSIZE_BYTES)
  {
    gfloat *refframe =
      (gfloat *) gst_adapter_peek (peaq->ref_adapter_fft,
                                   peaq->channels * FFT_BLOCKSIZE_BYTES);
    gfloat *testframe =
      (gfloat *) gst_adapter_peek (peaq->test_adapter_fft,
                                   peaq->channels * FFT_BLOCKSIZE_BYTES);
    if (peaq->advanced)
      gst_peaq_process_fft_block_advanced (peaq, refframe, testframe);
    else
      gst_peaq_process_fft_block_basic (peaq, refframe, testframe);
    g_assert (gst_adapter_available (peaq->ref_adapter_fft) >=
              peaq->channels * FFT_BLOCKSIZE_BYTES);
    gst_adapter_flush (peaq->ref_adapter_fft,
                       peaq->channels * FFT_STEPSIZE_BYTES);
    g_assert (gst_adapter_available (peaq->test_adapter_fft) >=
              peaq->channels * FFT_BLOCKSIZE_BYTES);
    gst_adapter_flush (peaq->test_adapter_fft,
                       peaq->channels * FFT_STEPSIZE_BYTES);
  }

  if (peaq->advanced) {
    while (gst_adapter_available (peaq->ref_adapter_fb) >=
           peaq->channels * FB_BLOCKSIZE_BYTES &&
           gst_adapter_available (peaq->test_adapter_fb) >=
           peaq->channels * FB_BLOCKSIZE_BYTES)
    {
      gfloat *refframe =
        (gfloat *) gst_adapter_peek (peaq->ref_adapter_fb,
                                     peaq->channels * FB_BLOCKSIZE_BYTES);
      gfloat *testframe = 
        (gfloat *) gst_adapter_peek (peaq->test_adapter_fb,
                                     peaq->channels * FB_BLOCKSIZE_BYTES);
      gst_peaq_process_fb_block (peaq, refframe, testframe);
      g_assert (gst_adapter_available (peaq->ref_adapter_fb) >=
                peaq->channels * FB_BLOCKSIZE_BYTES);
      gst_adapter_flush (peaq->ref_adapter_fb,
                         peaq->channels * FB_BLOCKSIZE_BYTES);
      g_assert (gst_adapter_available (peaq->test_adapter_fb) >=
                peaq->channels * FB_BLOCKSIZE_BYTES);
      gst_adapter_flush (peaq->test_adapter_fb,
                         peaq->channels * FB_BLOCKSIZE_BYTES);
    }
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
      gst_collect_pads2_start (peaq->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* need to unblock the collectpads before calling the
       * parent change_state so that streaming can finish */
      gst_collect_pads2_stop (peaq->collect);

      // TODO: do something similar for the filter bank adapters
      ref_data_left_count = gst_adapter_available (peaq->ref_adapter_fft);
      test_data_left_count = gst_adapter_available (peaq->test_adapter_fft);
      if (ref_data_left_count || test_data_left_count) {
        gfloat *padded_ref_frame =
          g_newa (gfloat, peaq->channels * FFT_FRAMESIZE);
        gfloat *padded_test_frame =
          g_newa (gfloat, peaq->channels * FFT_FRAMESIZE);
        guint ref_data_count =
          MIN (ref_data_left_count, peaq->channels * FFT_BLOCKSIZE_BYTES);
        guint test_data_count =
          MIN (test_data_left_count, peaq->channels * FFT_BLOCKSIZE_BYTES);
        gfloat *refframe = (gfloat *) gst_adapter_peek (peaq->ref_adapter_fft,
							ref_data_count);
        gfloat *testframe = (gfloat *) gst_adapter_peek (peaq->test_adapter_fft,
							 test_data_count);
	g_memmove (padded_ref_frame, refframe, ref_data_count);
	memset (((char *) padded_ref_frame) + ref_data_count, 0,
                peaq->channels * FFT_BLOCKSIZE_BYTES - ref_data_count);
	g_memmove (padded_test_frame, testframe, test_data_count);
	memset (((char *) padded_test_frame) + test_data_count, 0,
                peaq->channels * FFT_BLOCKSIZE_BYTES - test_data_count);
        if (peaq->advanced)
          gst_peaq_process_fft_block_advanced (peaq, padded_ref_frame,
                                               padded_test_frame);
        else
          gst_peaq_process_fft_block_basic (peaq, padded_ref_frame,
                                            padded_test_frame);
	g_assert (gst_adapter_available (peaq->ref_adapter_fft) >= 
		  ref_data_count);
	gst_adapter_flush (peaq->ref_adapter_fft, ref_data_count);
	g_assert (gst_adapter_available (peaq->test_adapter_fft) >= 
		  test_data_count);
	gst_adapter_flush (peaq->test_adapter_fft, test_data_count);
      }

      if (peaq->advanced) {
        ref_data_left_count = gst_adapter_available (peaq->ref_adapter_fb);
        test_data_left_count = gst_adapter_available (peaq->test_adapter_fb);
        if (ref_data_left_count || test_data_left_count) {
          gfloat *padded_ref_frame =
            g_newa (gfloat, peaq->channels * FB_FRAMESIZE);
          gfloat *padded_test_frame =
            g_newa (gfloat, peaq->channels * FB_FRAMESIZE);
          guint ref_data_count =
            MIN (ref_data_left_count, peaq->channels * FB_BLOCKSIZE_BYTES);
          guint test_data_count =
            MIN (test_data_left_count, peaq->channels * FB_BLOCKSIZE_BYTES);
          gfloat *refframe = (gfloat *) gst_adapter_peek (peaq->ref_adapter_fb,
                                                          ref_data_count);
          gfloat *testframe = (gfloat *) gst_adapter_peek (peaq->test_adapter_fb,
                                                           test_data_count);
          g_memmove (padded_ref_frame, refframe, ref_data_count);
          memset (((char *) padded_ref_frame) + ref_data_count, 0,
                  peaq->channels * FB_BLOCKSIZE_BYTES - ref_data_count);
          g_memmove (padded_test_frame, testframe, test_data_count);
          memset (((char *) padded_test_frame) + test_data_count, 0,
                  peaq->channels * FB_BLOCKSIZE_BYTES - test_data_count);
          gst_peaq_process_fb_block (peaq, padded_ref_frame, padded_test_frame);
          g_assert (gst_adapter_available (peaq->ref_adapter_fb) >= 
                    ref_data_count);
          gst_adapter_flush (peaq->ref_adapter_fb, ref_data_count);
          g_assert (gst_adapter_available (peaq->test_adapter_fb) >= 
                    test_data_count);
          gst_adapter_flush (peaq->test_adapter_fb, test_data_count);
        }
      }

      gst_peaq_calculate_odg (peaq);
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state (element, transition) ==
      GST_STATE_CHANGE_FAILURE) {
    return GST_STATE_CHANGE_FAILURE;
  }

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      return GST_STATE_CHANGE_ASYNC;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      if (peaq->channels == 0)
        return GST_STATE_CHANGE_ASYNC;
    default:
      break;
  }

  return GST_STATE_CHANGE_SUCCESS;
}

static void
gst_peaq_process_fft_block_basic (GstPeaq *peaq, gfloat *refdata,
                                  gfloat *testdata)
{
  guint i;
  guint c;
  gint channels = peaq->channels;
  gboolean ehs_valid_any = FALSE;
  gdouble *detection_probability[2];
  gdouble *detection_steps[2];
  gdouble ehs[2] = { 0., };

  PeaqEarModelParams *ear_params =
    peaq_earmodel_get_model_params(PEAQ_EARMODEL(peaq->ref_ear[0]));
  PeaqFFTEarModelParams *fft_ear_params =
    peaq_fftearmodel_get_fftmodel_params(peaq->ref_ear[0]);
  guint band_count = peaq_earmodelparams_get_band_count (ear_params);

  gboolean above_thres = is_frame_above_threshold (refdata, FFT_FRAMESIZE,
                                                   channels);

  for (i = 0; i < COUNT_MOV_BASIC; i++)
    peaq_movaccum_set_tentative (peaq->mov_accum[i], !above_thres);

  for (c = 0; c < channels; c++) {
    FFTEarModelOutput ref_ear_output;
    FFTEarModelOutput test_ear_output;
    LevelAdapterOutput level_output;
    ModulationProcessorOutput ref_mod_output;
    ModulationProcessorOutput test_mod_output;
    gdouble *noise_in_bands;

    gdouble mod_diff_1b;
    gdouble mod_diff_2b;
    gdouble temp_wt;
    gdouble noise_loudness;
    guint bw_ref;
    guint bw_test;
    gdouble nmr;
    gdouble nmr_max;
    gboolean ehs_valid;

    noise_in_bands = g_newa (gdouble, band_count);
    ref_ear_output.ear_model_output.unsmeared_excitation =
      g_newa (gdouble, band_count);
    ref_ear_output.ear_model_output.excitation =
      g_newa (gdouble, band_count);
    test_ear_output.ear_model_output.unsmeared_excitation =
      g_newa (gdouble, band_count);
    test_ear_output.ear_model_output.excitation =
      g_newa (gdouble, band_count);

    gfloat *refdata_c;
    gfloat *testdata_c;
    if (channels != 1) {
      refdata_c = g_newa (gfloat, FFT_BLOCKSIZE_BYTES);
      testdata_c = g_newa (gfloat, FFT_BLOCKSIZE_BYTES);
      for (i = 0; i < FFT_FRAMESIZE; i++) {
        refdata_c[i] = refdata[channels * i +c];
        testdata_c[i] = testdata[channels * i +c];
      }
    } else {
      refdata_c = refdata;
      testdata_c = testdata;
    }
    peaq_fftearmodel_process (peaq->ref_ear[c], refdata_c, &ref_ear_output);
    peaq_fftearmodel_process (peaq->test_ear[c], testdata_c, &test_ear_output);

    gdouble noise_spectrum[FFT_FRAMESIZE / 2 + 1];
    for (i = 0; i < FFT_FRAMESIZE / 2 + 1; i++)
      noise_spectrum[i] =
        ref_ear_output.weighted_power_spectrum[i] -
        2 * sqrt (ref_ear_output.weighted_power_spectrum[i] *
                  test_ear_output.weighted_power_spectrum[i]) +
        test_ear_output.weighted_power_spectrum[i];

    peaq_fftearmodelparams_group_into_bands (fft_ear_params, noise_spectrum,
                                             noise_in_bands);

    level_output.spectrally_adapted_ref_patterns =
      g_newa (gdouble, band_count);
    level_output.spectrally_adapted_test_patterns =
      g_newa (gdouble, band_count);
    peaq_leveladapter_process (peaq->level_adapter[c],
                               ref_ear_output.ear_model_output.excitation,
                               test_ear_output.ear_model_output.excitation,
                               &level_output);

    ref_mod_output.modulation = g_newa (gdouble, band_count);
    peaq_modulationprocessor_process (peaq->ref_modulation_processor[c],
                                      ref_ear_output.ear_model_output.unsmeared_excitation,
                                      &ref_mod_output);
    test_mod_output.modulation = g_newa (gdouble, band_count);
    peaq_modulationprocessor_process (peaq->test_modulation_processor[c],
                                      test_ear_output.ear_model_output.unsmeared_excitation,
                                      &test_mod_output);

    /* modulation difference */
    if (peaq->frame_counter >= 24) {
      calc_modulation_difference (ear_params, &ref_mod_output,
                                  &test_mod_output, &mod_diff_1b,
                                  &mod_diff_2b, &temp_wt);
      peaq_movaccum_accumulate_weighted (peaq->mov_accum[MOVBASIC_AVG_MOD_DIFF_1],
                                         c, mod_diff_1b, temp_wt);
      peaq_movaccum_accumulate_weighted (peaq->mov_accum[MOVBASIC_AVG_MOD_DIFF_2],
                                         c, mod_diff_2b, temp_wt);
      peaq_movaccum_accumulate (peaq->mov_accum[MOVBASIC_WIN_MOD_DIFF], c,
                                mod_diff_1b);
    }

    /* noise loudness */
    if (peaq->loudness_reached_frame == G_MAXUINT &&
        ref_ear_output.ear_model_output.overall_loudness > 0.1 &&
        test_ear_output.ear_model_output.overall_loudness > 0.1)
      peaq->loudness_reached_frame = peaq->frame_counter;
    if (peaq->frame_counter >= 24 &&
        peaq->frame_counter - 3 >= peaq->loudness_reached_frame) {
      noise_loudness =
        calc_noise_loudness (ear_params,
                             1.5, 0.15, 0.5, 0.,
                             ref_mod_output.modulation,
                             test_mod_output.modulation,
                             level_output.spectrally_adapted_ref_patterns,
                             level_output.spectrally_adapted_test_patterns);
      peaq_movaccum_accumulate (peaq->mov_accum[MOVBASIC_RMS_NOISE_LOUD], c,
                                noise_loudness);
    }


    /* bandwidth */
    calc_bandwidth (ref_ear_output.power_spectrum,
                    test_ear_output.power_spectrum, &bw_test, &bw_ref);
    if (bw_ref > 346) {
      peaq_movaccum_accumulate (peaq->mov_accum[MOVBASIC_BANDWIDTH_REF], c,
                                bw_ref);
      peaq_movaccum_accumulate (peaq->mov_accum[MOVBASIC_BANDWIDTH_TEST], c,
                                bw_test);
    }

    /* noise-to-mask ratio */
    calc_nmr (peaq, ref_ear_output.ear_model_output.excitation,
              noise_in_bands, &nmr, &nmr_max);
    peaq_movaccum_accumulate (peaq->mov_accum[MOVBASIC_TOTAL_NMR], c, nmr);
    peaq_movaccum_accumulate (peaq->mov_accum[MOVBASIC_REL_DIST_FRAMES], c,
                              nmr_max > 1.41253754462275 ? 1. : 0.);

    /* probability of detection */
    detection_probability[c] = g_newa (gdouble, band_count);
    detection_steps[c] = g_newa (gdouble, band_count);
    calc_prob_detect(band_count, ref_ear_output.ear_model_output.excitation,
                     test_ear_output.ear_model_output.excitation,
                     detection_probability[c], detection_steps[c]);

    /* error harmonic structure */
    calc_ehs (peaq, refdata_c, testdata_c, ref_ear_output.power_spectrum,
              test_ear_output.power_spectrum, &ehs_valid, &ehs[c]);
    if (ehs_valid)
      ehs_valid_any = TRUE;

    if (peaq->console_output) {
      g_printf ("  Ntot   : %f %f\n"
                "  ModDiff: %f %f\n"
                "  NL     : %f\n"
                "  BW     : %d %d\n"
                "  NMR    : %f %f\n"
                //"  PD     : %f %f\n"
                "  EHS    : %f\n",
                ref_ear_output.ear_model_output.overall_loudness,
                test_ear_output.ear_model_output.overall_loudness,
                mod_diff_1b, mod_diff_2b,
                noise_loudness,
                bw_ref, bw_test,
                nmr, nmr_max,
                //detection_probability, detection_steps,
                ehs_valid ? 1000 * ehs[c] : -1);
    }
  }

  if (ehs_valid_any) {
    for (c = 0; c < channels; c++)
      peaq_movaccum_accumulate (peaq->mov_accum[MOVBASIC_EHS], c, ehs[c]);
  }

  gdouble binaural_detection_probability = 1.;
  gdouble binaural_detection_steps = 0;
  for (i = 0; i < band_count; i++) {
    gdouble pbin = detection_probability[0][i];
    gdouble qbin = detection_steps[0][i];
    for (c = 1; c < channels; c++) {
      if (detection_probability[c][i] > pbin)
        pbin = detection_probability[c][i];
      if (detection_steps[c][i] > qbin)
        qbin = detection_steps[c][i];
    }
    binaural_detection_probability *= 1. - pbin;
    binaural_detection_steps += qbin;
  }
  binaural_detection_probability = 1. - binaural_detection_probability;
  if (binaural_detection_probability > 0.5) {
    peaq_movaccum_accumulate (peaq->mov_accum[MOVBASIC_ADB], 0,
                              binaural_detection_steps);
  }
  peaq_movaccum_accumulate (peaq->mov_accum[MOVBASIC_MFPD], 0,
                            binaural_detection_probability);

  for (i = 0; i < channels * FFT_FRAMESIZE / 2; i++) {
    peaq->total_signal_energy
      += refdata[i] * refdata[i];
    peaq->total_noise_energy 
      += (refdata[i] - testdata[i]) * (refdata[i] - testdata[i]);
  }

  peaq->frame_counter++;
}

static void
gst_peaq_process_fft_block_advanced (GstPeaq *peaq, gfloat *refdata,
                                     gfloat *testdata)
{
  guint i, c;
  gint channels = peaq->channels;
  gboolean ehs_valid_any = FALSE;
  gdouble ehs[2] = {0., };

  PeaqEarModelParams *ear_params =
    peaq_earmodel_get_model_params(PEAQ_EARMODEL(peaq->ref_ear[0]));
  PeaqFFTEarModelParams *fft_ear_params =
    peaq_fftearmodel_get_fftmodel_params(peaq->ref_ear[0]);
  guint band_count = peaq_earmodelparams_get_band_count (ear_params);

  gboolean above_thres = is_frame_above_threshold (refdata, FFT_FRAMESIZE,
                                                   channels);

  peaq_movaccum_set_tentative (peaq->mov_accum[MOVADV_SEGMENTAL_NMR],
                               !above_thres);
  peaq_movaccum_set_tentative (peaq->mov_accum[MOVADV_EHS], !above_thres);

  for (c = 0; c < channels; c++) {
    FFTEarModelOutput ref_ear_output;
    FFTEarModelOutput test_ear_output;
    gdouble *noise_in_bands;
    gdouble nmr;
    gdouble nmr_max;
    gboolean ehs_valid;

    noise_in_bands = g_newa (gdouble, band_count);
    ref_ear_output.ear_model_output.unsmeared_excitation =
      g_newa (gdouble, band_count);
    ref_ear_output.ear_model_output.excitation =
      g_newa (gdouble, band_count);
    test_ear_output.ear_model_output.unsmeared_excitation =
      g_newa (gdouble, band_count);
    test_ear_output.ear_model_output.excitation =
      g_newa (gdouble, band_count);

    gfloat *refdata_c;
    gfloat *testdata_c;
    if (channels != 1) {
      refdata_c = g_newa (gfloat, FFT_BLOCKSIZE_BYTES);
      testdata_c = g_newa (gfloat, FFT_BLOCKSIZE_BYTES);
      for (i = 0; i < FFT_FRAMESIZE; i++) {
        refdata_c[i] = refdata[channels * i +c];
        testdata_c[i] = testdata[channels * i +c];
      }
    } else {
      refdata_c = refdata;
      testdata_c = testdata;
    }
    peaq_fftearmodel_process (peaq->ref_ear[c], refdata_c, &ref_ear_output);
    peaq_fftearmodel_process (peaq->test_ear[c], testdata_c, &test_ear_output);

    gdouble noise_spectrum[FFT_FRAMESIZE / 2 + 1];
    for (i = 0; i < FFT_FRAMESIZE / 2 + 1; i++)
      noise_spectrum[i] =
        ref_ear_output.weighted_power_spectrum[i] -
        2 * sqrt (ref_ear_output.weighted_power_spectrum[i] *
                  test_ear_output.weighted_power_spectrum[i]) +
        test_ear_output.weighted_power_spectrum[i];
    peaq_fftearmodelparams_group_into_bands (fft_ear_params, noise_spectrum,
                                             noise_in_bands);

    /* noise-to-mask ratio */
    calc_nmr (peaq, ref_ear_output.ear_model_output.excitation,
              noise_in_bands, &nmr, &nmr_max);
    peaq_movaccum_accumulate (peaq->mov_accum[MOVADV_SEGMENTAL_NMR], c,
                              10. * log10 (nmr));

    /* error harmonic structure */
    calc_ehs (peaq, refdata_c, testdata_c, ref_ear_output.power_spectrum,
              test_ear_output.power_spectrum, &ehs_valid, &ehs[c]);
    if (ehs_valid)
      ehs_valid_any = TRUE;

    if (peaq->console_output) {
      g_printf ("  NMR    : %f\n"
                "  EHS    : %f\n",
                nmr,
                ehs_valid ? 1000 * ehs[c] : -1);
    }
  }

  if (ehs_valid_any) {
    for (c = 0; c < channels; c++)
      peaq_movaccum_accumulate (peaq->mov_accum[MOVADV_EHS], c, ehs[c]);
  }

  for (i = 0; i < channels * FFT_FRAMESIZE / 2; i++) {
    peaq->total_signal_energy += refdata[i] * refdata[i];
    peaq->total_noise_energy 
      += (refdata[i] - testdata[i]) * (refdata[i] - testdata[i]);
  }

  peaq->frame_counter++;
}

static void
gst_peaq_process_fb_block (GstPeaq *peaq, gfloat *refdata, gfloat *testdata)
{
  guint c;
  gint channels = peaq->channels;
  PeaqEarModelParams *ear_params =
    peaq_earmodel_get_model_params(PEAQ_EARMODEL(peaq->ref_ear_fb[0]));
  guint band_count = peaq_earmodelparams_get_band_count (ear_params);

  gboolean above_thres = is_frame_above_threshold (refdata, FB_FRAMESIZE,
                                                   channels);

  peaq_movaccum_set_tentative (peaq->mov_accum[MOVADV_RMS_MOD_DIFF],
                               !above_thres);
  peaq_movaccum_set_tentative (peaq->mov_accum[MOVADVEXTRA_NOISE_LOUD],
                               !above_thres);
  peaq_movaccum_set_tentative (peaq->mov_accum[MOVADVEXTRA_MISSING_COMPONENTS],
                               !above_thres);
  peaq_movaccum_set_tentative (peaq->mov_accum[MOVADV_AVG_LIN_DIST],
                               !above_thres);

  for (c = 0; c < channels; c++) {
    guint i;
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

    ref_ear_output.unsmeared_excitation = g_newa (gdouble, band_count);
    ref_ear_output.excitation = g_newa (gdouble, band_count);
    test_ear_output.unsmeared_excitation = g_newa (gdouble, band_count);
    test_ear_output.excitation = g_newa (gdouble, band_count);
    gfloat *refdata_c;
    gfloat *testdata_c;
    if (channels != 1) {
      refdata_c = g_newa (gfloat, FB_BLOCKSIZE_BYTES);
      testdata_c = g_newa (gfloat, FB_BLOCKSIZE_BYTES);
      for (i = 0; i < FB_FRAMESIZE; i++) {
        refdata_c[i] = refdata[channels * i + c];
        testdata_c[i] = testdata[channels * i + c];
      }
    } else {
      refdata_c = refdata;
      testdata_c = testdata;
    }
    peaq_filterbankearmodel_process (peaq->ref_ear_fb[c], refdata_c,
                                     &ref_ear_output);
    peaq_filterbankearmodel_process (peaq->test_ear_fb[c], testdata_c,
                                     &test_ear_output);

    level_output.spectrally_adapted_ref_patterns =
      g_newa (gdouble, band_count);
    level_output.spectrally_adapted_test_patterns =
      g_newa (gdouble, band_count);
    peaq_leveladapter_process (peaq->level_adapter[c],
                               ref_ear_output.excitation,
                               test_ear_output.excitation, &level_output);

    ref_mod_output.modulation = g_newa (gdouble, band_count);
    peaq_modulationprocessor_process (peaq->ref_modulation_processor[c],
                                      ref_ear_output.unsmeared_excitation,
                                      &ref_mod_output);
    test_mod_output.modulation = g_newa (gdouble, band_count);
    peaq_modulationprocessor_process (peaq->test_modulation_processor[c],
                                      test_ear_output.unsmeared_excitation,
                                      &test_mod_output);

    /* modulation difference */
    calc_modulation_difference_A (ear_params, &ref_mod_output,
                                  &test_mod_output, &mod_diff_a, &temp_wt);
    if (peaq->frame_counter_fb >= 125) {
      peaq_movaccum_accumulate_weighted (peaq->mov_accum[MOVADV_RMS_MOD_DIFF],
                                         c, mod_diff_a, temp_wt);
    }

    /* noise loudness */
    if (peaq->loudness_reached_frame == G_MAXUINT) {
      if (ref_ear_output.overall_loudness > 0.1 &&
          test_ear_output.overall_loudness > 0.1)
        peaq->loudness_reached_frame = peaq->frame_counter_fb;
    }
    noise_loudness =
      calc_noise_loudness (ear_params, 2.5, 0.3, 1, 0.1,
                           ref_mod_output.modulation,
                           test_mod_output.modulation,
                           level_output.spectrally_adapted_ref_patterns,
                           level_output.spectrally_adapted_test_patterns);
    /* TODO: should the modulation patterns really also be swapped? */
    missing_components =
      calc_noise_loudness (ear_params, 1.5, 0.15, 1, 0.,
                           test_mod_output.modulation,
                           ref_mod_output.modulation,
                           level_output.spectrally_adapted_test_patterns,
                           level_output.spectrally_adapted_ref_patterns);
    lin_dist =
      calc_noise_loudness (ear_params,
                           1.5, 0.15, 1, 0.,
                           ref_mod_output.modulation,
                           ref_mod_output.modulation,
                           level_output.spectrally_adapted_ref_patterns,
                           ref_ear_output.excitation);
    if (peaq->frame_counter_fb >= 125 &&
        peaq->frame_counter_fb - 13 >= peaq->loudness_reached_frame) {
      peaq_movaccum_accumulate (peaq->mov_accum[MOVADVEXTRA_NOISE_LOUD], c,
                                noise_loudness);
      peaq_movaccum_accumulate (peaq->mov_accum[MOVADVEXTRA_MISSING_COMPONENTS],
                                c, missing_components);
      peaq_movaccum_accumulate (peaq->mov_accum[MOVADV_AVG_LIN_DIST], c,
                                lin_dist);
    }

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
  }

  peaq->frame_counter_fb++;
}

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
                                          (PEAQ_EARMODEL(peaq->ref_ear[0])));
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
    detection_probability[i] = pc;
    detection_steps[i] = qc;
  }
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

static double
gst_peaq_calculate_di (GstPeaq * peaq)
{
  guint i;
  gdouble movs[11];
  gdouble x[3];
  gdouble distortion_index;
  for (i = 0; i < COUNT_MOV_BASIC; i++)
    movs[i] = peaq_movaccum_get_value (peaq->mov_accum[i]);
  movs[MOVBASIC_EHS] *= 1000.;

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
              movs[0], movs[1], movs[2], movs[3], movs[4], movs[5], movs[6],
              movs[7], movs[8], movs[9], movs[10]);
  }
  return distortion_index;
}

static double
gst_peaq_calculate_di_advanced (GstPeaq *peaq)
{
  guint i;
  gdouble x[5];
  gdouble movs[5];

  guint band_count
    = peaq_earmodelparams_get_band_count (peaq_earmodel_get_model_params
                                          (PEAQ_EARMODEL(peaq->ref_ear_fb[0])));
  gdouble distortion_index;
  movs[0] = sqrt (band_count) *
    peaq_movaccum_get_value (peaq->mov_accum[MOVADV_RMS_MOD_DIFF]);
  movs[1] =
    peaq_movaccum_get_value (peaq->mov_accum[MOVADVEXTRA_NOISE_LOUD]) +
    0.5 * peaq_movaccum_get_value (peaq->mov_accum[MOVADVEXTRA_MISSING_COMPONENTS]);
  movs[2] = peaq_movaccum_get_value (peaq->mov_accum[MOVADV_SEGMENTAL_NMR]);
  movs[3] = 1000. * peaq_movaccum_get_value (peaq->mov_accum[MOVADV_EHS]);
  movs[4] = peaq_movaccum_get_value (peaq->mov_accum[MOVADV_AVG_LIN_DIST]);
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
    g_printf("RmsModDiffA = %f\n"
             "RmsNoiseLoudAsymA = %f\n"
             "SegmentalNMRB = %f\n"
             "EHSB = %f\n"
             "AvgLinDistA = %f\n",
             movs[0],
             movs[1],
             movs[2],
             movs[3],
             movs[4]);
  }
  return distortion_index;
}

static double
gst_peaq_calculate_odg (GstPeaq * peaq)
{
  gdouble distortion_index;
  if (peaq->advanced)
    distortion_index = gst_peaq_calculate_di_advanced (peaq);
  else
    distortion_index = gst_peaq_calculate_di (peaq);
  gdouble odg = -3.98 + 4.2 / (1 + exp (-distortion_index));
  if (peaq->console_output) {
    g_printf ("Objective Difference Grade: %.3f\n", odg);
  }
  return odg;
}

static gboolean
is_frame_above_threshold (gfloat *framedata, guint framesize, guint channels)
{
  gfloat sum;
  guint i, c;

  for (c = 0; c < channels; c++) {
    sum = 0;
    for (i = 0; i < 5; i++)
      sum += fabs (framedata[channels * i + c]);
    while (i < framesize) {
      sum += fabs (framedata[channels * i + c]) -
        fabs (framedata[channels * (i - 5) + c]);
      if (sum >= 200. / 32768)
        return TRUE;
      i++;
    }
  }
  return FALSE;
}
