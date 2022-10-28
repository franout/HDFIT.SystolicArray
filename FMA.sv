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
`include "msFlipFlop.svh"


module FMA (
		input logic 		     clk,
		input mulNormalSigned_t mult1,
		input mulNormalSigned_t  mult2,
		input accNormalSigned_t  acc,
		output accNormalSigned_t out
		);
	
	
	// 4 Stage PPA
	logic signed [$bits(mult1.Mant):0] ppa_stg5; // one bit larger
	PartialProductArrayCSA #(.MULT_WIDTH($bits(mult1.Mant))) ppaInst (
			.clk(clk),
			.mul1(mult1.Mant), .mul2(mult2.Mant),
			.out(ppa_stg5));
	
	/* 1. stage ****************************************************************************************/
	// Calculate shift for floats
	logic [$clog2($bits(mulOut_t)) - 1 : 0] mulShift;
	logic [$clog2($bits(accMantNormalSigned_t))-1:0] accShift;
	exponent_t 			     expOut;
	logic 			     isInf;
      
	ShiftCalc #(.MUL_WIDTH($bits(mulOut_t))) shiftCalc
		(
			.expMul0(mult1.Exp),
			.expMul1(mult2.Exp),
			.expAcc(acc.Exp),
			.mulShift(mulShift),
			.accShift(accShift),
			.expOut(expOut),
			.isInf(isInf)
		);

	// Stage 1. -> 2.
	accMantNormalSigned_t 					   acc_stg2;
	logic [$clog2($bits(mulOut_t)) - 1 : 0] mulShift_stg2;
	logic [$clog2($bits(accMantNormalSigned_t))-1:0] accShift_stg2;
	exponent_t							   expOut_stg2;
	logic 							   isInf_stg2;
		
	`MSFF(acc_stg2, acc.Mant, clk);
	`MSFF(mulShift_stg2, mulShift, clk);
	`MSFF(accShift_stg2, accShift, clk);
	`MSFF(expOut_stg2, expOut, clk);
	`MSFF(isInf_stg2, isInf, clk);
	
	/* Forward 1. to 5. stage to wait for PPA ************************************************************/
	
	// Stage 2. -> 3.
	accMantNormalSigned_t 					   acc_stg3;
	logic [$clog2($bits(mulOut_t)) - 1 : 0] mulShift_stg3;
	logic [$clog2($bits(accMantNormalSigned_t))-1:0] accShift_stg3;
	exponent_t							   expOut_stg3;
	logic 							   isInf_stg3;
		
	`MSFF(acc_stg3, acc_stg2, clk);
	`MSFF(mulShift_stg3, mulShift_stg2, clk);
	`MSFF(accShift_stg3, accShift_stg2, clk);
	`MSFF(expOut_stg3, expOut_stg2, clk);
	`MSFF(isInf_stg3, isInf_stg2, clk);
	
	// Stage 3. -> 4.
	accMantNormalSigned_t 					   acc_stg4;
	logic [$clog2($bits(mulOut_t)) - 1 : 0] mulShift_stg4;
	logic [$clog2($bits(accMantNormalSigned_t))-1:0] accShift_stg4;
	exponent_t							   expOut_stg4;
	logic 							   isInf_stg4;
		
	`MSFF(acc_stg4, acc_stg3, clk);
	`MSFF(mulShift_stg4, mulShift_stg3, clk);
	`MSFF(accShift_stg4, accShift_stg3, clk);
	`MSFF(expOut_stg4, expOut_stg3, clk);
	`MSFF(isInf_stg4, isInf_stg3, clk);
	
	// Stage 4. -> 5.
	accMantNormalSigned_t 					   acc_stg5;
	logic [$clog2($bits(mulOut_t)) - 1 : 0] mulShift_stg5;
	logic [$clog2($bits(accMantNormalSigned_t))-1:0] accShift_stg5;
	exponent_t							   expOut_stg5;
	logic 							   isInf_stg5;
		
	`MSFF(acc_stg5, acc_stg4, clk);
	`MSFF(mulShift_stg5, mulShift_stg4, clk);
	`MSFF(accShift_stg5, accShift_stg4, clk);
	`MSFF(expOut_stg5, expOut_stg4, clk);
	`MSFF(isInf_stg5, isInf_stg4, clk);
	
	/* 5. stage ****************************************************************************************/
	
	logic signed [$bits(accMantNormalSigned_t) + 1:0] mantOut; // one bit larger for sum
	
	FmaAdder fmaAdder(
			.in1(ppa_stg5),
			.in1Shift(mulShift_stg5),
			.in2(acc_stg5),
			.in2Shift(accShift_stg5),
			.out(mantOut)			
		);
	
	// Next stage flip flops
	logic signed [$bits(accMantNormalSigned_t) + 1:0] mantOut_stg6;
	exponent_t expOut_stg6;
	logic 							   isInf_stg6;
      
	`MSFF(mantOut_stg6, mantOut, clk);
	`MSFF(expOut_stg6, expOut_stg5, clk);
	`MSFF(isInf_stg6, isInf_stg5, clk);
	
	/* 6. stage ****************************************************************************************/
	
	FmaNormalizer fmaNormalizer(
			.isInf(isInf_stg6),
			.exp(expOut_stg6),
			.mant(mantOut_stg6),
			.out(out));			
  
endmodule
