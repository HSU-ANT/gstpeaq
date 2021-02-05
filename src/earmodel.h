/* GstPEAQ
 * Copyright (C) 2006, 2011, 2012, 2013, 2014, 2015, 2021
 * Martin Holters <martin.holters@hsu-hh.de>
 *
 * earmodel.h: Peripheral ear model part.
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


#ifndef __EARMODEL_H__
#define __EARMODEL_H__ 1

#include <glib-object.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PEAQ_TYPE_EARMODEL (peaq_earmodel_get_type ())
#define PEAQ_EARMODEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST (obj, PEAQ_TYPE_EARMODEL, \
                               PeaqEarModel))
#define PEAQ_EARMODEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST (klass, PEAQ_TYPE_EARMODEL, \
                            PeaqEarModelClass))
#define PEAQ_EARMODEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS (obj, PEAQ_TYPE_EARMODEL, \
                              PeaqEarModelClass))

typedef struct _PeaqEarModelClass PeaqEarModelClass;
typedef struct _PeaqEarModel PeaqEarModel;

/**
 * PeaqEarModel:
 * @parent: The parent #GObject.
 * @band_count: Number of frequency bands.
 * @fc: Center frequencies of the frequency bands
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><msub><mi>f</mi><mi>c</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 * </math></inlineequation>
 * in <xref linkend="BS1387" /> and <xref linkend="Kabal03" />).
 * @internal_noise: The ear internal noise per band
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><msub><mi>P</mi><mi>Thres</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><msub><mi>E</mi><mtext>IN</mtext></msub><mfenced open="[" close="]"><mi>i</mi></mfenced></mrow>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * @ear_time_constants: The time constants for time domain spreading / forward
 * masking
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>a</mi>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><mi>&alpha;</mi><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * @excitation_threshold: The excitation threshold per band
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><msub><mi>E</mi><mi>Thres</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><msub><mi>E</mi><mi>t</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * @threshold: The threshold index per band
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><mi>s</mi><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow>
 * </math></inlineequation>
 * in <xref linkend="BS1387" /> and <xref linkend="Kabal03" />).
 * @loudness_factor: The loudness scaling factor per band
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><mi>const</mi><mo>&sdot;</mo><msup><mfenced><mrow><mfrac><mn>1</mn><mrow><mi>s</mi><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow></mfrac><mo>&sdot;</mo><mfrac><mrow><msub><mi>E</mi><mi>Thres</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow><msup><mn>10</mn><mn>4</mn></msup></mfrac></mrow></mfenced><mn>0.23</mn></msup></mrow>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mrow><mi>c</mi><mo>&sdot;</mo><msup><mfenced><mfrac><mrow><msub><mi>E</mi><mi>t</mi></msub><mfenced open="[" close="]"><mi>k</mi></mfenced></mrow><mrow><mi>s</mi><mfenced open="[" close="]"><mi>k</mi></mfenced><msub><mi>E</mi><mn>0</mn></msub></mrow></mfrac></mfenced><mn>0.23</mn></msup></mrow>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 *
 * The fields in #PeaqEarModel get updated when the #PeaqEarModel:band-centers
 * property is set. Read access to them is usually safe as long as the number
 * of bands is not changed after one of the pointers has been obtained. The
 * data should not be written to directly, though.
 */
struct _PeaqEarModel
{
  /*< public >*/
  GObject parent;
  guint band_count;
  gdouble *fc;
  gdouble *internal_noise;
  gdouble *ear_time_constants;
  gdouble *excitation_threshold;
  gdouble *threshold;
  gdouble *loudness_factor;
};

