/* GstPEAQ
 * Copyright (C) 2006, 2007, 2010, 2011, 2012, 2013, 2014
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
#include <gst/base/gstadapter.h>
#include <gst/fft/gstfftf64.h>
#include <gst/gst.h>
#include <math.h>
#include <string.h>

#include "gstpeaq.h"
#include "fbearmodel.h"
#include "fftearmodel.h"
#include "leveladapter.h"
#include "modpatt.h"
#include "movaccum.h"
#include "movs.h"

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

enum _MovAdvanced {
  MOVADV_RMS_MOD_DIFF,
  MOVADV_RMS_NOISE_LOUD_ASYM,
  MOVADV_SEGMENTAL_NMR,
  MOVADV_EHS,
  MOVADV_AVG_LIN_DIST,
  COUNT_MOV_ADVANCED
};

enum _MovBasic {
  MOVBASIC_BANDWIDTH_REF,
  MOVBASIC_BANDWIDTH_TEST,
  MOVBASIC_TOTAL_NMR,
  MOVBASIC_WIN_MOD_DIFF,
  MOVBASIC_ADB,
  MOVBASIC_EHS,
  MOVBASIC_AVG_MOD_DIFF_1,
  MOVBASIC_AVG_MOD_DIFF_2,
  MOVBASIC_RMS_NOISE_LOUD,
  MOVBASIC_MFPD,
  MOVBASIC_REL_DIST_FRAMES,
  COUNT_MOV_BASIC
};

struct _GstPeaq
{
  GstElement element;
  GstPad *refpad;
  GstPad *testpad;
  gboolean ref_eos;
  gboolean test_eos;
  GstAdapter *ref_adapter_fft;
  GstAdapter *test_adapter_fft;
  GstAdapter *ref_adapter_fb;
  GstAdapter *test_adapter_fb;
  GstFFTF64 *correlation_fft;
  GstFFTF64 *correlator_fft;
  GstFFTF64 *correlator_inverse_fft;
  gboolean console_output;
  gboolean advanced;
  gint channels;
  guint frame_counter;
  guint frame_counter_fb;
  guint loudness_reached_frame;
  PeaqEarModel *fft_ear_model;
  gpointer ref_fft_ear_state[2];
  gpointer test_fft_ear_state[2];
  PeaqEarModel *fb_ear_model;
  gpointer ref_fb_ear_state[2];
  gpointer test_fb_ear_state[2];
  PeaqLevelAdapter *level_adapter[2];
  PeaqModulationProcessor *ref_modulation_processor[2];
  PeaqModulationProcessor *test_modulation_processor[2];
  PeaqMovAccum *mov_accum[COUNT_MOV_BASIC];
  gdouble total_signal_energy;
  gdouble total_noise_energy;
};

struct _GstPeaqClass
{
  GstElementClass parent_class;
  gdouble *correlation_window;
};

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

static const gdouble amin_advanced[5] = {
  13.298751, 0.041073, -25.018791, 0.061560, 0.02452
};

static const gdouble amax_advanced[5] = {
  2166.5, 13.24326, 13.46708, 10.226771, 14.224874
};

static const gdouble wx_advanced[5][5] = {
  {21.211773, -39.013052, -1.382553, -14.545348, -0.320899},
  {-8.981803, 19.956049, 0.935389, -1.686586, -3.238586},
  {1.633830, -2.877505, -7.442935, 5.606502, -1.783120},
  {6.103821, 19.587435, -0.240284, 1.088213, -0.511314},
  {11.556344, 3.892028, 9.720441, -3.287205, -11.031250},
};

static const double wxb_advanced[5] = { 1.330890, 2.686103, 2.096598, -1.327851, 3.087055 };

static const double wy_advanced[5] = { -4.696996, -3.289959, 7.004782, 6.651897, 4.009144 };

static const gdouble amin[] = {
  393.916656, 361.965332, -24.045116, 1.110661, -0.206623, 0.074318, 1.113683,
  0.950345, 0.029985, 0.000101, 0
};

static const gdouble amax[] = {
  921, 881.131226, 16.212030, 107.137772, 2.886017, 13.933351, 63.257874,
  1145.018555, 14.819740, 1, 1
};

static const gdouble wx[11][3] = {
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

static const double wxb[] = { -2.518254, 0.654841, -2.207228 };
static const double wy[] = { -3.817048, 4.107138, 4.629582, -0.307594 };

static void base_init (gpointer g_class);
static void class_init (gpointer g_class, gpointer class_data);
static void init (GTypeInstance *obj, gpointer g_class);
static void finalize (GObject * object);
static void get_property (GObject *obj, guint id, GValue *value,
                          GParamSpec *pspec);
static void set_property (GObject *obj, guint id, const GValue *value,
                          GParamSpec *pspec);
static GstCaps *get_caps (GstPad *pad);
static gboolean set_caps (GstPad *pad, GstCaps *caps);
static GstFlowReturn pad_chain (GstPad *pad, GstBuffer *buffer);
static gboolean pad_event (GstPad *pad, GstEvent *event);
static gboolean query (GstElement *element, GstQuery *query);
static GstStateChangeReturn change_state (GstElement * element,
                                          GstStateChange transition);
static gboolean send_event (GstElement *element, GstEvent *event);
static void process_fft_block_basic (GstPeaq *peaq, gfloat *refdata,
                                     gfloat *testdata);
static void process_fft_block_advanced (GstPeaq *peaq, gfloat *refdata,
                                        gfloat *testdata);
static void process_fb_block (GstPeaq *peaq, gfloat *refdata, gfloat *testdata);
static void calc_ehs (GstPeaq const *peaq, gpointer *ref_state,
                      gpointer *test_state, PeaqMovAccum *mov_accum);
static double calculate_di (GstPeaq * peaq);
static double calculate_di_advanced (GstPeaq *peaq);
static double calculate_odg (GstPeaq * peaq);
static gboolean is_frame_above_threshold (gfloat *framedata, guint framesize,
                                          guint channels);

GType
gst_peaq_get_type (void)
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (GstPeaqClass),
      base_init,
      NULL,                     /* base_finalize */
      class_init,
      NULL,                     /* class_finalize */
      NULL,                     /* class_data */
      sizeof (GstPeaq),
      0,                        /* n_preallocs */
      init
    };
    type = g_type_register_static (GST_TYPE_ELEMENT, "GstPeaq", &info, 0);
  }
  return type;
}

