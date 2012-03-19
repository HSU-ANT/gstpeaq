/* GstPEAQ
 * Copyright (C) 2006 Martin Holters <martin.holters@hsuhh.de>
 *
 * leveladapter.h: Level and pattern adaptation.
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


#ifndef __LEVELADAPTER_H__
#define __LEVELADAPTER_H__ 1

#include "earmodel.h"
#include <glib-object.h>

#define PEAQ_TYPE_LEVELADAPTER (peaq_leveladapter_get_type ())
#define PEAQ_LEVELADAPTER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST (obj, PEAQ_TYPE_LEVELADAPTER, PeaqLevelAdapter))
#define PEAQ_LEVELADAPTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST (klass, PEAQ_TYPE_LEVELADAPTER, PeaqLevelAdapterClass))
#define PEAQ_IS_LEVELADAPTER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE (obj, PEAQ_TYPE_LEVELADAPTER))
#define PEAQ_IS_LEVELADAPTER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE (klass, PEAQ_TYPE_LEVELADAPTER))
#define PEAQ_LEVELADAPTER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS (obj, PEAQ_TYPE_LEVELADAPTER, \
			      PeaqLevelAdapterClass))

typedef struct _LevelAdapterOutput
{
  gdouble *spectrally_adapted_ref_patterns;
  gdouble *spectrally_adapted_test_patterns;
} LevelAdapterOutput;

typedef struct _PeaqLevelAdapterClass PeaqLevelAdapterClass;
typedef struct _PeaqLevelAdapter PeaqLevelAdapter;

GType peaq_leveladapter_get_type ();
PeaqLevelAdapter *peaq_leveladapter_new (PeaqEarModel *ear_model);
void peaq_leveladapter_set_ear_model (PeaqLevelAdapter *level,
                                      PeaqEarModel *ear_model);
void peaq_leveladapter_process (PeaqLevelAdapter * level,
				gdouble * ref_exciation,
				gdouble * test_exciation,
				LevelAdapterOutput * output);
#endif