/**
 * PeaqEarModelClass:
 * @parent: The parent #GObjectClass.
 * @frame_size: The size in samples of one frame to process with
 * peaq_earmodel_process_block()/@process_block.
 * @step_size: The step size in samples to progress between successive
 * invocations of peaq_earmodel_process_block()/@process_block.
 * @loudness_scale: The frequency independent loudness scaling
 * (<inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>const</mi>
 * </math></inlineequation>
 * in <xref linkend="BS1387" />, <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><mi>c</mi>
 * </math></inlineequation>
 * in <xref linkend="Kabal03" />).
 * @tau_min: Parameter <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>&tau;</mi><mi>min</mi></msub>
 * </math></inlineequation>
 * of the time domain spreading filter.
 * @tau_100: Parameter <inlineequation><math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>&tau;</mi><mn>100</mn></msub>
 * </math></inlineequation>
 * of the time domain spreading filter.
 * @get_playback_level: Function to call when the
 * #PeaqEarModel:playback-level property is read, has to return the current
 * playback level in dB.
 * @set_playback_level: Function to call when the
 * #PeaqEarModel:playback-level property is set to do any necessary parameter
 * adjustments.
 * @state_alloc: Function to allocate instance state data, called by
 * peaq_earmodel_state_alloc().
 * @state_free: Function to deallocate instance state data, called by
 * peaq_earmodel_state_free().
 * @process_block: Function to process one block of data, called by
 * peaq_earmodel_process_block().
 * @get_excitation: Function to obtain the current excitation from the state,
 * called by peaq_earmodel_get_excitation().
 * @get_unsmeared_excitation: Function to obtain the current unsmeared
 * excitation from the state, called by
 * peaq_earmodel_get_unsmeared_excitation().
 *
 * Derived classes must provide values for all fields of #PeaqEarModelClass
 * (except for <structfield>parent</structfield>).
 */
struct _PeaqEarModelClass
{
  GObjectClass parent;
  guint frame_size;
  guint step_size;
  gdouble loudness_scale;
  gdouble tau_min;
  gdouble tau_100;
  gdouble (*get_playback_level) (PeaqEarModel const *model);
  void (*set_playback_level) (PeaqEarModel *model, gdouble level);
  gpointer (*state_alloc) (PeaqEarModel const *model);
  void (*state_free) (PeaqEarModel const *model, gpointer state);
  void (*process_block) (PeaqEarModel const *model, gpointer state,
                         gfloat const *samples);
  gdouble const *(*get_excitation) (PeaqEarModel const *model, gpointer state);
  gdouble const *(*get_unsmeared_excitation) (PeaqEarModel const *model,
                                              gpointer state);
};

GType peaq_earmodel_get_type ();
gpointer peaq_earmodel_state_alloc (PeaqEarModel const *model);
void peaq_earmodel_state_free (PeaqEarModel const *model, gpointer state);
void peaq_earmodel_process_block (PeaqEarModel const *model, gpointer state,
                                  gfloat const *samples);
gdouble const *peaq_earmodel_get_excitation (PeaqEarModel const *model,
                                             gpointer state);
gdouble const *peaq_earmodel_get_unsmeared_excitation (PeaqEarModel const *model,
                                                       gpointer state);
guint peaq_earmodel_get_band_count (PeaqEarModel const *model);
guint peaq_earmodel_get_sampling_rate (PeaqEarModel const *model);
guint peaq_earmodel_get_frame_size (PeaqEarModel const *model);
guint peaq_earmodel_get_step_size (PeaqEarModel const *model);
gdouble peaq_earmodel_get_band_center_frequency (PeaqEarModel const *model,
                                                 guint band);
gdouble peaq_earmodel_get_internal_noise (PeaqEarModel const *model,
                                          guint band);
gdouble peaq_earmodel_get_ear_time_constant (PeaqEarModel const *model,
                                             guint band);
gdouble peaq_earmodel_calc_time_constant (PeaqEarModel const *model,
                                          guint band,
                                          gdouble tau_min, gdouble tau_100);
gdouble peaq_earmodel_calc_ear_weight (gdouble frequency);
gdouble peaq_earmodel_calc_loudness (PeaqEarModel const *model,
                                     gpointer state);

#ifdef __cplusplus
}
#endif

#endif