static gboolean
query (GstElement *element, GstQuery *query)
{
  GstElementClass *parent_class = 
    GST_ELEMENT_CLASS (g_type_class_peek_parent (g_type_class_peek
                                                 (GST_TYPE_PEAQ)));
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
base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GObjectClass *gobject_class = G_OBJECT_CLASS (g_class);

  GstPadTemplate *pad_template =
    gst_static_pad_template_get (&gst_peaq_ref_template);
  gst_element_class_add_pad_template (element_class, pad_template);

  pad_template = gst_static_pad_template_get (&gst_peaq_test_template);
  gst_element_class_add_pad_template (element_class, pad_template);

  gst_element_class_set_details_simple (element_class,
                                        "Perceptual evaluation of audio quality",
                                        "Sink/Audio",
                                        "Compute objective audio quality measures",
                                        "Martin Holters <martin.holters@hsuhh.de>");

  element_class->query = query;
  element_class->change_state = change_state;
  element_class->send_event = send_event;

  gobject_class->finalize = finalize;
}

static void
class_init (gpointer g_class, gpointer class_data)
{
  guint i;
  GObjectClass *object_class = G_OBJECT_CLASS (g_class);
  GstPeaqClass *peaq_class = GST_PEAQ_CLASS (g_class);

  /* centering the window of the correlation in the EHS computation at lag zero
   * (as considered in [Kabal03] to be more reasonable) degrades conformance */
  peaq_class->correlation_window = g_new (gdouble, MAXLAG);
  for (i = 0; i < MAXLAG; i++)
#if 0
    peaq_class->correlation_window[i] = 0.81649658092773 *
      (1 + cos (2 * M_PI * i / (2 * MAXLAG - 1))) / MAXLAG;
#else
    peaq_class->correlation_window[i] = 0.81649658092773 *
      (1 - cos (2 * M_PI * i / (MAXLAG - 1))) / MAXLAG;
#endif

  object_class->get_property = get_property;
  object_class->set_property = set_property;
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
init (GTypeInstance *obj, gpointer g_class)
{
  guint i;
  GstPadTemplate *template;

  GstPeaq *peaq = GST_PEAQ (obj);

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
  gst_pad_set_chain_function (peaq->refpad, pad_chain);
  gst_pad_set_event_function (peaq->refpad, pad_event);
  gst_pad_set_setcaps_function (peaq->refpad, set_caps);
  gst_pad_set_getcaps_function (peaq->refpad, get_caps);
  gst_element_add_pad (GST_ELEMENT (peaq), peaq->refpad);

  template = gst_static_pad_template_get (&gst_peaq_test_template);
  peaq->testpad = gst_pad_new_from_template (template, "test");
  gst_object_unref (template);
  gst_pad_set_chain_function (peaq->testpad, pad_chain);
  gst_pad_set_event_function (peaq->testpad, pad_event);
  gst_pad_set_setcaps_function (peaq->testpad, set_caps);
  gst_pad_set_getcaps_function (peaq->testpad, get_caps);
  gst_element_add_pad (GST_ELEMENT (peaq), peaq->testpad);

  GST_OBJECT_FLAG_SET (peaq, GST_ELEMENT_IS_SINK);

  peaq->frame_counter = 0;
  peaq->frame_counter_fb = 0;
  peaq->loudness_reached_frame = G_MAXUINT;
  peaq->total_signal_energy = 0.;
  peaq->total_noise_energy = 0.;

  peaq->fft_ear_model = g_object_new (PEAQ_TYPE_FFTEARMODEL, NULL);
  peaq->fb_ear_model = NULL;

  peaq->level_adapter[0] = peaq_leveladapter_new (peaq->fft_ear_model);
  peaq->level_adapter[1] = peaq_leveladapter_new (peaq->fft_ear_model);
  peaq->ref_modulation_processor[0] =
    peaq_modulationprocessor_new (peaq->fft_ear_model);
  peaq->ref_modulation_processor[1] =
    peaq_modulationprocessor_new (peaq->fft_ear_model);
  peaq->test_modulation_processor[0] =
    peaq_modulationprocessor_new (peaq->fft_ear_model);
  peaq->test_modulation_processor[1] =
    peaq_modulationprocessor_new (peaq->fft_ear_model);
  for (i = 0; i < COUNT_MOV_BASIC; i++)
    peaq->mov_accum[i] = peaq_movaccum_new ();
}

static void
finalize (GObject * object)
{
  guint i;
  GstElementClass *parent_class = 
    GST_ELEMENT_CLASS (g_type_class_peek_parent (g_type_class_peek
                                                 (GST_TYPE_PEAQ)));
  GstPeaq *peaq = GST_PEAQ (object);
  g_object_unref (peaq->ref_adapter_fft);
  g_object_unref (peaq->test_adapter_fft);
  g_object_unref (peaq->ref_adapter_fb);
  g_object_unref (peaq->test_adapter_fb);
  g_object_unref (peaq->fft_ear_model);
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
get_property (GObject *obj, guint id, GValue *value, GParamSpec *pspec)
{
  GstPeaq *peaq = GST_PEAQ (obj);
  switch (id) {
    case PROP_PLAYBACK_LEVEL:
      g_object_get_property (G_OBJECT (peaq->fft_ear_model),
			     "playback-level", value);
      break;
    case PROP_DI:
      if (peaq->advanced)
        g_value_set_double (value, calculate_di_advanced (peaq));
      else
        g_value_set_double (value, calculate_di (peaq));
      break;
    case PROP_ODG:
      g_value_set_double (value, calculate_odg (peaq));
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
set_property (GObject *obj, guint id, const GValue *value, GParamSpec *pspec)
{
  GstPeaq *peaq = GST_PEAQ (obj);
  switch (id) {
    case PROP_PLAYBACK_LEVEL:
      g_object_set_property (G_OBJECT (peaq->fft_ear_model),
			     "playback-level", value);
      // TODO: also set playback level for filterbank ears
      break;
    case PROP_MODE_ADVANCED:
      {
        guint band_count;
        PeaqEarModel *model;
        peaq->advanced = g_value_get_boolean (value);
        if (peaq->advanced) {
          band_count = 55;
        } else {
          band_count = 109;
        }
        g_object_set (peaq->fft_ear_model, "number-of-bands", band_count, NULL);
        if (peaq->advanced) {
          if (peaq->fb_ear_model == NULL)
            peaq->fb_ear_model = g_object_new (PEAQ_TYPE_FILTERBANKEARMODEL,
                                               NULL);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVADV_RMS_MOD_DIFF],
                                  MODE_RMS);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVADV_SEGMENTAL_NMR],
                                  MODE_AVG);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVADV_EHS], MODE_AVG);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVADV_AVG_LIN_DIST],
                                  MODE_AVG);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVADV_RMS_NOISE_LOUD_ASYM],
                                  MODE_RMS_ASYM);
          model = peaq->fb_ear_model;
        } else {
          if (peaq->fb_ear_model) {
            g_object_unref (peaq->fb_ear_model);
            peaq->fb_ear_model = NULL;
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
          model = peaq->fft_ear_model;
        }
        peaq_leveladapter_set_ear_model (peaq->level_adapter[0], model);
        peaq_leveladapter_set_ear_model (peaq->level_adapter[1], model);
        peaq_modulationprocessor_set_ear_model (peaq->ref_modulation_processor[0],
                                                model);
        peaq_modulationprocessor_set_ear_model (peaq->ref_modulation_processor[1],
                                                model);
        peaq_modulationprocessor_set_ear_model (peaq->test_modulation_processor[0],
                                                model);
        peaq_modulationprocessor_set_ear_model (peaq->test_modulation_processor[1],
                                                model);
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
  gst_object_unref (peaq);
  return caps;
}

