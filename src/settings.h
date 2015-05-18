/* GstPEAQ
 * Copyright (C) 2014, 2015 Martin Holters <martin.holters@hsuhh.de>
 *
 * settings.h: Settings controlling behaviour not sufficiently specified in the
 * standard.
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
 * SECTION:settings
 * @short_description: Settings controlling behaviour not sufficiently
 * specified in the standard.
 * @title: Compile time settings
 */

/**
 * SWAP_MOD_PATTS_FOR_NOISE_LOUDNESS_MOVS:
 *
 * Controls whether the modulation patterns are exchanged along with excitation
 * patters for RmsMssingComponentsA in peaq_mov_noise_loud_asym() and
 * AvgLinDistA n peaq_mov_lin_dist(). While <xref linkend="BS1387" /> only
 * describes changing the excitation patterns, in <xref linkend="Kabal03" />
 * the modulation patterns are also exchanged accordingly. Set to zero or
 * undefine to take <xref linkend="BS1387" /> literally, set to some non-zero
 * value to use the interpretation from <xref linkend="Kabal03" />.
 */
#define SWAP_MOD_PATTS_FOR_NOISE_LOUDNESS_MOVS 1

/**
 * CENTER_EHS_CORRELATION_WINDOW:
 *
 * Controls whether the Hann window applied to the correlation when computing
 * the error harmonic structure in peaq_mov_ehs() is centered at bin zero as
 * proposed in <xref linkend="Kabal03" />.
 */
#define CENTER_EHS_CORRELATION_WINDOW 0

/**
 * EHS_SUBTRACT_DC_BEFORE_WINDOW:
 *
 * Controls whether the DC component is removed from the correlation before (as
 * proposed by <xref linkend="Kabal03" />) or after (as described in <xref
 * linkend="Kabal03" />) applying the Hann window to the correlation when
 * computing the error harmonic structure in peaq_mov_ehs().
 */
#define EHS_SUBTRACT_DC_BEFORE_WINDOW 1

/**
 * USE_FLOOR_FOR_STEPS_ABOVE_THRESHOLD:
 *
 * Controls whether the INT operation used in the calculation of the steps
 * above threshold in peaq_mov_prob_detect() is implemented as trunc() (which
 * is the usual meaning) or as floor() (which according to <xref
 * linkend="Kabal03" /> makes more sense).
 */
#define USE_FLOOR_FOR_STEPS_ABOVE_THRESHOLD 0

/**
 * CLAMP_MOVS:
 *
 * Controls whether the model output variables are clamped to the range [amin,
 * amax] before calulating the distortion index in peaq_calculate_di_basic() or
 * peaq_calculate_di_advanced(). This is proposed in <xref linkend="Kabal03" />
 * but not mentioned at all in <xref linkend="BS1387" />.
 */
#define CLAMP_MOVS 0
    
/** 
 * SWAP_SLOPE_FILTER_COEFFICIENTS:
 *
 * Controls whether the coefficients of time smoothing filter for the frequency
 * domain spreading slopes in the filterbank-based ear model are swapped
 * compared to the pseudo code of <xref linkend="BS1387" />. As remarked
 * in <xref linkend="Kabal03" />, the pseudo code is incosistent with the
 * textual description and results in an extremely short time constant.
 */ 
#define SWAP_SLOPE_FILTER_COEFFICIENTS 0
