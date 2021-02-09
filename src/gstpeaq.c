/* GstPEAQ
 * Copyright (C) 2006, 2007, 2010, 2011, 2012, 2013, 2014, 2015
 * Martin Holters <martin.holters@hsu-hh.de>
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

/**
 * SECTION:gstpeaq
 * @short_description: The peaq GStreamer Element.
 * @title: GstPeaq
 *
 * The GstPeaq element is accessed as "peaq" and acts as a sink, providing a
 * "ref" and a "test" pad. If these are fed with the reference and test signal,
 * respectively, the element computes the objective difference grade according
 * to <xref linkend="BS1387" />. (Note, however, that GstPeaq does not fulfill
 * the requirements of conformance specified therein.) Both pads require the
 * input to be "audio/x-raw-float" sampled at 48 kHz. Both mono and stereo
 * signals are supported.
 *
 * GstPeaq supports both the basic and the advanced version of <xref
 * linkend="BS1387" />, as controlled with #GstPeaq:advanced.
 *
 * The resulting objective difference grade can be acquired at any time using
 * the #GstPeaq:odg property. If #GstPeaq:console-output is set to TRUE, the
 * final objective difference grade (and some additional data) is also printed
 * to stdout when the playback is stopped.
 *
 * Assuming the reference and test signal are stored in "ref.wav" and
 * "test.wav", the following will calculate the basic version objective
 * difference grade and print the result to the console:
 * |[
 * gst-launch \
 *   filesrc location="ref.wav" \! wavparse \! audioconvert name=refsrc \
 *   filesrc location="test.wav" \! wavparse \! audioconvert name=testsrc \
 *   peaq name=peaq \
 *   refsrc.src\!peaq.ref testsrc.src\!peaq.test
 * ]|
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gprintf.h>
#include <gst/base/gstadapter.h>
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
#include "nn.h"

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
  gboolean console_output;
  gboolean advanced;
  gint channels;
  guint frame_counter;
  guint frame_counter_fb;
  guint loudness_reached_frame;
  PeaqEarModel *fft_ear_model;
  gpointer *ref_fft_ear_state;
  gpointer *test_fft_ear_state;
  PeaqEarModel *fb_ear_model;
  gpointer *ref_fb_ear_state;
  gpointer *test_fb_ear_state;
  PeaqLevelAdapter **level_adapter;
  PeaqModulationProcessor **ref_modulation_processor;
  PeaqModulationProcessor **test_modulation_processor;
  PeaqMovAccum *mov_accum[COUNT_MOV_BASIC];
  gdouble total_signal_energy;
  gdouble total_noise_energy;
};

struct _GstPeaqClass
{
  GstElementClass parent_class;
};

#define STATIC_CAPS \
  GST_STATIC_CAPS ( \
		    "audio/x-raw, " \
                    "format = F32LE," \
                    "layout = interleaved," \
		    "rate = (int) 48000 " \
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

static void base_init (gpointer g_class);
static void class_init (gpointer g_class, gpointer class_data);
static void init (GTypeInstance *obj, gpointer g_class);
static void finalize (GObject * object);
static void free_per_channel_data(GstPeaq *peaq);
static void alloc_per_channel_data(GstPeaq *peaq);
static void get_property (GObject *obj, guint id, GValue *value,
                          GParamSpec *pspec);
static void set_property (GObject *obj, guint id, const GValue *value,
                          GParamSpec *pspec);
static gboolean set_caps (GstPad *pad, GstCaps *caps);
static GstFlowReturn pad_chain (GstPad *pad, GstObject *parent, GstBuffer *buffer);
static gboolean pad_event (GstPad *pad, GstObject *parent, GstEvent *event);
static gboolean pad_query (GstPad *pad, GstObject *parent, GstQuery *query);
static GstStateChangeReturn change_state (GstElement * element,
                                          GstStateChange transition);
static void process_fft_block_basic (GstPeaq *peaq, gfloat *refdata,
                                     gfloat *testdata);
static void process_fft_block_advanced (GstPeaq *peaq, gfloat *refdata,
                                        gfloat *testdata);
static void process_fb_block (GstPeaq *peaq, gfloat *refdata, gfloat *testdata);
static double calculate_di_basic (GstPeaq * peaq);
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
pad_query (GstPad *pad, GstObject *parent, GstQuery *query)
{
  switch (query->type) {
    case GST_QUERY_CAPS:
      {
        GstPeaq *peaq = GST_PEAQ (parent);
        GstCaps *caps;
        GstCaps *mycaps;
        GstCaps *filt;
        gst_query_parse_caps (query, &filt);
        if (pad == peaq->refpad) {
          mycaps = gst_static_pad_template_get_caps (&gst_peaq_ref_template);
          filt = gst_pad_peer_query_caps (peaq->testpad, filt);
        } else {
          mycaps = gst_static_pad_template_get_caps (&gst_peaq_test_template);
          filt = gst_pad_peer_query_caps (peaq->refpad, filt);
        }
        caps = gst_caps_intersect (mycaps, filt);
        gst_caps_unref (filt);
        gst_caps_unref (mycaps);

        gst_query_set_caps_result (query, caps);
        gst_caps_unref (caps);
        return TRUE;
      }
    default:
      return gst_pad_query_default (pad, parent, query);
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

  element_class->change_state = change_state;

  gobject_class->finalize = finalize;
}

static void
class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *object_class = G_OBJECT_CLASS (g_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

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

  gst_element_class_set_static_metadata (element_class,
                                        "Perceptual evaluation of audio quality",
                                        "Sink/Audio",
                                        "Compute objective audio quality measures",
                                        "Martin Holters <" PACKAGE_BUGREPORT ">");
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

  template = gst_static_pad_template_get (&gst_peaq_ref_template);
  peaq->refpad = gst_pad_new_from_template (template, "ref");
  gst_object_unref (template);
  gst_pad_set_chain_function (peaq->refpad, pad_chain);
  gst_pad_set_event_function (peaq->refpad, pad_event);
  gst_pad_set_query_function (peaq->refpad, pad_query);
  gst_element_add_pad (GST_ELEMENT (peaq), peaq->refpad);

  template = gst_static_pad_template_get (&gst_peaq_test_template);
  peaq->testpad = gst_pad_new_from_template (template, "test");
  gst_object_unref (template);
  gst_pad_set_chain_function (peaq->testpad, pad_chain);
  gst_pad_set_event_function (peaq->testpad, pad_event);
  gst_pad_set_query_function (peaq->testpad, pad_query);
  gst_element_add_pad (GST_ELEMENT (peaq), peaq->testpad);

  GST_OBJECT_FLAG_SET (peaq, GST_ELEMENT_FLAG_SINK);

  peaq->frame_counter = 0;
  peaq->frame_counter_fb = 0;
  peaq->loudness_reached_frame = G_MAXUINT;
  peaq->total_signal_energy = 0.;
  peaq->total_noise_energy = 0.;

  peaq->channels = 0;
  peaq->fft_ear_model = g_object_new (PEAQ_TYPE_FFTEARMODEL, NULL);
  peaq->fb_ear_model = g_object_new (PEAQ_TYPE_FILTERBANKEARMODEL, NULL);

  peaq->ref_fft_ear_state = NULL;
  peaq->test_fft_ear_state = NULL;
  peaq->ref_fb_ear_state = NULL;
  peaq->test_fb_ear_state = NULL;

  peaq->level_adapter = NULL;
  peaq->ref_modulation_processor = NULL;
  peaq->test_modulation_processor = NULL;
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
  free_per_channel_data (peaq);
  g_object_unref (peaq->ref_adapter_fft);
  g_object_unref (peaq->test_adapter_fft);
  g_object_unref (peaq->ref_adapter_fb);
  g_object_unref (peaq->test_adapter_fb);
  g_object_unref (peaq->fft_ear_model);
  g_object_unref (peaq->fb_ear_model);
  for (i = 0; i < COUNT_MOV_BASIC; i++)
    peaq_movaccum_delete (peaq->mov_accum[i]);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
free_per_channel_data (GstPeaq *peaq)
{
  guint c;

  if (peaq->ref_fft_ear_state) {
    for (c = 0; c < peaq->channels; c++)
      peaq_earmodel_state_free (peaq->fft_ear_model, peaq->ref_fft_ear_state[c]);
    g_free (peaq->ref_fft_ear_state);
  }
  if (peaq->test_fft_ear_state) {
    for (c = 0; c < peaq->channels; c++)
      peaq_earmodel_state_free (peaq->fft_ear_model, peaq->test_fft_ear_state[c]);
    g_free (peaq->test_fft_ear_state);
  }
  if (peaq->level_adapter) {
    for (c = 0; c < peaq->channels; c++)
      peaq_leveladapter_delete (peaq->level_adapter[c]);
    g_free (peaq->level_adapter);
  }
  if (peaq->ref_modulation_processor) {
    for (c = 0; c < peaq->channels; c++)
      peaq_modulationprocessor_delete(peaq->ref_modulation_processor[c]);
    g_free (peaq->ref_modulation_processor);
  }
  if (peaq->test_modulation_processor) {
    for (c = 0; c < peaq->channels; c++)
      peaq_modulationprocessor_delete(peaq->test_modulation_processor[c]);
    g_free (peaq->test_modulation_processor);
  }
  if (peaq->ref_fb_ear_state) {
    for (c = 0; c < peaq->channels; c++)
      peaq_earmodel_state_free (peaq->fb_ear_model, peaq->ref_fb_ear_state[c]);
    g_free (peaq->ref_fb_ear_state);
  }
  if (peaq->test_fb_ear_state) {
    for (c = 0; c < peaq->channels; c++)
      peaq_earmodel_state_free (peaq->fb_ear_model, peaq->test_fb_ear_state[c]);
    g_free (peaq->test_fb_ear_state);
  }
}

static void
alloc_per_channel_data (GstPeaq *peaq)
{
  guint c;

  peaq->ref_fft_ear_state = g_new (gpointer, peaq->channels);
  peaq->test_fft_ear_state = g_new (gpointer, peaq->channels);
  peaq->level_adapter = g_new (PeaqLevelAdapter *, peaq->channels);
  peaq->ref_modulation_processor = g_new (PeaqModulationProcessor *, peaq->channels);
  peaq->test_modulation_processor = g_new (PeaqModulationProcessor *, peaq->channels);
  for (c = 0; c < peaq->channels; c++) {
    peaq->ref_fft_ear_state[c] = peaq_earmodel_state_alloc (peaq->fft_ear_model);
    peaq->test_fft_ear_state[c] = peaq_earmodel_state_alloc (peaq->fft_ear_model);
    peaq->level_adapter[c] = peaq_leveladapter_new (peaq->fft_ear_model);
    peaq->ref_modulation_processor[c] =
      peaq_modulationprocessor_new (peaq->fft_ear_model);
    peaq->test_modulation_processor[c] =
      peaq_modulationprocessor_new (peaq->fft_ear_model);
  }
  if (peaq->advanced) {
    peaq->ref_fb_ear_state = g_new (gpointer, peaq->channels);
    peaq->test_fb_ear_state = g_new (gpointer, peaq->channels);
    for (c = 0; c < peaq->channels; c++) {
      peaq->ref_fb_ear_state[c] = peaq_earmodel_state_alloc (peaq->fb_ear_model);
      peaq->test_fb_ear_state[c] = peaq_earmodel_state_alloc (peaq->fb_ear_model);
      peaq_leveladapter_set_ear_model (peaq->level_adapter[c], peaq->fb_ear_model);
      peaq_modulationprocessor_set_ear_model (peaq->ref_modulation_processor[c],
                                              peaq->fb_ear_model);
      peaq_modulationprocessor_set_ear_model (peaq->test_modulation_processor[c],
                                              peaq->fb_ear_model);
    }
  }
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
        g_value_set_double (value, calculate_di_basic (peaq));
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
      g_object_set_property (G_OBJECT (peaq->fb_ear_model),
			     "playback-level", value);
      break;
    case PROP_MODE_ADVANCED:
      {
        guint band_count;
        free_per_channel_data (peaq);
        peaq->advanced = g_value_get_boolean (value);
        if (peaq->advanced) {
          band_count = 55;
        } else {
          band_count = 109;
        }
        g_object_set (peaq->fft_ear_model, "number-of-bands", band_count, NULL);
        if (peaq->advanced) {
          peaq_movaccum_set_mode (peaq->mov_accum[MOVADV_RMS_MOD_DIFF],
                                  MODE_RMS);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVADV_SEGMENTAL_NMR],
                                  MODE_AVG);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVADV_EHS], MODE_AVG);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVADV_AVG_LIN_DIST],
                                  MODE_AVG);
          peaq_movaccum_set_mode (peaq->mov_accum[MOVADV_RMS_NOISE_LOUD_ASYM],
                                  MODE_RMS_ASYM);
        } else {
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
        }
        alloc_per_channel_data (peaq);
      }
      break;
    case PROP_CONSOLE_OUTPUT:
      peaq->console_output = g_value_get_boolean (value);
      break;
  }
}

static gboolean
set_caps (GstPad *pad, GstCaps *caps)
{
  GstPeaq *peaq = GST_PEAQ (gst_pad_get_parent_element (pad));

  GST_OBJECT_LOCK (peaq);

  free_per_channel_data (peaq);

  guint i;
  gst_structure_get_int (gst_caps_get_structure (caps, 0),
                         "channels", &(peaq->channels));
  for (i = 0; i < COUNT_MOV_BASIC; i++)
    if (!peaq->advanced && (i == MOVBASIC_ADB || i == MOVBASIC_MFPD))
      peaq_movaccum_set_channels (peaq->mov_accum[i], 1);
    else
      peaq_movaccum_set_channels (peaq->mov_accum[i], peaq->channels);

  alloc_per_channel_data (peaq);

  GST_OBJECT_UNLOCK (peaq);

  gst_object_unref (peaq);

  return TRUE;
}

static void
do_processing (GstPeaq *peaq, GstAdapter *ref_adapter, GstAdapter *test_adapter,
               void (*process_block)(GstPeaq *, gfloat*, gfloat *),
               guint frame_size_bytes, guint step_size_bytes)
{
  while (gst_adapter_available (ref_adapter) >= frame_size_bytes &&
         gst_adapter_available (test_adapter) >= frame_size_bytes)
  {
    gfloat *refframe = (gfloat *) gst_adapter_map (ref_adapter, frame_size_bytes);
    gfloat *testframe = (gfloat *) gst_adapter_map (test_adapter, frame_size_bytes);
    process_block (peaq, refframe, testframe);
    gst_adapter_unmap (ref_adapter);
    gst_adapter_unmap (test_adapter);
    gst_adapter_flush (ref_adapter, step_size_bytes);
    gst_adapter_flush (test_adapter, step_size_bytes);
  }
}

static GstFlowReturn
pad_chain (GstPad *pad, GstObject *parent, GstBuffer *buffer)
{
  GstElement *element = GST_ELEMENT (parent);
  GstPeaq *peaq = GST_PEAQ (element);

  GST_OBJECT_LOCK (peaq);

  if (element->pending_state != GST_STATE_VOID_PENDING) {
    element->current_state = element->pending_state;
    element->pending_state = GST_STATE_VOID_PENDING;
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
 
  guint frame_size_bytes =
    peaq->channels * sizeof (gfloat) *
    peaq_earmodel_get_frame_size (peaq->fft_ear_model);
  guint step_size_bytes =
    peaq->channels * sizeof (gfloat) *
    peaq_earmodel_get_step_size (peaq->fft_ear_model);

  if (peaq->advanced) {
    do_processing (peaq, peaq->ref_adapter_fft, peaq->test_adapter_fft,
                   process_fft_block_advanced, frame_size_bytes, step_size_bytes);
    frame_size_bytes =
      peaq->channels * sizeof (gfloat) *
      peaq_earmodel_get_frame_size (peaq->fb_ear_model);
    do_processing (peaq, peaq->ref_adapter_fb, peaq->test_adapter_fb,
                   process_fb_block, frame_size_bytes, frame_size_bytes);
  } else {
    do_processing (peaq, peaq->ref_adapter_fft, peaq->test_adapter_fft,
                   process_fft_block_basic, frame_size_bytes, step_size_bytes);
  }

  GST_OBJECT_UNLOCK (peaq);

  return GST_FLOW_OK;
}

static gboolean
pad_event (GstPad *pad, GstObject *parent, GstEvent* event)
{
  gboolean ret = FALSE;
  switch (event->type) {
    case GST_EVENT_EOS:
      {
        GstElement *element = GST_ELEMENT (parent);
        GstPeaq *peaq = GST_PEAQ (element);

        if (pad == peaq->refpad) {
          peaq->ref_eos = TRUE;
        } else if (pad == peaq->testpad) {
          peaq->test_eos = TRUE;
        }

        if (peaq->ref_eos && peaq->test_eos) {
          GstMessage *msg = gst_message_new_eos (parent);
          guint32 seqnum = gst_event_get_seqnum (event);
          gst_message_set_seqnum (msg, seqnum);
          ret = gst_element_post_message (element, msg);
        }

        gst_event_unref (event);
        break;
      }
    case GST_EVENT_CAPS:
      {
        GstCaps *caps;
        gst_event_parse_caps (event, &caps);
        GstPad *other_pad;
        GstPeaq *peaq = GST_PEAQ (parent);
        if (pad == peaq->refpad) {
          other_pad = peaq->testpad;
        } else {
          other_pad = peaq->refpad;
        }
        if (gst_pad_peer_query_accept_caps (other_pad, caps)) {
          set_caps (pad, caps);
          ret = TRUE;
        } else {
          ret = FALSE;
        }
        gst_event_unref (event);
        break;
      }
    default:
      ret = gst_pad_event_default (pad, parent, event);
  }
  return ret;
}

static void
do_flush (GstPeaq *peaq, GstAdapter *ref_adapter, GstAdapter *test_adapter,
          void (*process_block)(GstPeaq *, gfloat*, gfloat *),
          guint frame_size)
{
  guint ref_data_left_count, test_data_left_count;
  ref_data_left_count = gst_adapter_available (ref_adapter);
  test_data_left_count = gst_adapter_available (test_adapter);
  if (ref_data_left_count || test_data_left_count) {
    guint frame_size_bytes = peaq->channels * sizeof (gfloat) * frame_size;
    gfloat *padded_ref_frame = g_newa (gfloat, peaq->channels * frame_size);
    gfloat *padded_test_frame = g_newa (gfloat, peaq->channels * frame_size);
    guint ref_data_count = MIN (ref_data_left_count, frame_size_bytes);
    guint test_data_count = MIN (test_data_left_count, frame_size_bytes);
    gfloat *refframe = (gfloat *) gst_adapter_map (ref_adapter,
                                                   ref_data_count);
    gfloat *testframe = (gfloat *) gst_adapter_map (test_adapter,
                                                    test_data_count);
    memmove (padded_ref_frame, refframe, ref_data_count);
    memset (((char *) padded_ref_frame) + ref_data_count, 0,
            frame_size_bytes - ref_data_count);
    memmove (padded_test_frame, testframe, test_data_count);
    memset (((char *) padded_test_frame) + test_data_count, 0,
            frame_size_bytes - test_data_count);
    gst_adapter_unmap (ref_adapter);
    gst_adapter_unmap (test_adapter);
    process_block (peaq, padded_ref_frame, padded_test_frame);
    gst_adapter_flush (ref_adapter, ref_data_count);
    gst_adapter_flush (test_adapter, test_data_count);
  }
}

static GstStateChangeReturn
change_state (GstElement * element, GstStateChange transition)
{
  GstPeaq *peaq;

  GstElementClass *parent_class = 
    GST_ELEMENT_CLASS (g_type_class_peek_parent (g_type_class_peek
                                                 (GST_TYPE_PEAQ)));
  peaq = GST_PEAQ (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (peaq->advanced) {
        do_flush (peaq, peaq->ref_adapter_fft, peaq->test_adapter_fft,
                  process_fft_block_advanced, 
                  peaq_earmodel_get_frame_size (peaq->fft_ear_model));
        do_flush (peaq, peaq->ref_adapter_fb, peaq->test_adapter_fb,
                  process_fb_block, 
                  peaq_earmodel_get_frame_size (peaq->fb_ear_model));
      } else {
        do_flush (peaq, peaq->ref_adapter_fft, peaq->test_adapter_fft,
                  process_fft_block_basic, 
                  peaq_earmodel_get_frame_size (peaq->fft_ear_model));
      }

      calculate_odg (peaq);

      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state (element, transition) ==
      GST_STATE_CHANGE_FAILURE) {
    return GST_STATE_CHANGE_FAILURE;
  }

  return GST_STATE_CHANGE_SUCCESS;
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
                       peaq->channels,
                       peaq->mov_accum[MOVBASIC_ADB],
                       peaq->mov_accum[MOVBASIC_MFPD]);

  /* error harmonic structure */
  peaq_mov_ehs (peaq->fft_ear_model, peaq->ref_fft_ear_state,
                peaq->test_fft_ear_state, peaq->mov_accum[MOVBASIC_EHS]);

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
  peaq_mov_ehs (peaq->fft_ear_model, peaq->ref_fft_ear_state,
                peaq->test_fft_ear_state, peaq->mov_accum[MOVADV_EHS]);

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

static double
calculate_di_basic (GstPeaq * peaq)
{
  guint i;
  gdouble movs[11];
  for (i = 0; i < COUNT_MOV_BASIC; i++)
    movs[i] = peaq_movaccum_get_value (peaq->mov_accum[i]);

  gdouble distortion_index = peaq_calculate_di_basic (movs);

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
  gdouble movs[5];
  for (i = 0; i < COUNT_MOV_ADVANCED; i++)
    movs[i] = peaq_movaccum_get_value (peaq->mov_accum[i]);

  gdouble distortion_index = peaq_calculate_di_advanced (movs);

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
    distortion_index = calculate_di_basic (peaq);
  gdouble odg = peaq_calculate_odg (distortion_index);
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