static gboolean
set_caps (GstPad *pad, GstCaps *caps)
{
  GstPeaq *peaq = GST_PEAQ (gst_pad_get_parent_element (pad));
  gst_structure_get_int (gst_caps_get_structure (caps, 0),
                         "channels", &(peaq->channels));
  gst_object_unref (peaq);
  return TRUE;
}

static GstFlowReturn
pad_chain (GstPad *pad, GstBuffer *buffer)
{
  GstElement *element = gst_pad_get_parent_element (pad);
  GstPeaq *peaq = GST_PEAQ (element);

  if (buffer->caps != NULL) {
    gint channels;
    gst_structure_get_int (gst_caps_get_structure (buffer->caps, 0),
                           "channels", &channels);
    if (channels != peaq->channels || peaq->channels == 0) {
      return GST_FLOW_NOT_NEGOTIATED;
    }
  }

  GST_OBJECT_LOCK (peaq);

  if (element->pending_state != GST_STATE_VOID_PENDING) {
    guint i;

    element->current_state = element->pending_state;
    element->pending_state = GST_STATE_VOID_PENDING;

    for (i = 0; i < COUNT_MOV_BASIC; i++)
      if (i == MOVBASIC_ADB || i == MOVBASIC_MFPD)
        peaq_movaccum_set_channels (peaq->mov_accum[i], 1);
      else
        peaq_movaccum_set_channels (peaq->mov_accum[i], peaq->channels);
  }

  if (pad == peaq->refpad) {
    peaq->ref_eos = FALSE;
    if (peaq->advanced)
      gst_adapter_push (peaq->ref_adapter_fb, gst_buffer_copy (buffer));
    gst_adapter_push (peaq->ref_adapter_fft, buffer);
  } else if (pad == peaq->testpad) {
    peaq->test_eos = FALSE;
    if (peaq->advanced)
      gst_adapter_push (peaq->test_adapter_fb, gst_buffer_copy (buffer));
    gst_adapter_push (peaq->test_adapter_fft, buffer);
  }
 
  guint required_size =
    peaq->channels * sizeof (gfloat) *
    peaq_earmodel_get_frame_size (peaq->fft_ear_model);
  guint step_size_bytes =
    peaq->channels * sizeof (gfloat) *
    peaq_earmodel_get_step_size (peaq->fft_ear_model);

  while (gst_adapter_available (peaq->ref_adapter_fft) >= required_size &&
         gst_adapter_available (peaq->test_adapter_fft) >= required_size)
  {
    gfloat *refframe =
      (gfloat *) gst_adapter_peek (peaq->ref_adapter_fft,
                                   required_size);
    gfloat *testframe =
      (gfloat *) gst_adapter_peek (peaq->test_adapter_fft,
                                   required_size);
    if (peaq->advanced)
      process_fft_block_advanced (peaq, refframe, testframe);
    else
      process_fft_block_basic (peaq, refframe, testframe);
    g_assert (gst_adapter_available (peaq->ref_adapter_fft) >= required_size);
    gst_adapter_flush (peaq->ref_adapter_fft, step_size_bytes);
    g_assert (gst_adapter_available (peaq->test_adapter_fft) >= required_size);
    gst_adapter_flush (peaq->test_adapter_fft, step_size_bytes);
  }

  if (peaq->advanced) {
    guint required_size =
      peaq->channels * sizeof (gfloat) *
      peaq_earmodel_get_frame_size (peaq->fb_ear_model);
    while (gst_adapter_available (peaq->ref_adapter_fb) >= required_size &&
           gst_adapter_available (peaq->test_adapter_fb) >= required_size)
    {
      gfloat *refframe =
        (gfloat *) gst_adapter_peek (peaq->ref_adapter_fb, required_size);
      gfloat *testframe = 
        (gfloat *) gst_adapter_peek (peaq->test_adapter_fb, required_size);
      process_fb_block (peaq, refframe, testframe);
      g_assert (gst_adapter_available (peaq->ref_adapter_fb) >= required_size);
      gst_adapter_flush (peaq->ref_adapter_fb, required_size);
      g_assert (gst_adapter_available (peaq->test_adapter_fb) >= required_size);
      gst_adapter_flush (peaq->test_adapter_fb, required_size);
    }
  }

  GST_OBJECT_UNLOCK (peaq);

  gst_object_unref (peaq);

  return GST_FLOW_OK;
}

