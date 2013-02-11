/* GstPEAQ
 * Copyright (C) 2012, 2013 Martin Holters <martin.holters@hsuhh.de>
 *
 * peaq.c: Command-line frontend to process given WAV files
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

#include <gst/gst.h>
#include <glib/gprintf.h>

static gchar **filenames;
static gboolean advanced = FALSE;

static GOptionEntry option_entries[] = {
  {"advanced", 0, 0, G_OPTION_ARG_NONE, &advanced, "use advanced version",
    NULL},
  {"basic", 0, G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &advanced,
    "use basic version (default)", NULL},
  {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &filenames, NULL,
   "REFFILE TESTFILE"},
  {NULL, 0, 0, G_OPTION_ARG_NONE, NULL, NULL, NULL}
};

static GMainLoop *loop;

static gboolean
my_bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      {
        GError *err;
        gchar *debug;

        gst_message_parse_error (message, &err, &debug);
        g_print ("Error: %s\n", err->message);
        g_error_free (err);
        g_free (debug);

        g_main_loop_quit (loop);
        break;
      }
    case GST_MESSAGE_EOS:
      /* end-of-stream */
      g_main_loop_quit (loop);
      break;
    default:
      /* unhandled message */
      break;
  }
  return TRUE;
}

static void
new_pad (GstElement * element, GstPad * pad, gpointer data)
{
  GstPad *sinkpad = GST_PAD (data);
  gst_pad_link (pad, sinkpad);
}

void
usage()
{
  exit(1);
}

int
main(int argc, char *argv[])
{
  gdouble odg, di;
  GError *error = NULL;
  GOptionContext *context;
  GOptionGroup *option_group;
  GstElement *pipeline, *ref_source, *ref_parser, *ref_converter, *ref_resample,
             *test_source, *test_parser, *test_converter, *test_resample, *peaq;
  gchar *reffilename;
  gchar *testfilename;

  if (!g_thread_supported ())
    g_thread_init (NULL);

  /* parse command-line options and initialize */
  context = g_option_context_new (NULL);
  option_group =
    g_option_group_new ("main", "main group", "help for main group", NULL,
                        NULL);

  g_option_group_add_entries (option_group, option_entries);

  g_option_context_set_main_group (context, option_group);
  g_option_context_add_group (context, gst_init_get_option_group ());
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_print ("Failed to initialize: %s\n", error->message);
    g_error_free (error);
    return 1;
  }
  g_option_context_free (context);

  if (filenames == NULL || filenames[0] == NULL || filenames[1] == NULL
      || filenames[2] != NULL) {
    usage ();
  }
  reffilename = filenames[0];
  testfilename = filenames[1];

  loop = g_main_loop_new (NULL, FALSE);

  pipeline = gst_pipeline_new ("pipeline");
  gst_object_ref (pipeline);
  gst_object_sink (pipeline);
  GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, my_bus_callback, NULL);
  gst_object_unref (bus);

  peaq = gst_element_factory_make ("peaq", "peaq");
  g_object_set (G_OBJECT (peaq), "advanced", advanced,
                "console_output", FALSE, NULL);
  ref_source = gst_element_factory_make ("filesrc", "ref_file-source");
  ref_parser = gst_element_factory_make ("wavparse", "ref_wav-parser");
  g_object_set (G_OBJECT (ref_source), "location", reffilename, NULL);
  ref_converter = gst_element_factory_make ("audioconvert", "ref-converter");
  ref_resample = gst_element_factory_make ("audioresample", "ref-resampler");
  test_source = gst_element_factory_make ("filesrc", "test_file-source");
  test_parser = gst_element_factory_make ("wavparse", "test_wav-parser");
  g_object_set (G_OBJECT (test_source), "location", testfilename, NULL);
  test_converter = gst_element_factory_make ("audioconvert", "test-converter");
  test_resample = gst_element_factory_make ("audioresample", "test-resampler");

  g_signal_connect (ref_parser, "pad-added", G_CALLBACK (new_pad),
                    gst_element_get_pad (ref_converter, "sink"));
  g_signal_connect (test_parser, "pad-added", G_CALLBACK (new_pad),
                    gst_element_get_pad (test_converter, "sink"));

  gst_bin_add_many (GST_BIN (pipeline), 
                    ref_source, ref_parser, ref_converter, ref_resample,
                    test_source, test_parser, test_converter, test_resample,
                    peaq,
                    NULL);
  gst_element_link (ref_source, ref_parser);
  gst_element_link (ref_converter, ref_resample);
  gst_element_link_pads (ref_resample, "src", peaq, "ref");
  gst_element_link (test_source, test_parser);
  gst_element_link (test_converter, test_resample);
  gst_element_link_pads (test_resample, "src", peaq, "test");

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_object_get (peaq, "odg", &odg, NULL);
  g_printf ("Objective Difference Grade: %.3f\n", odg);
  g_object_get (peaq, "di", &di, NULL);
  g_printf ("Distortion Index: %.3f\n", di);

  g_main_loop_unref (loop);

  gst_deinit ();

  return 0;
}


