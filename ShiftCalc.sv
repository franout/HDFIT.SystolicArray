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

`include "globals.svh"

// out = f0 * f1 + acc
// (f0 * f1)_exp = f0_exp + f1_exp (before normalization)
// out_exp = Max(f0_exp + f1_exp, acc_exp)
// shiftX = expX - out_exp, i.e. for (X_mant >> shiftX)
// Note:
// 1) Only ">>" shifts will occur (mantissa will always be made smaller or stay same)
module ShiftCalc #(
		parameter MUL_WIDTH
		)
		(
		input exponent_t  expMul0,
		input exponent_t  expMul1,
		input exponent_t  expAcc,
		output logic [$clog2(MUL_WIDTH) - 1 : 0] mulShift,
		output logic [$clog2($bits(accMantNormalSigned_t))-1:0] accShift,
		output exponent_t expOut,
		output logic isInf
		);
	
	typedef logic signed [$bits(exponent_t) + 1:0] signedExtExp_t; // 1 bit for sign and another for overflow
	
	localparam signedExtExp_t bitsMultMant = signedExtExp_t'(2 * $bits(mulMantNormalSigned_t));
	localparam signedExtExp_t bitsAccMant = signedExtExp_t'($bits(accMantNormalSigned_t));
	
	signedExtExp_t expMul;
	logic 	      expMulZero;
	signedExtExp_t mulShiftTmp;
	signedExtExp_t accShiftTmp;
         
	always_comb begin
		// a - EXP_BIAS + b - EXP_BIAS + EXP_BIAS
		expMul = $signed({2'b00, expMul0}) + $signed({2'b00, expMul1}) - $signed({1'b0, EXP_BIAS});
		expMulZero = expMul < 'sd0 ? 1'b1 : 1'b0;
		isInf = ((expMul >= 2**$bits(exponent_t) - 1) ? 1'b1 : 1'b0) | ('1 == expMul0) | ('1 == expMul1) | ('1 == expAcc);
            
		expOut = (expMul > $signed({2'b00, expAcc})) ? expMul[$bits(exponent_t) - 1:0] : expAcc;
      
		mulShiftTmp = $signed({2'b00, expOut}) - expMul;
		mulShift = (expMulZero || (mulShiftTmp > bitsMultMant)) ? bitsMultMant[$bits(mulShift)-1:0] : mulShiftTmp[$bits(mulShift)-1:0];

		accShiftTmp = {1'b0, expOut} - {1'b0, expAcc};
		accShift = (accShiftTmp > bitsAccMant) ? bitsAccMant[$bits(accShift) - 1 : 0] : accShiftTmp[$bits(accShift) - 1 : 0];
	end
	
endmodule
