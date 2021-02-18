/* GstPEAQ
 * Copyright (C) 2006, 2007, 2010, 2011, 2012, 2013, 2014, 2015, 2021
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
#include "peaqalgo.h"

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

struct _GstPeaq
{
  GstElement element;
  GstPad *refpad;
  GstPad *testpad;
  gboolean ref_eos;
  gboolean test_eos;
  GstAdapter *ref_adapter;
  GstAdapter *test_adapter;
  gboolean console_output;
  gboolean advanced;
  gint channels;
  gdouble total_signal_energy;
  gdouble total_noise_energy;
  PeaqAlgo *algo;
  PeaqAlgo *algo_basic;
  PeaqAlgo *algo_advanced;
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
  GstPadTemplate *template;

  GstPeaq *peaq = GST_PEAQ (obj);

  peaq->ref_adapter = gst_adapter_new ();
  peaq->test_adapter = gst_adapter_new ();

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

  peaq->total_signal_energy = 0.;
  peaq->total_noise_energy = 0.;

  peaq->channels = 0;

  peaq->algo_basic = peaq_algo_basic_new ();
  peaq->algo_advanced = peaq_algo_advanced_new ();
}

static void
finalize (GObject * object)
{
  GstElementClass *parent_class =
    GST_ELEMENT_CLASS (g_type_class_peek_parent (g_type_class_peek
                                                 (GST_TYPE_PEAQ)));
  GstPeaq *peaq = GST_PEAQ (object);
  g_object_unref (peaq->ref_adapter);
  g_object_unref (peaq->test_adapter);
  G_OBJECT_CLASS (parent_class)->finalize (object);
  peaq_algo_delete (peaq->algo_basic);
  peaq_algo_delete (peaq->algo_advanced);
}

static void
get_property (GObject *obj, guint id, GValue *value, GParamSpec *pspec)
{
  GstPeaq *peaq = GST_PEAQ (obj);
  switch (id) {
    case PROP_PLAYBACK_LEVEL:
      g_value_set_double (value, peaq_algo_get_playback_level (peaq->algo));
      break;
    case PROP_DI:
      g_value_set_double (value, peaq_algo_calculate_di (peaq->algo, peaq->console_output));
      break;
    case PROP_ODG:
      g_value_set_double (value, peaq_algo_calculate_odg (peaq->algo, peaq->console_output));
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
      peaq_algo_set_playback_level (peaq->algo_basic, g_value_get_double (value));
      peaq_algo_set_playback_level (peaq->algo_advanced, g_value_get_double (value));
      break;
    case PROP_MODE_ADVANCED:
      peaq->advanced = g_value_get_boolean (value);
      if (peaq->advanced) {
        peaq->algo = peaq->algo_advanced;
      } else {
        peaq->algo = peaq->algo_basic;
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

  gst_structure_get_int (gst_caps_get_structure (caps, 0),
                         "channels", &(peaq->channels));

  peaq_algo_set_channels (peaq->algo_basic, peaq->channels);
  peaq_algo_set_channels (peaq->algo_advanced, peaq->channels);

  GST_OBJECT_UNLOCK (peaq);

  gst_object_unref (peaq);

  return TRUE;
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
    gst_adapter_push (peaq->ref_adapter, buffer);
  } else if (pad == peaq->testpad) {
    peaq->test_eos = FALSE;
    gst_adapter_push (peaq->test_adapter, buffer);
  }

  guint data_size = MIN (gst_adapter_available (peaq->ref_adapter),
    gst_adapter_available (peaq->test_adapter));
  if (data_size > 0) {
    gfloat *refframe = (gfloat *) gst_adapter_map (peaq->ref_adapter, data_size);
    gfloat *testframe = (gfloat *) gst_adapter_map (peaq->test_adapter, data_size);
    guint frame_size = data_size / (peaq->channels * sizeof(gfloat));
    peaq_algo_process_block (peaq->algo, refframe, testframe, frame_size);
    guint i;
    for (i = 0; i < data_size / sizeof(gfloat); i++) {
      peaq->total_signal_energy += refframe[i] * refframe[i];
      peaq->total_noise_energy
        += (refframe[i] - testframe[i]) * (refframe[i] - testframe[i]);
    }

    gst_adapter_unmap (peaq->ref_adapter);
    gst_adapter_unmap (peaq->test_adapter);
    gst_adapter_flush (peaq->ref_adapter, data_size);
    gst_adapter_flush (peaq->test_adapter, data_size);
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
      peaq_algo_flush (peaq->algo);
      peaq_algo_calculate_odg (peaq->algo, peaq->console_output);
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