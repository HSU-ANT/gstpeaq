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

#include "gstpeaq.h"
#include "earmodel.h"
#include "leveladapter.h"

#define BLOCKSIZE 2048
#define STEPSIZE (BLOCKSIZE/2)
#define BLOCKSIZE_BYTES (BLOCKSIZE * sizeof(gfloat))
#define STEPSIZE_BYTES (STEPSIZE * sizeof(gfloat))

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

static void gst_peaq_finalize (GObject * object);
static GstFlowReturn gst_peaq_collected (GstCollectPads * pads,
					 gpointer user_data);
static GstStateChangeReturn gst_peaq_change_state (GstElement * element,
						   GstStateChange transition);

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
gst_peaq_class_init (GstPeaqClass * g_class)
{
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

  peaq->bytes_read = 0;

  peaq->ref_ear_model = g_object_new (PEAQ_TYPE_EARMODEL, NULL);
  peaq->test_ear_model = g_object_new (PEAQ_TYPE_EARMODEL, NULL);
  peaq->level_adapter = g_object_new (PEAQ_TYPE_LEVELADAPTER, NULL);
}

static void
gst_peaq_finalize (GObject * object)
{
  GstPeaq *peaq = GST_PEAQ (object);
  g_object_unref (peaq->collect);
  g_object_unref (peaq->ref_adapter);
  g_object_unref (peaq->test_adapter);
  G_OBJECT_CLASS (parent_class)->finalize (object);
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
      g_printf ("Got buffer of size %d ", len);
      if (data->pad == peaq->refpad) {
	g_printf ("for reference pad.\n");
	gst_adapter_push (peaq->ref_adapter, buf);
      } else if (data->pad == peaq->testpad) {
	g_printf ("for test pad.\n");
	gst_adapter_push (peaq->test_adapter, buf);
      } else
	g_printf ("for unknown pad.\n");
      buf = gst_collect_pads_pop (pads, data);
    }
  }

  while (gst_adapter_available (peaq->ref_adapter) >= BLOCKSIZE_BYTES
	 && gst_adapter_available (peaq->test_adapter) >= BLOCKSIZE_BYTES) {
    guint i;
    EarModelOutput ref_output;
    EarModelOutput test_output;
    LevelAdapterOutput level_output;
    gdouble noise_spectrum[FRAMESIZE / 2 + 1];
    gdouble noise_in_bands[CRITICAL_BAND_COUNT];
    gfloat *refframe = (gfloat *) gst_adapter_peek (peaq->ref_adapter,
						    BLOCKSIZE_BYTES);
    gfloat *testframe = (gfloat *) gst_adapter_peek (peaq->test_adapter,
						     BLOCKSIZE_BYTES);
    g_printf ("Processing frame...\n");
    peaq_earmodel_process (peaq->ref_ear_model, refframe, &ref_output);
    peaq_earmodel_process (peaq->test_ear_model, testframe, &test_output);
    for (i = 0; i < FRAMESIZE / 2 + 1; i++)
      noise_spectrum[i] = 
	ABS(ref_output.weighted_fft[i] - test_output.weighted_fft[i]);
    peaq_earmodel_group_into_bands (PEAQ_EARMODEL_GET_CLASS(peaq->ref_ear_model),
				    noise_spectrum, noise_in_bands);
    peaq_leveladapter_process (peaq->level_adapter, ref_output.excitation,
			       test_output.excitation, &level_output);

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

  peaq = GST_PEAQ (element);

  g_printf ("State transition...\n");

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
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}
