/*
 * Copyright (C) 2022 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License, as published
 * by the Free Software Foundation; either version 3 of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

`ifndef GLOBALS_SVH_  // guard
	`define GLOBALS_SVH_
	
	parameter EXP_BIAS =  11'sd1023;
	parameter EXP_MAX = 11'd2047;
	typedef logic [10:0] exponent_t; // -1023 biased exponent
		
	// Separate typedef for acc and mul mantissa for future fp32 * fp32 + fp64 = fp64 extension
	typedef logic signed [51 + 2:0] accMantNormalSigned_t; // Signed mantissa with leading 1
	typedef logic signed [51 + 2:0] mulMantNormalSigned_t;  // Signed mantissa with leading 1
	
	typedef struct packed {
		exponent_t Exp;
		accMantNormalSigned_t Mant;
	} accNormalSigned_t;

	typedef struct packed {
		exponent_t Exp;
		mulMantNormalSigned_t Mant;
	} mulNormalSigned_t;
	
	typedef logic signed [2 * $bits(mulMantNormalSigned_t) - 1: 0] mulOut_t;
		
`endif // guard GLOBALS_SVH_