static gboolean
pad_event (GstPad *pad, GstEvent* event)
{
  gboolean ret = FALSE;
  if (event->type == GST_EVENT_EOS) {
    GstElement *element = gst_pad_get_parent_element (pad);
    GstPeaq *peaq = GST_PEAQ (element);

    if (pad == peaq->refpad) {
      peaq->ref_eos = TRUE;
    } else if (pad == peaq->testpad) {
      peaq->test_eos = TRUE;
    }

    if (peaq->ref_eos && peaq->test_eos) {
      gst_element_post_message (GST_ELEMENT_CAST (peaq),
                                gst_message_new_eos (GST_OBJECT_CAST (peaq)));
      ret = TRUE;
    }

    gst_event_unref (event);
    gst_object_unref (peaq);
  } else {
    ret = gst_pad_event_default (pad, event);
  }
  return ret;
}

static GstStateChangeReturn
change_state (GstElement * element, GstStateChange transition)
{
  GstPeaq *peaq;
  guint ref_data_left_count, test_data_left_count;

  GstElementClass *parent_class = 
    GST_ELEMENT_CLASS (g_type_class_peek_parent (g_type_class_peek
                                                 (GST_TYPE_PEAQ)));
  peaq = GST_PEAQ (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      peaq->ref_fft_ear_state[0] = peaq_earmodel_state_alloc (peaq->fft_ear_model);
      peaq->ref_fft_ear_state[1] = peaq_earmodel_state_alloc (peaq->fft_ear_model);
      peaq->test_fft_ear_state[0] = peaq_earmodel_state_alloc (peaq->fft_ear_model);
      peaq->test_fft_ear_state[1] = peaq_earmodel_state_alloc (peaq->fft_ear_model);
      if (peaq->advanced) {
        peaq->ref_fb_ear_state[0] = peaq_earmodel_state_alloc (peaq->fb_ear_model);
        peaq->ref_fb_ear_state[1] = peaq_earmodel_state_alloc (peaq->fb_ear_model);
        peaq->test_fb_ear_state[0] = peaq_earmodel_state_alloc (peaq->fb_ear_model);
        peaq->test_fb_ear_state[1] = peaq_earmodel_state_alloc (peaq->fb_ear_model);
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* need to unblock the collectpads before calling the
       * parent change_state so that streaming can finish */
      ref_data_left_count = gst_adapter_available (peaq->ref_adapter_fft);
      test_data_left_count = gst_adapter_available (peaq->test_adapter_fft);
      if (ref_data_left_count || test_data_left_count) {
        guint frame_size = peaq_earmodel_get_frame_size (peaq->fft_ear_model);
        guint required_size = peaq->channels * sizeof (gfloat) * frame_size;
        gfloat *padded_ref_frame =
          g_newa (gfloat, peaq->channels * frame_size);
        gfloat *padded_test_frame =
          g_newa (gfloat, peaq->channels * frame_size);
        guint ref_data_count =
          MIN (ref_data_left_count, required_size);
        guint test_data_count =
          MIN (test_data_left_count, required_size);
        gfloat *refframe = (gfloat *) gst_adapter_peek (peaq->ref_adapter_fft,
							ref_data_count);
        gfloat *testframe = (gfloat *) gst_adapter_peek (peaq->test_adapter_fft,
							 test_data_count);
	g_memmove (padded_ref_frame, refframe, ref_data_count);
	memset (((char *) padded_ref_frame) + ref_data_count, 0,
                required_size - ref_data_count);
	g_memmove (padded_test_frame, testframe, test_data_count);
	memset (((char *) padded_test_frame) + test_data_count, 0,
                required_size - test_data_count);
        if (peaq->advanced)
          process_fft_block_advanced (peaq, padded_ref_frame,
                                      padded_test_frame);
        else
          process_fft_block_basic (peaq, padded_ref_frame, padded_test_frame);
	g_assert (gst_adapter_available (peaq->ref_adapter_fft) >= 
		  ref_data_count);
	gst_adapter_flush (peaq->ref_adapter_fft, ref_data_count);
	g_assert (gst_adapter_available (peaq->test_adapter_fft) >= 
		  test_data_count);
	gst_adapter_flush (peaq->test_adapter_fft, test_data_count);
      }

      if (peaq->advanced) {
        guint frame_size = peaq_earmodel_get_frame_size (peaq->fb_ear_model);
        guint required_size = peaq->channels * sizeof (gfloat) * frame_size;
        ref_data_left_count = gst_adapter_available (peaq->ref_adapter_fb);
        test_data_left_count = gst_adapter_available (peaq->test_adapter_fb);
        if (ref_data_left_count || test_data_left_count) {
          gfloat *padded_ref_frame =
            g_newa (gfloat, peaq->channels * frame_size);
          gfloat *padded_test_frame =
            g_newa (gfloat, peaq->channels * frame_size);
          guint ref_data_count = MIN (ref_data_left_count, required_size);
          guint test_data_count = MIN (test_data_left_count, required_size);
          gfloat *refframe = (gfloat *) gst_adapter_peek (peaq->ref_adapter_fb,
                                                          ref_data_count);
          gfloat *testframe = (gfloat *) gst_adapter_peek (peaq->test_adapter_fb,
                                                           test_data_count);
          g_memmove (padded_ref_frame, refframe, ref_data_count);
          memset (((char *) padded_ref_frame) + ref_data_count, 0,
                  required_size - ref_data_count);
          g_memmove (padded_test_frame, testframe, test_data_count);
          memset (((char *) padded_test_frame) + test_data_count, 0,
                  required_size - test_data_count);
          process_fb_block (peaq, padded_ref_frame, padded_test_frame);
          g_assert (gst_adapter_available (peaq->ref_adapter_fb) >= 
                    ref_data_count);
          gst_adapter_flush (peaq->ref_adapter_fb, ref_data_count);
          g_assert (gst_adapter_available (peaq->test_adapter_fb) >= 
                    test_data_count);
          gst_adapter_flush (peaq->test_adapter_fb, test_data_count);
        }
      }

      calculate_odg (peaq);

      peaq_earmodel_state_free (peaq->fft_ear_model, peaq->ref_fft_ear_state[0]);
      peaq_earmodel_state_free (peaq->fft_ear_model, peaq->ref_fft_ear_state[1]);
      peaq_earmodel_state_free (peaq->fft_ear_model, peaq->test_fft_ear_state[0]);
      peaq_earmodel_state_free (peaq->fft_ear_model, peaq->test_fft_ear_state[1]);
      if (peaq->advanced) {
        peaq_earmodel_state_free (peaq->fb_ear_model, peaq->ref_fb_ear_state[0]);
        peaq_earmodel_state_free (peaq->fb_ear_model, peaq->ref_fb_ear_state[1]);
        peaq_earmodel_state_free (peaq->fb_ear_model, peaq->test_fb_ear_state[0]);
        peaq_earmodel_state_free (peaq->fb_ear_model, peaq->test_fb_ear_state[1]);
      }
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

static gboolean
send_event (GstElement *element, GstEvent *event)
{
  if (event->type == GST_EVENT_LATENCY) {
    return TRUE;
  } else {
    GstElementClass *parent_class = 
      GST_ELEMENT_CLASS (g_type_class_peek_parent (g_type_class_peek
                                                   (GST_TYPE_PEAQ)));
    return parent_class->send_event (element, event);
  }
}

static void
apply_ear_model (PeaqEarModel *model, guint channels, gfloat *data,
                 gpointer *state)
{
  guint c;
  guint frame_size = peaq_earmodel_get_frame_size (model);
  for (c = 0; c < channels; c++) {
    gfloat *data_c;
    if (channels != 1) {
      guint i;
      data_c = g_newa (gfloat, frame_size);
      for (i = 0; i < frame_size; i++) {
        data_c[i] = data[channels * i +c];
      }
    } else {
      data_c = data;
    }
    peaq_earmodel_process_block (model, state[c], data_c);
  }
}

static void
apply_ear_model_and_preprocess (GstPeaq *peaq, PeaqEarModel *model,
                                gfloat *refdata, gfloat *testdata,
                                gpointer *refstate, gpointer *teststate,
                                guint frame_counter)
{
  guint c;
  gint channels = peaq->channels;
  apply_ear_model (model, channels, refdata, refstate);
  apply_ear_model (model, channels, testdata, teststate);
  for (c = 0; c < channels; c++) {
    gdouble const *ref_excitation =
      peaq_earmodel_get_excitation (model, refstate[c]);
    gdouble const *test_excitation =
      peaq_earmodel_get_excitation (model, teststate[c]);
    gdouble const *ref_unsmeared_excitation =
      peaq_earmodel_get_unsmeared_excitation (model, refstate[c]);
    gdouble const *test_unsmeared_excitation =
      peaq_earmodel_get_unsmeared_excitation (model, teststate[c]);

    peaq_leveladapter_process (peaq->level_adapter[c],
                               ref_excitation, test_excitation);
    peaq_modulationprocessor_process (peaq->ref_modulation_processor[c],
                                      ref_unsmeared_excitation);
    peaq_modulationprocessor_process (peaq->test_modulation_processor[c],
                                      test_unsmeared_excitation);

    if (peaq->loudness_reached_frame == G_MAXUINT) {
      if (peaq_earmodel_calc_loudness (model, refstate[c]) > 0.1 &&
          peaq_earmodel_calc_loudness (model, teststate[c]) > 0.1)
        peaq->loudness_reached_frame = frame_counter;
    }
  }
}

static void
process_fft_block_basic (GstPeaq *peaq, gfloat *refdata, gfloat *testdata)
{
  guint i;
  gint channels = peaq->channels;

  PeaqEarModel *ear_params = peaq->fft_ear_model;
  guint frame_size = peaq_earmodel_get_frame_size (ear_params);

  gboolean above_thres =
    is_frame_above_threshold (refdata, frame_size, channels);

  for (i = 0; i < COUNT_MOV_BASIC; i++)
    peaq_movaccum_set_tentative (peaq->mov_accum[i], !above_thres);

  apply_ear_model_and_preprocess (peaq, peaq->fft_ear_model,
                                  refdata, testdata,
                                  peaq->ref_fft_ear_state,
                                  peaq->test_fft_ear_state,
                                  peaq->frame_counter);

  /* modulation difference */
  if (peaq->frame_counter >= 24) {
    peaq_mov_modulation_difference (peaq->ref_modulation_processor,
                                    peaq->test_modulation_processor,
                                    peaq->mov_accum[MOVBASIC_AVG_MOD_DIFF_1],
                                    peaq->mov_accum[MOVBASIC_AVG_MOD_DIFF_2],
                                    peaq->mov_accum[MOVBASIC_WIN_MOD_DIFF]);
  }

  /* noise loudness */
  if (peaq->frame_counter >= 24 &&
      peaq->frame_counter - 3 >= peaq->loudness_reached_frame) {
    peaq_mov_noise_loudness (peaq->ref_modulation_processor,
                             peaq->test_modulation_processor,
                             peaq->level_adapter,
                             peaq->mov_accum[MOVBASIC_RMS_NOISE_LOUD]);
  }

  /* bandwidth */
  peaq_mov_bandwidth (peaq->ref_fft_ear_state,
                      peaq->test_fft_ear_state, 
                      peaq->mov_accum[MOVBASIC_BANDWIDTH_REF],
                      peaq->mov_accum[MOVBASIC_BANDWIDTH_TEST]);

  /* noise-to-mask ratio */
  peaq_mov_nmr (PEAQ_FFTEARMODEL (peaq->fft_ear_model),
                peaq->ref_fft_ear_state,
                peaq->test_fft_ear_state,
                peaq->mov_accum[MOVBASIC_TOTAL_NMR],
                peaq->mov_accum[MOVBASIC_REL_DIST_FRAMES]);

  /* probability of detection */
  peaq_mov_prob_detect(peaq->fft_ear_model,
                       peaq->ref_fft_ear_state,
                       peaq->test_fft_ear_state,
                       peaq->mov_accum[MOVBASIC_ADB],
                       peaq->mov_accum[MOVBASIC_MFPD]);

  /* error harmonic structure */
  calc_ehs (peaq, peaq->ref_fft_ear_state, peaq->test_fft_ear_state,
            peaq->mov_accum[MOVBASIC_EHS]);

  for (i = 0; i < channels * frame_size / 2; i++) {
    peaq->total_signal_energy
      += refdata[i] * refdata[i];
    peaq->total_noise_energy 
      += (refdata[i] - testdata[i]) * (refdata[i] - testdata[i]);
  }

  peaq->frame_counter++;
}

static void
process_fft_block_advanced (GstPeaq *peaq, gfloat *refdata, gfloat *testdata)
{
  guint i;
  gint channels = peaq->channels;

  PeaqEarModel *ear_params = peaq->fft_ear_model;
  guint frame_size = peaq_earmodel_get_frame_size (ear_params);

  gboolean above_thres =
    is_frame_above_threshold (refdata, frame_size, channels);

  peaq_movaccum_set_tentative (peaq->mov_accum[MOVADV_SEGMENTAL_NMR],
                               !above_thres);
  peaq_movaccum_set_tentative (peaq->mov_accum[MOVADV_EHS], !above_thres);

  apply_ear_model (peaq->fft_ear_model, channels, refdata,
                   peaq->ref_fft_ear_state);
  apply_ear_model (peaq->fft_ear_model, channels, testdata,
                   peaq->test_fft_ear_state);

  /* noise-to-mask ratio */
  peaq_mov_nmr (PEAQ_FFTEARMODEL (peaq->fft_ear_model),
                peaq->ref_fft_ear_state,
                peaq->test_fft_ear_state,
                peaq->mov_accum[MOVADV_SEGMENTAL_NMR],
                NULL);

  /* error harmonic structure */
  calc_ehs (peaq, peaq->ref_fft_ear_state, peaq->test_fft_ear_state,
            peaq->mov_accum[MOVADV_EHS]);

  for (i = 0; i < channels * frame_size / 2; i++) {
    peaq->total_signal_energy += refdata[i] * refdata[i];
    peaq->total_noise_energy 
      += (refdata[i] - testdata[i]) * (refdata[i] - testdata[i]);
  }

  peaq->frame_counter++;
}

static void
process_fb_block (GstPeaq *peaq, gfloat *refdata, gfloat *testdata)
{
  gint channels = peaq->channels;
  PeaqEarModel *ear_params = peaq->fb_ear_model;
  guint frame_size = peaq_earmodel_get_frame_size (ear_params);

  gboolean above_thres =
    is_frame_above_threshold (refdata, frame_size, channels);

  peaq_movaccum_set_tentative (peaq->mov_accum[MOVADV_RMS_MOD_DIFF],
                               !above_thres);
  peaq_movaccum_set_tentative (peaq->mov_accum[MOVADV_RMS_NOISE_LOUD_ASYM],
                               !above_thres);
  peaq_movaccum_set_tentative (peaq->mov_accum[MOVADV_AVG_LIN_DIST],
                               !above_thres);

  apply_ear_model_and_preprocess (peaq, peaq->fb_ear_model,
                                  refdata, testdata,
                                  peaq->ref_fb_ear_state,
                                  peaq->test_fb_ear_state,
                                  peaq->frame_counter_fb);

  /* modulation difference */
  if (peaq->frame_counter_fb >= 125) {
    peaq_mov_modulation_difference (peaq->ref_modulation_processor,
                                    peaq->test_modulation_processor,
                                    peaq->mov_accum[MOVADV_RMS_MOD_DIFF],
                                    NULL, NULL);
  }

  /* noise loudness */
  if (peaq->frame_counter_fb >= 125 &&
      peaq->frame_counter_fb - 13 >= peaq->loudness_reached_frame) {
    peaq_mov_noise_loud_asym (peaq->ref_modulation_processor,
                              peaq->test_modulation_processor,
                              peaq->level_adapter,
                              peaq->mov_accum[MOVADV_RMS_NOISE_LOUD_ASYM]);
    peaq_mov_lin_dist (peaq->ref_modulation_processor,
                       peaq->test_modulation_processor,
                       peaq->level_adapter,
                       peaq->ref_fb_ear_state,
                       peaq->mov_accum[MOVADV_AVG_LIN_DIST]);
  }

  peaq->frame_counter_fb++;
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
calc_ehs (GstPeaq const *peaq, gpointer *ref_state, gpointer *test_state,
          PeaqMovAccum *mov_accum)
{
  guint i;
  guint chan;

  gint channels = peaq->channels;

  GstPeaqClass *peaq_class = GST_PEAQ_GET_CLASS (peaq);
  guint frame_size = peaq_earmodel_get_frame_size (peaq->fft_ear_model);
  gdouble ehs_valid = FALSE;
  for (chan = 0; chan < channels; chan++) {
    if (peaq_fftearmodel_is_energy_threshold_reached (ref_state[chan]) ||
        peaq_fftearmodel_is_energy_threshold_reached (test_state[chan]))
    ehs_valid = TRUE;
  }
  if (!ehs_valid)
    return;

  for (chan = 0; chan < channels; chan++) {
    gdouble const *ref_power_spectrum =
      peaq_fftearmodel_get_weighted_power_spectrum (ref_state[chan]);
    gdouble const *test_power_spectrum =
      peaq_fftearmodel_get_weighted_power_spectrum (test_state[chan]);

    gdouble *d = g_newa (gdouble, frame_size / 2 + 1);
    gdouble c[MAXLAG];
    gdouble d0;
    gdouble dk;
    gdouble cavg;
    gdouble ehs = 0.;
    GstFFTF64Complex c_fft[MAXLAG / 2 + 1];
    gdouble s;
    for (i = 0; i < 2 * MAXLAG; i++) {
      gdouble fref = ref_power_spectrum[i];
      gdouble ftest = test_power_spectrum[i];
      if (fref == 0. && ftest == 0.)
        d[i] = 0.;
      else
        d[i] = log (ftest / fref);
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
    s = c_fft[0].r * c_fft[0].r + c_fft[0].i * c_fft[0].i;
    for (i = 1; i < MAXLAG / 2 + 1; i++) {
      gdouble new_s = c_fft[i].r * c_fft[i].r + c_fft[i].i * c_fft[i].i;
      if (new_s > s && new_s > ehs)
        ehs = new_s;
      s = new_s;
    }
    peaq_movaccum_accumulate (mov_accum, chan, ehs, 1.);
  }
}

static double
calculate_di (GstPeaq * peaq)
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
calculate_di_advanced (GstPeaq *peaq)
{
  guint i;
  gdouble x[5];
  gdouble movs[5];

  guint band_count
    = peaq_earmodel_get_band_count (peaq->fb_ear_model);
  gdouble distortion_index;
  movs[0] = sqrt (band_count) *
    peaq_movaccum_get_value (peaq->mov_accum[MOVADV_RMS_MOD_DIFF]);
  movs[1] =
    peaq_movaccum_get_value (peaq->mov_accum[MOVADV_RMS_NOISE_LOUD_ASYM]);
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
calculate_odg (GstPeaq * peaq)
{
  gdouble distortion_index;
  if (peaq->advanced)
    distortion_index = calculate_di_advanced (peaq);
  else
    distortion_index = calculate_di (peaq);
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
