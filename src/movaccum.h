/* GstPEAQ
 * Copyright (C) 2013 Martin Holters <martin.holters@hsuhh.de>
 *
 * movaccum.h: Model out variable (MOV) accumulation.
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


#ifndef __MOVACCUM_H__
#define __MOVACCUM_H__ 1

#include <glib-object.h>

#define PEAQ_TYPE_MOVACCUM (peaq_movaccum_get_type ())
#define PEAQ_MOVACCUM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST (obj, PEAQ_TYPE_MOVACCUM, PeaqMovAccum))
#define PEAQ_MOVACCUM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST (klass, PEAQ_TYPE_MOVACCUM, PeaqMovAccumClass))
#define PEAQ_IS_MOVACCUM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE (obj, PEAQ_TYPE_MOVACCUM))
#define PEAQ_IS_MOVACCUM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE (klass, PEAQ_TYPE_MOVACCUM))
#define PEAQ_MOVACCUM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS (obj, PEAQ_TYPE_MOVACCUM, PeaqMovAccumClass))

typedef struct _PeaqMovAccumClass PeaqMovAccumClass;
typedef struct _PeaqMovAccum PeaqMovAccum;
typedef enum _PeaqMovAccumMode PeaqMovAccumMode;

enum _PeaqMovAccumMode
{
  MODE_AVG,
  MODE_AVG_LOG,
  MODE_RMS,
  MODE_AVG_WINDOW,
  MODE_FILTERED_MAX,
  MODE_ADB
};

GType peaq_movaccum_get_type ();
PeaqMovAccum *peaq_movaccum_new ();
void peaq_movaccum_set_mode (PeaqMovAccum *acc, PeaqMovAccumMode mode);
void peaq_movaccum_accumulate (PeaqMovAccum *acc, guint c, gdouble val,
                               gboolean tentative);
void peaq_movaccum_accumulate_weighted (PeaqMovAccum *acc, guint c,
                                        gdouble val, gdouble weight,
                                        gboolean tentative);
gdouble peaq_movaccum_get_value (PeaqMovAccum *acc, guint channels);

#endif
