/* GstPEAQ
 * Copyright (C) 2014 Martin Holters <martin.holters@hsuhh.de>
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
 * CLAMP_MOVS:
 *
 * Controls whether the model output variables are clamped to the range [amin,
 * amax] before calulating the distortion index in peaq_calculate_di_basic() or
 * peaq_calculate_di_advanced(). This is proposed in <xref linkend="Kabal03" />
 * but not mentioned at all in <xref linkend="BS1387" />.
 */
#define CLAMP_MOVS 1
