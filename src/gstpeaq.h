/* GstPEAQ
 * Copyright (C) 2006, 2011, 2013 Martin Holters <martin.holters@hsuhh.de>
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

#include "fbearmodel.h"
#include "fftearmodel.h"
#include "leveladapter.h"
#include "modpatt.h"
#include "movaccum.h"

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>
#include <gst/base/gstadapter.h>
#include <gst/fft/gstfftf64.h>


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
typedef struct _GstPeaqAggregatedDataFFTBasic GstPeaqAggregatedDataFFTBasic;
typedef struct _GstPeaqAggregatedDataFFTAdvanced GstPeaqAggregatedDataFFTAdvanced;
typedef struct _GstPeaqAggregatedDataFB GstPeaqAggregatedDataFB;

struct _GstPeaq
{
  GstElement element;
  GstPad *refpad;
  GstPad *testpad;
  GstCollectPads *collect;
  GstAdapter *ref_adapter_fft;
  GstAdapter *test_adapter_fft;
  GstAdapter *ref_adapter_fb;
  GstAdapter *test_adapter_fb;
  GstFFTF64 *correlation_fft;
  GstFFTF64 *correlator_fft;
  GstFFTF64 *correlator_inverse_fft;
  gboolean console_output;
  gboolean advanced;
  guint channels;
  gdouble *masking_difference;
  guint frame_counter;
  guint frame_counter_fb;
  guint loudness_reached_frame;
  PeaqFFTEarModel *ref_ear[2];
  PeaqFFTEarModel *test_ear[2];
  PeaqFilterbankEarModel *ref_ear_fb;
  PeaqFilterbankEarModel *test_ear_fb;
  PeaqLevelAdapter *level_adapter[2];
  PeaqModulationProcessor *ref_modulation_processor[2];
  PeaqModulationProcessor *test_modulation_processor[2];
  PeaqMovAccum *ref_bandwidth_accum;
  PeaqMovAccum *test_bandwidth_accum;
  PeaqMovAccum *nmr_accum;
  PeaqMovAccum *mod_diff_1_accum;
  PeaqMovAccum *mod_diff_2_accum;
  PeaqMovAccum *win_mod_diff_accum;
  PeaqMovAccum *noise_loud_accum;
  PeaqMovAccum *missing_comp_accum;
  PeaqMovAccum *lin_dist_accum;
  PeaqMovAccum *ehs_accum;
  PeaqMovAccum *dist_frame_accum;
  PeaqMovAccum *prob_detect_accum;
  PeaqMovAccum *detect_steps_accum;
  gdouble total_signal_energy;
  gdouble total_noise_energy;
};

struct _GstPeaqClass
{
  GstElementClass parent_class;
  guint sampling_rate;
  gdouble *correlation_window;
};

GType gst_peaq_get_type ();

G_END_DECLS;

#endif /* __GST_PEAQ_H__ */
