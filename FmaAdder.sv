
`include "globals.svh"

module FmaAdder(
		input logic signed [$bits(mulMantNormalSigned_t):0] in1,
		input logic [$clog2($bits(mulOut_t)) - 1 : 0] in1Shift,
		input accMantNormalSigned_t in2,
		input logic [$clog2($bits(accMantNormalSigned_t))-1:0] in2Shift,
		output logic signed [$bits(accMantNormalSigned_t) + 1:0] out		
		);
	
	logic signed [$bits(in1) -1 : 0] in1Shifted; 
	always_comb	in1Shifted = in1 >>> in1Shift;
	
	accMantNormalSigned_t in2Shifted;
	always_comb in2Shifted = in2 >>> in2Shift;	
	
	
	always_comb out = 
		{in1Shifted, 1'b0} +  
		{{(2){in2Shifted[$bits(in2Shifted) - 1]}}, in2Shifted};


endmodule


