/* GstPEAQ
 * Copyright (C) 2013 Martin Holters <martin.holters@hsuhh.de>
 *
 * movs.h: Model Output Variables.
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

#ifndef __MOVS_H_
#define __MOVS_H_ 1

#include "fftearmodel.h"
#include "leveladapter.h"
#include "modpatt.h"
#include "movaccum.h"

void peaq_mov_modulation_difference (PeaqModulationProcessor* const *ref_mod_proc,
                                     PeaqModulationProcessor* const *test_mod_proc,
                                     PeaqMovAccum *mov_accum1,
                                     PeaqMovAccum *mov_accum2,
                                     PeaqMovAccum *mov_accum_win);
void peaq_mov_noise_loudness (PeaqModulationProcessor * const *ref_mod_proc,
                              PeaqModulationProcessor * const *test_mod_proc,
                              PeaqLevelAdapter * const *level,
                              PeaqMovAccum *mov_accum);
void peaq_mov_noise_loud_asym (PeaqModulationProcessor * const *ref_mod_proc,
                               PeaqModulationProcessor * const *test_mod_proc,
                               PeaqLevelAdapter * const *level,
                               PeaqMovAccum *mov_accum);
void peaq_mov_lin_dist (PeaqModulationProcessor * const *ref_mod_proc,
                        PeaqLevelAdapter * const *level, gpointer *state,
                        PeaqMovAccum *mov_accum);
void peaq_mov_bandwidth (gpointer *ref_state, gpointer *test_state,
                         PeaqMovAccum *mov_accum_ref,
                         PeaqMovAccum *mov_accum_test);
void peaq_mov_nmr (PeaqFFTEarModel const *ear_model, gpointer *ref_state,
                   gpointer *test_state, PeaqMovAccum *mov_accum_nmr,
                   PeaqMovAccum *mov_accum_rel_dist_frames);
void peaq_mov_prob_detect (PeaqEarModel const *ear_model, gpointer *ref_state,
                           gpointer *test_state, PeaqMovAccum *mov_accum_adb,
                           PeaqMovAccum *mov_accum_mfpd);
#endif
