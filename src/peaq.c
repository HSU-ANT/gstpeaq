/* GstPEAQ
 * Copyright (C) 2012, 2013, 2014, 2015
 * Martin Holters <martin.holters@hsu-hh.de>
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
#include <stdlib.h>

#include <gst/gstplugin.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

static gchar **filenames;
static gboolean advanced = FALSE;
static gboolean print_version = FALSE;

static GOptionEntry option_entries[] = {
  {"version", 0, 0, G_OPTION_ARG_NONE, &print_version, "print version information",
    NULL},
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

static void on_pad_added(GstElement *element, GstPad *pad, gpointer data) {
    GstElement *sink = (GstElement *)data;
    GstPad *sinkpad = gst_element_get_static_pad(sink, "sink");
    gst_pad_link(pad, sinkpad);
    gst_object_unref(sinkpad);
}

GST_PLUGIN_STATIC_DECLARE(peaq);

int
main(int argc, char *argv[])
{
  gdouble odg, di;
  GError *error = NULL;
  GOptionContext *context;
  GOptionGroup *option_group;
  GstElement *pipeline, *ref_source, *ref_decodebin, *ref_converter, *ref_resample,
             *test_source, *test_decodebin, *test_converter, *test_resample, *peaq;
  gchar *reffilename;
  gchar *testfilename;

#if !GLIB_CHECK_VERSION(2, 32, 0)
  if (!g_thread_supported ())
    g_thread_init (NULL);
#endif

  /* parse command-line options and initialize */
  context = g_option_context_new (NULL);
  option_group =
    g_option_group_new ("main", "main group", "help for main group", NULL,
                        NULL);

  g_option_group_add_entries (option_group, option_entries);

  g_option_context_set_main_group (context, option_group);
  g_option_context_add_group (context, gst_init_get_option_group ());
  g_option_context_set_summary (context,
                                "peaq computes the Objective Difference Grade based on ITU-R BS.1367-1 (but it\n"
                                "does not meet its conformance requirements).");
  g_option_context_set_description (context,
                                    "Report bugs to: <" PACKAGE_BUGREPORT ">\n"
                                    PACKAGE_NAME " home page: <http://ant.hsu-hh.de/gstpeaq>");
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_print ("Failed to initialize: %s\n", error->message);
    g_error_free (error);
    g_option_context_free (context);
    return 1;
  }

  if (print_version) {
    puts (PACKAGE_STRING "\n"
          "Copyright 2015 Martin Holters\n"
          "License LGPLv2+: GNU Library General Public License version2 or later.\n"
          "There is NO WARRANTY, to the extent permitted by law.");
    g_option_context_free (context);
    return 0;
  }

  if (filenames == NULL || filenames[0] == NULL || filenames[1] == NULL
      || filenames[2] != NULL) {
    gchar *help = g_option_context_get_help (context, TRUE, NULL);
    puts (help);
    g_free (help);
    g_option_context_free (context);
    return 1;
  }
  g_option_context_free (context);
  reffilename = filenames[0];
  testfilename = filenames[1];

  loop = g_main_loop_new (NULL, FALSE);

  pipeline = gst_pipeline_new ("pipeline");
  gst_object_ref_sink (pipeline);
  GstBus *bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, my_bus_callback, NULL);
  gst_object_unref (bus);

  peaq = gst_element_factory_make ("peaq", "peaq");
  if (!peaq) {
    puts ("Warning: peaq plugin is not installed - registering statically");
    GST_PLUGIN_STATIC_REGISTER(peaq);
  }

  peaq = gst_element_factory_make ("peaq", "peaq");
  if (!peaq) {
    puts ("Error: peaq element could not be instantiated");
    exit (2);
  }

  gst_object_ref_sink (peaq);
  g_object_set (G_OBJECT (peaq), "advanced", advanced,
                "console_output", FALSE, NULL);
  ref_source = gst_element_factory_make ("filesrc", "ref_file-source");
  if (!ref_source) {
    puts ("Error: filesrc element could not be instantiated");
    exit (2);
  }
  g_object_set (G_OBJECT (ref_source), "location", reffilename, NULL);
  ref_converter = gst_element_factory_make ("audioconvert", "ref-converter");
  if (!ref_converter) {
    puts ("Error: audioconvert element could not be instantiated");
    exit (2);
  }
  ref_resample = gst_element_factory_make ("audioresample", "ref-resampler");
  if (!ref_resample) {
    puts ("Error: audioresample element could not be instantiated");
    exit (2);
  }
  ref_decodebin = gst_element_factory_make("decodebin", "ref_decodebin");
  if (!ref_decodebin) {
      puts("Error: decodebin element could not be instantiated");
      return 2;
  }
  g_signal_connect(ref_decodebin, "pad-added", G_CALLBACK(on_pad_added), ref_converter);

  test_source = gst_element_factory_make ("filesrc", "test_file-source");
  if (!test_source) {
    puts ("Error: filesrc element could not be instantiated");
    exit (2);
  }
  g_object_set (G_OBJECT (test_source), "location", testfilename, NULL);
  test_converter = gst_element_factory_make ("audioconvert", "test-converter");
  if (!test_converter) {
    puts ("Error: audioconvert element could not be instantiated");
    exit (2);
  }
  test_resample = gst_element_factory_make ("audioresample", "test-resampler");
  if (!test_resample) {
    puts ("Error: audioresample element could not be instantiated");
    exit (2);
  }
  test_decodebin = gst_element_factory_make("decodebin", "test_decodebin");
  if (!test_decodebin) {
      puts("Error: decodebin element could not be instantiated");
      return 2;
  }
  g_signal_connect(test_decodebin, "pad-added", G_CALLBACK(on_pad_added), test_converter);

  gst_bin_add_many (GST_BIN (pipeline), 
                    ref_source, ref_decodebin, ref_converter, ref_resample,
                    test_source, test_decodebin, test_converter, test_resample,
                    peaq,
                    NULL);
  gst_element_link(ref_source, ref_decodebin);
  gst_element_link (ref_converter, ref_resample);
  gst_element_link_pads (ref_resample, "src", peaq, "ref");
  gst_element_link(test_source, test_decodebin);
  gst_element_link (test_converter, test_resample);
  gst_element_link_pads (test_resample, "src", peaq, "test");

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);

  g_object_get (peaq, "odg", &odg, NULL);
  g_printf ("Objective Difference Grade: %.3f\n", odg);
  g_object_get (peaq, "di", &di, NULL);
  g_printf ("Distortion Index: %.3f\n", di);

  gst_object_unref (peaq);
  g_main_loop_unref (loop);
  gst_deinit ();

  return 0;
}
