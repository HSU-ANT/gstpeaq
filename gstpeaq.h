/* GstPEAQ
 * Copyright (C) 2006 Martin Holters <martin.holters@hsuhh.de>
 *
 * gstpeaq.h: Compute objective audio quality measures
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


#ifndef __GST_PEAQ_H__
#define __GST_PEAQ_H__

#include "earmodel.h"
#include "leveladapter.h"
#include "modpatt.h"
#include "fft.h"

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS;

#define GST_TYPE_PEAQ            (gst_peaq_get_type())
#define GST_PEAQ(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), \
							     GST_TYPE_PEAQ, \
							     GstPeaq))
#define GST_PEAQ_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), \
							  GST_TYPE_PEAQ, \
							  GstPeaqClass))
#define GST_IS_PEAQ(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), \
							     GST_TYPE_PEAQ))
#define GST_IS_PEAQ_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), \
							  GST_TYPE_PEAQ))
#define GST_PEAQ_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS(obj, \
							    GST_TYPE_PEAQ, \
							    GstPeaqClass))

#define SAMPLINGRATE 48000

typedef struct _GstPeaq GstPeaq;
typedef struct _GstPeaqClass GstPeaqClass;

struct _GstPeaq
{
  GstElement element;
  GstPad *refpad;
  GstPad *testpad;
  GstCollectPads *collect;
  GstAdapter *ref_adapter;
  GstAdapter *test_adapter;
  guint bytes_read;
  PeaqEarModel *ref_ear_model;
  PeaqEarModel *test_ear_model;
  PeaqLevelAdapter *level_adapter;
  PeaqModulationProcessor *ref_modulation_processor;
  PeaqModulationProcessor *test_modulation_processor;
};

struct _GstPeaqClass
{
  GstElementClass parent_class;
  guint window_length;
  guint sampling_rate;
  gdouble *masking_difference;
  gdouble *correlation_window;
  FFTData *correlation_fft_data;
};

GType gst_peaq_get_type();

G_END_DECLS;

#endif /* __GST_PEAQ_H__ */
