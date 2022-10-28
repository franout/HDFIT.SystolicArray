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

/**
 * Module: PartialProductArrayCSA
 * 
 * TODO: Add module documentation
 */
module PartialProductArrayCSA #(parameter MULT_WIDTH)
		(
		input logic 						 clk,
		input logic signed [MULT_WIDTH-1:0] mul1,
		input logic signed [MULT_WIDTH-1:0] mul2,
		output logic signed [MULT_WIDTH:0] out
		);
	
		
	/* Stage 1 **************************************************************************************************************************/
	
	// No infinite precision, so only carry out operation for bits that may carry up
	// Create PPA
	// See e.g. https://en.wikipedia.org/wiki/Binary_multiplier#Signed_integers
	// without leading one in first and last row
	// int9 * int8 example:
	// 												          	~p0[7]  p0[6]  p0[5]  p0[4]  p0[3]  p0[2]  p0[1]  p0[0]
	//										             ~p1[7] +p1[6] +p1[5] +p1[4] +p1[3] +p1[2] +p1[1] +p1[0]   
	//								              ~p2[7] +p2[6] +p2[5] +p2[4] +p2[3] +p2[2] +p2[1] +p2[0]         
	//						               ~p3[7] +p3[6] +p3[5] +p3[4] +p3[3] +p3[2] +p3[1] +p3[0]               
	//					            ~p4[7] +p4[6] +p4[5] +p4[4] +p4[3] +p4[2] +p4[1] +p4[0]                     
	//                       ~p5[7] +p5[6] +p5[5] +p5[4] +p5[3] +p5[2] +p5[1] +p5[0]                          
	//	              ~p6[7] +p6[6] +p6[5] +p6[4] +p6[3] +p6[2] +p6[1] +p6[0]  
	//	       ~p7[7] +p7[6] +p7[5] +p7[4] +p7[3] +p7[2] +p7[1] +p7[0]  
	//	+p8[7] ~p8[6] ~p8[5] ~p8[4] ~p8[3] ~p8[2] ~p8[1] ~p8[0]
	
	logic [MULT_WIDTH-1:0] ppaRows[MULT_WIDTH];
	generate
		for (genvar row = 0; row < MULT_WIDTH; row++) begin : ppa
			if(0 == row) 					always_comb ppaRows[row] = {~(mul1[row] & mul2[MULT_WIDTH-1]),   {(MULT_WIDTH - 1){mul1[row]}} & mul2[MULT_WIDTH-2:0]};
			else if(MULT_WIDTH - 1 == row) 	always_comb ppaRows[row] = {  mul1[row] & mul2[MULT_WIDTH-1] , ~({(MULT_WIDTH - 1){mul1[row]}} & mul2[MULT_WIDTH-2:0])};   					
			else 							always_comb ppaRows[row] = {~(mul1[row] & mul2[MULT_WIDTH-1]),   {(MULT_WIDTH - 1){mul1[row]}} & mul2[MULT_WIDTH-2:0]};
			
			//always_comb $display("ppa[%d] = %b", row, {{(MULT_WIDTH - row){1'b0}}, ppaRows[row]});
		end
	endgenerate
	
	// First row of CSAs
	// TODO: Don't generate last two bits for first CSA as they can't carry up
	// TODO: Could optimize some CSA bits as the 1'bX introduced are of course known
	// See https://en.wikipedia.org/wiki/Carry-save_adder
	// Sum three consecutive PPA rows to produce
	//                                           ps0[9] ps0[8] ps0[7] ps0[6] ps0[5] ps0[4] ps0[3] ps0[2] ps0[1] ps0[0]
	//                                           sc0[7] sc0[6] sc0[5] sc0[4] sc0[3] sc0[2] sc0[1] sc0[0]
	//                      ps1[9] ps1[8] ps1[7] ps1[6] ps1[5] ps1[4] ps1[3] ps1[2] ps1[1] ps1[0]
	//                      sc1[7] sc1[6] sc1[5] sc1[4] sc1[3] sc1[2] sc1[1] sc1[0]
	// ps2[9] ps2[8] ps2[7] ps2[6] ps2[5] ps2[4] ps2[3] ps2[2] ps2[1] ps2[0]
	// sc2[7] sc2[6] sc2[5] sc2[4] sc2[3] sc2[2] sc2[1] sc2[0]
	
	logic [MULT_WIDTH + 1:0] csaRowsPs[MULT_WIDTH / 3]; // Requires two extra bits
	logic [MULT_WIDTH - 1:0] csaRowsSc[MULT_WIDTH / 3]; // No extra bit as the MSB/LSB can't create a carry
	generate
		for (genvar row = 0; row < MULT_WIDTH; row += 3) begin : csa
			logic signed [MULT_WIDTH - 1:0] a;
			logic signed [MULT_WIDTH - 1:0] b;
			logic signed [MULT_WIDTH - 1:0] c;
			
			if(0 == row) always_comb a = {1'b1, ppaRows[row][MULT_WIDTH-1:1]}; // add leading one & handle LSB separately
			else         always_comb a = {1'b0, ppaRows[row][MULT_WIDTH-1:1]};
			always_comb b =              {      ppaRows[row + 1]};
			always_comb c =              {      ppaRows[row + 2][MULT_WIDTH-2:0], 1'b0}; // handle MSB separately				

			always_comb csaRowsPs[row / 3][0] = ppaRows[row][0]; // Handling LSB separately
			always_comb csaRowsPs[row / 3][MULT_WIDTH:1] = a ^ b ^ c;
			always_comb csaRowsPs[row / 3][MULT_WIDTH + 1] = ppaRows[row + 2][MULT_WIDTH-1]; // Handling MSB separately
			
			always_comb csaRowsSc[row / 3] = (a & b) | (a & c) | (b & c);
			
				/*
			always_comb begin
				$display("csa1 %d %b", row / 3, {{((MULT_WIDTH / 3 - 1 - row / 3) * 3){1'b0}}, csaRowsPs[row / 3]});
				$display("csa1 %d %b", row / 3, {{((MULT_WIDTH / 3 - 1 - row / 3) * 3){1'b0}}, csaRowsSc[row / 3]});
			end
				 */				
		end		
	endgenerate
	
	
	// Sum the CSA rows to create
	//                                                   sum0[10] sum0[9] sum0[8] sum0[7] sum0[6] sum0[5] sum0[4] sum0[3] sum0[2] sum0[1] sum0[0] 
	//                          sum1[10] sum1[9] sum1[8] sum1[ 7] sum1[6] sum1[5] sum1[4] sum1[3] sum1[2] sum1[1] sum1[0] 
	// sum2[10] sum2[9] sum2[8] sum2[ 7] sum2[6] sum2[5] sum2[ 4] sum2[3] sum2[2] sum2[1] sum2[0]
	// ...
	logic [MULT_WIDTH + 2:0] csaRowsSum[MULT_WIDTH / 3]; // One extra bit for carry
	generate
		for (genvar row = 0; row < MULT_WIDTH / 3; row++) begin : csaSum
			always_comb csaRowsSum[row][1:0] = csaRowsPs[row][1:0]; // Handle two LSBs separately
			always_comb csaRowsSum[row][MULT_WIDTH + 2:2] = {1'b0, csaRowsPs[row][MULT_WIDTH + 1:2]} + {1'b0, csaRowsSc[row]};
			
				//always_comb $display("csaSum %d %b", row, {{((MULT_WIDTH / 3 - 1 - row) * 3){1'b0}}, csaRowsSum[row]});
		end		
	endgenerate
	
	// Next stage flip flops
	logic [MULT_WIDTH + 2:0] csaRowsSum_stg2[MULT_WIDTH / 3];
	
	generate
		for(genvar row = 0; row < MULT_WIDTH / 3; row++) begin: csaFF
			`MSFF(csaRowsSum_stg2[row], csaRowsSum[row], clk);
		end
	endgenerate
	
	/* Stage 2 **************************************************************************************************************************/
	
	// CSA for above csaRowsSum to create
	//                                                                                      sc[10] sc[ 9] sc[ 8]  sc[ 7]   sc[ 6] sc[5]   sc[4]   sc[3]   sc[2]   sc[1]   sc[0]
	//                                                                       ps[16]  ps[15] ps[14] ps[13] ps[12]  ps[11]   ps[10] ps[9]   ps[8]   ps[7]   ps[6]   ps[5]   ps[4]   ps[3]   ps[2]   ps[1]   ps[0]
	//                  sc[10] sc[ 9] sc[ 8]  sc[ 7]   sc[ 6]  sc[ 5] sc[ 4] sc[ 3] sc[ 2]  sc[ 1]   sc[ 0]
	//   ps[16]  ps[15] ps[14] ps[13] ps[12]  ps[11]   ps[10]  ps[ 9] ps[ 8] ps[ 7] ps[ 6]  ps[ 5]   ps[ 4] ps[3]   ps[2]   ps[1]   ps[0]
	// ...
	logic [MULT_WIDTH + 8:0] csa2Ps[MULT_WIDTH / 9]; // 6 extra bits
	logic [MULT_WIDTH + 2:0] csa2Sc[MULT_WIDTH / 9]; // no extra bit required
	generate
		for (genvar row = 0; row < MULT_WIDTH / 3; row += 3) begin : csa2
			logic signed [MULT_WIDTH + 2:0] a;
			logic signed [MULT_WIDTH + 2:0] b;
			logic signed [MULT_WIDTH + 2:0] c;
			
			always_comb a = {3'b000, csaRowsSum_stg2[row][MULT_WIDTH + 2:3]}; // Handle three LSBs separately
			always_comb b =          csaRowsSum_stg2[row + 1];
			always_comb c = {        csaRowsSum_stg2[row + 2][MULT_WIDTH - 1 : 0], 3'b000}; // Handle three MSBs separately
			
			always_comb csa2Ps[row / 3][2:0] = csaRowsSum_stg2[row][2:0];
			always_comb csa2Ps[row / 3][MULT_WIDTH + 5 : 3] = a ^ b ^ c; 
			always_comb csa2Ps[row / 3][MULT_WIDTH + 8: MULT_WIDTH + 6] = csaRowsSum_stg2[row + 2][MULT_WIDTH + 2 : MULT_WIDTH];
			
			always_comb csa2Sc[row / 3] = (a & b) | (a & c) | (b & c);
			
				/*
			always_comb begin
				$display("csa2 %d %b", row / 3, {{((MULT_WIDTH / 9 - row / 3) * 2 + 2){1'b0}}, csa2Sc[row/3]});
				$display("csa2 %d %b", row / 3, {{((MULT_WIDTH / 9 - row / 3) * 2){1'b0}}, csa2Ps[row/3]});
			end
				 */
			
		end		
	endgenerate
	
	// Sum the CSA rows to create
	//
	// {         9'd0,         9'd0,         9'd0,         9'd0,         9'd0, sum[0][17:9], sum[0][8:0]}
	// {         9'd0,         9'd0,         9'd0,         9'd0, sum[1][17:9], sum[1][ 8:0],        9'd0}
	// {         9'd0,         9'd0,         9'd0, sum[2][17:9], sum[2][ 8:0],         9'd0,        9'd0}
	// {         9'd0,         9'd0, sum[3][17:9], sum[3][ 8:0],         9'd0,         9'd0,        9'd0}
	// {         9'd0, sum[4][17:9], sum[4][ 8:0],         9'd0,         9'd0,         9'd0,        9'd0}
	// { sum[5][17:9], sum[5][ 8:0],         9'd0,         9'd0,         9'd0,         9'd0,        9'd0}
	logic [MULT_WIDTH + 9:0] csa2RowsSum[MULT_WIDTH / 9]; // One extra bit for carry
	generate
		for (genvar row = 0; row < MULT_WIDTH / 9; row++) begin : csa2Sum
			always_comb csa2RowsSum[row][3:0] = csa2Ps[row][3:0]; // Handle four LSBs separately
			always_comb csa2RowsSum[row][MULT_WIDTH + 9:4] = {1'b0, csa2Ps[row][MULT_WIDTH + 8:4]} + {3'b0, csa2Sc[row]};
			
				// always_comb $display("csa2RowsSum %d %b", row, {{((MULT_WIDTH / 9 - row) * 9){1'b0}}, csa2RowsSum[row], {(row * 9){1'b0}}});
		end		
	endgenerate
	
	/*	
	always_comb $display("csa2RowsSum %b",
			{{((MULT_WIDTH / 9 - 0) * 9){1'b0}}, csa2RowsSum[0], {(0 * 9){1'b0}}} +
			{{((MULT_WIDTH / 9 - 1) * 9){1'b0}}, csa2RowsSum[1], {(1 * 9){1'b0}}} +
			{{((MULT_WIDTH / 9 - 2) * 9){1'b0}}, csa2RowsSum[2], {(2 * 9){1'b0}}} + 
			{{((MULT_WIDTH / 9 - 3) * 9){1'b0}}, csa2RowsSum[3], {(3 * 9){1'b0}}} + 
			{{((MULT_WIDTH / 9 - 4) * 9){1'b0}}, csa2RowsSum[4], {(4 * 9){1'b0}}} + 
			{{((MULT_WIDTH / 9 - 5) * 9){1'b0}}, csa2RowsSum[5], {(5 * 9){1'b0}}});
	 */	
	
	// Next stage flip flops
	logic [MULT_WIDTH + 9:0] csa2RowsSum_stg3[MULT_WIDTH / 9];
	
	generate
		for(genvar row = 0; row < MULT_WIDTH / 9; row++) begin: csa2FF
			`MSFF(csa2RowsSum_stg3[row], csa2RowsSum[row], clk);
		end
	endgenerate

	/* Stage 3 **************************************************************************************************************************/
			
	// CSA for above rows to create
	// sc[0] << 10
	// ps[0] 
	// sc[1] << 37
	// ps[1] << 27
	// that is (now for 54 bit width)
	//         [107:100]    [99:81]      [80:73]       [72:37]       [36:27]       [27:10]      [9:0]
	//          8'b          19'b           8'b          36'b          10'b          17'b        10'b
	//
	// {                                    8'd0, sc[0][62:27], sc[0][26:17], sc[0][16: 0],      10'd0
	// {        8'd0,        19'd0, ps[0][80:73], ps[0][72:37], ps[0][36:27], ps[0][26:10], ps[0][9:0]
	// {        8'd0, sc[1][62:44], sc[1][43:36], sc[1][35: 0],        10'd0,        17'd0,      10'd0
	// {ps[1][80:73], ps[1][72:54], ps[1][53:46], ps[1][45:10], ps[1][ 9: 0],        17'd0,      10'd0
		
	logic [MULT_WIDTH + 27:0] csa3Ps[MULT_WIDTH / 27]; // 18 extra bits
	logic [MULT_WIDTH + 9:0] csa3Sc[MULT_WIDTH / 27]; // no extra bits required
	generate
		for (genvar row = 0; row < MULT_WIDTH / 9; row += 3) begin : csa3
			logic signed [MULT_WIDTH + 9:0] a;
			logic signed [MULT_WIDTH + 9:0] b;
			logic signed [MULT_WIDTH + 9:0] c;
			
			always_comb a = {9'd0, csa2RowsSum_stg3[row][MULT_WIDTH + 9:9]}; // Handle nine LSBs separately
			always_comb b =        csa2RowsSum_stg3[row + 1];
			always_comb c = {      csa2RowsSum_stg3[row + 2][MULT_WIDTH : 0], 9'd0}; // Handle nine MSBs separately
			
			always_comb csa3Ps[row / 3][8:0] = csa2RowsSum_stg3[row][8:0];
			always_comb csa3Ps[row / 3][MULT_WIDTH + 18 : 9] = a ^ b ^ c; 
			always_comb csa3Ps[row / 3][MULT_WIDTH + 27 : MULT_WIDTH + 19] = csa2RowsSum_stg3[row + 2][MULT_WIDTH + 9 : MULT_WIDTH + 1];
			
			always_comb csa3Sc[row / 3] = (a & b) | (a & c) | (b & c);
		end		
	endgenerate
	
	/*
	always_comb begin
		$display("csa3 0 %b", {            8'd0,            19'd0,             8'd0, csa3Sc[0][62:27], csa3Sc[0][26:17], csa3Sc[0][16: 0],          10'd0});
		$display("csa3 1 %b", {            8'd0,            19'd0, csa3Ps[0][80:73], csa3Ps[0][72:37], csa3Ps[0][36:27], csa3Ps[0][26:10], csa3Ps[0][9:0]});
		$display("csa3 2 %b", {            8'd0, csa3Sc[1][62:44], csa3Sc[1][43:36], csa3Sc[1][35: 0],            10'd0,            17'd0,          10'd0});
		$display("csa3 3 %b", {csa3Ps[1][80:73], csa3Ps[1][72:54], csa3Ps[1][53:46], csa3Ps[1][45:10], csa3Ps[1][ 9: 0],            17'd0,          10'd0});
	
		$display("csa3 %b",
				{            8'd0,            19'd0,             8'd0, csa3Sc[0][62:27], csa3Sc[0][26:17], csa3Sc[0][16: 0],          10'd0} + 
				{            8'd0,            19'd0, csa3Ps[0][80:73], csa3Ps[0][72:37], csa3Ps[0][36:27], csa3Ps[0][26:10], csa3Ps[0][9:0]} + 
				{            8'd0, csa3Sc[1][62:44], csa3Sc[1][43:36], csa3Sc[1][35: 0],            10'd0,            17'd0,          10'd0} + 
				{csa3Ps[1][80:73], csa3Ps[1][72:54], csa3Ps[1][53:46], csa3Ps[1][45:10], csa3Ps[1][ 9: 0],            17'd0,          10'd0});
	end
	 */
	
			
	// Another CSA for [95:10]
	// (Ignore ps[0][9:0] as it can't carry up)
	// We then have
	//          8'b                 18'b              45'b            26'b
	//        [96:89]             [88:71]           [70:26]           [25:0]
	//
	// {            8'd0,            18'd0,    csa4Sc[70:26],    csa4Sc[25:0]
	// {            8'd0,    csa4Ps[89:72],    csa4Ps[71:27],    csa4Ps[26:1] // last bit can't carry
	// {csa3Ps[1][80:73], csa3Ps[1][72:55], csa3Ps[1][54:10],           26'd0
	
	logic [89:0] csa4Ps;
	logic [70:0] csa4Sc;
	
	logic [70:0] csa4A;
	logic [70:0] csa4B;
	logic [70:0] csa4C;
	
	always_comb csa4A = {            8'd0, csa3Sc[0][62:27], csa3Sc[0][26:17], csa3Sc[0][16: 0]};
	always_comb csa4B = {csa3Ps[0][80:73], csa3Ps[0][72:37], csa3Ps[0][36:27], csa3Ps[0][26:10]};
	always_comb csa4C = {csa3Sc[1][43:36], csa3Sc[1][35: 0], csa3Ps[1][ 9: 0],            17'd0};
	
	always_comb csa4Ps[89:71] = csa3Sc[1][62:44]; // handle 19 MSBs separately - can't create a carry
	always_comb csa4Ps[70: 0] = csa4A ^ csa4B ^ csa4C;
	
	always_comb csa4Sc = (csa4A & csa4B) | (csa4A & csa4C) | (csa4B & csa4C);
		/*	
	always_comb begin
		$display("");
		$display("csa4 0 %b", {            8'd0,            18'd0,    csa4Sc[70:26],    csa4Sc[25:0]});
		$display("csa4 1 %b", {            8'd0,    csa4Ps[89:72],    csa4Ps[71:27],    csa4Ps[26:1]});
		$display("csa4 2 %b", {csa3Ps[1][80:73], csa3Ps[1][72:55], csa3Ps[1][54:10],           26'd0});

		$display("csa4 %b",
				{            8'd0,            18'd0,    csa4Sc[70:26],    csa4Sc[25:0]} +
				{            8'd0,    csa4Ps[89:72],    csa4Ps[71:27],    csa4Ps[26:1]} +
				{csa3Ps[1][80:73], csa3Ps[1][72:55], csa3Ps[1][54:10],           26'd0});
		$display("");
	end
		 */
	
		// Another CSA for all three rows to create
		//
		// {         7'd0, csa5Sc[88: 0]
		// {csa5Ps[96:90], csa5Ps[89: 1] // cut off last bit
	
	logic [96:0] csa5Ps;
	logic [88:0] csa5Sc;
	
	logic [88:0] csa5A;
	logic [88:0] csa5B;
	logic [88:0] csa5C;
	
	always_comb csa5A = {           18'd0,    csa4Sc[70:26],    csa4Sc[25:0]};
	always_comb csa5B = {   csa4Ps[89:72],    csa4Ps[71:27],    csa4Ps[26:1]};
	always_comb csa5C = {csa3Ps[1][72:55], csa3Ps[1][54:10],           26'd0};
	
	always_comb csa5Ps[96:89] = csa3Ps[1][80:73]; // Handle 8 MSBs separately
	always_comb csa5Ps[88:0] = csa5A ^ csa5B ^ csa5C;
	
	always_comb csa5Sc = (csa5A & csa5B) | (csa5A & csa5C) | (csa5B & csa5C);
	
	always_comb begin
		/*
		$display("csa5 0 %b", {         7'd0, csa5Sc[88: 0]});
		$display("csa5 1 %b", {csa5Ps[96:90], csa5Ps[89: 1]});
		$display("csa5   %b", 
				{         7'd0, csa5Sc[88: 0]} + 
				{csa5Ps[96:90], csa5Ps[89: 1]});
		 */
	end
	
	// Want to output MULT_WIDHT + 1 bits, i.e. top 55 bits
	// Sum in two parts to create
	//
	// {         6'd0,       45'b{csa5Sc[88:45] + csa5Ps[89:46]}, '0}
	// {csa5Ps[96:91], csa5Ps[90], 43'd0, 46'b{csa5Sc[44:0] + csa5Ps[45:1]}}   
	
	logic [44:0] sum1;
	always_comb sum1 = {1'b0, csa5Sc[88:45]} + {1'b0, csa5Ps[89:46]};
		
	logic [45:0] sum2;
	always_comb sum2 = {1'b0, csa5Sc[44:0]} + {1'b0, csa5Ps[45:1]};
	
		/*
	always_comb begin
		$display("sum1 %b", {         6'd0, sum1, 45'd0});
		$display("sum2 %b", {csa5Ps[96:91], csa5Ps[90], 43'd0, sum2});
		$display("sum  %b", 
				{         6'd0, sum1, 45'd0} +
				{csa5Ps[96:91], csa5Ps[90], 43'd0, sum2});
	end
		 */
		
	// next Stage
	logic signed [MULT_WIDTH:0]  out1_stg4;
	logic signed [MULT_WIDTH:0]  out2_stg4;
	
	`MSFF(out1_stg4, {6'b100000, sum1, 4'd0}, clk); // Adding leading 1 missing from last row of ppa
	`MSFF(out2_stg4, {csa5Ps[96:91], csa5Ps[90], 43'd0, sum2[45:41]}, clk);
	
	/*
	always_comb begin
		$display("out1 %b", out1_stg4);
		$display("out2 %b", out2_stg4);
		$display("out  %b", out1_stg4 + out2_stg4);
	end
	*/
	
	/* Stage 4 **************************************************************************************************************************/
	// TODO: Output MSBs are too many. Can already see above that PPA exceeds 2 * MULT_WIDTH!
	logic signed [MULT_WIDTH:0] outSum = out1_stg4 + out2_stg4;
	
	`MSFF(out, outSum, clk);
	
		
endmodule
