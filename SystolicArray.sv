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

module SystolicArray #(
		parameter M_MMA = 1,
		parameter K_MMA = 8
		)
		(
		input logic 						 clk,
		input mulNormalSigned_t multLeft[M_MMA - 1 : 0][K_MMA - 1 : 0],
		input mulNormalSigned_t multRight[K_MMA - 1 : 0],
		input accNormalSigned_t acc[M_MMA - 1 : 0],
		output accNormalSigned_t out[M_MMA - 1 : 0],
		output logic 						 error // for cyclic protection
		);
	
	accNormalSigned_t colOut[M_MMA-1:0][1:0];
	
	generate
		for (genvar m_mma = 0; m_mma < M_MMA; m_mma++) begin : fma_m

			accNormalSigned_t fmaAccOut[K_MMA-3:0];
	 
			for (genvar k_mma = 0; k_mma < K_MMA; k_mma++) begin : fma_k
				// The first fmas read Acc directly from input port
				// All other fmas read Acc from preceding fma
				accNormalSigned_t fmaAccIn;
				if(0 == k_mma) begin
					assign fmaAccIn = acc[m_mma];
				end else if (1 == k_mma) begin
					assign fmaAccIn = 0; // TODO: Have simple multiplier
				end else begin
					assign fmaAccIn = fmaAccOut[k_mma-2];
				end
	    
				logic fmaClock;
				assign fmaClock = ((k_mma % 2) != 0) ? ~clk : clk;
				
				accNormalSigned_t fmaOut;	    
				FMA fma (
						.clk(fmaClock),
						.mult1(multLeft[m_mma][k_mma]),
						.mult2(multRight[k_mma]),
						.acc(fmaAccIn),
						.out(fmaOut));
				
				// Next stage flip flops OR
				// Set output port if it's the last column
				if((K_MMA - 1 != k_mma) && (K_MMA - 2 != k_mma)) begin
					`MSFF(fmaAccOut[k_mma], fmaOut, fmaClock);
				end else begin
					`MSFF(colOut[m_mma][k_mma % 2], fmaOut, fmaClock);
				end
			end
		end
	endgenerate
	
	generate
		
		for (genvar m_mma = 0; m_mma < M_MMA; m_mma++) begin : add_m
						
			/* Stage 1 *******************************************************************************************************/
			exponent_t sumUnnormalizedExp;
			logic signed [$bits(accMantNormalSigned_t):0] sumUnnormalizedMant;
			
			Adder adder (
					.in1(colOut[m_mma][0]),
					.in2(colOut[m_mma][1]),
					.sumExp(sumUnnormalizedExp),
					.sumMant(sumUnnormalizedMant));					
			
			// Next stage flip flops
			logic signed [$bits(accMantNormalSigned_t):0] sumUnnormalizedMant_stg2;
			`MSFF(sumUnnormalizedMant_stg2, sumUnnormalizedMant, ~clk);
			
			exponent_t sumUnnormalizedExp_stg2;
			`MSFF(sumUnnormalizedExp_stg2, sumUnnormalizedExp, ~clk);
			
			/* Stage 2 *******************************************************************************************************/
			
			Normalizer normalizer(
					.exp(sumUnnormalizedExp_stg2),
					.mant(sumUnnormalizedMant_stg2),
					.out(out[m_mma]));					
					
		end
	endgenerate

endmodule
