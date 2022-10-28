
`include "globals.svh"

// Returns unnormalized sum
module Adder (
		input accNormalSigned_t in1,
		input accNormalSigned_t in2,
		output exponent_t sumExp,
		output logic signed [$bits(accMantNormalSigned_t):0] sumMant // larger mantissa
		);
	
	accMantNormalSigned_t inMant;
	accMantNormalSigned_t inMantShift;
	exponent_t shift;
	
	always_comb begin
		if(in1.Exp > in2.Exp) begin
			sumExp = in1.Exp;
			inMant = in1.Mant;
			inMantShift = in2.Mant;
			shift = sumExp - in2.Exp;			
		end else begin
			sumExp = in2.Exp;
			inMant = in2.Mant;
			inMantShift = in1.Mant;
			shift = sumExp - in1.Exp;			
		end		
	end
	
	always_comb sumMant = 
		{inMant[$bits(accMantNormalSigned_t) - 1], inMant} + 
		{inMantShift[$bits(accMantNormalSigned_t) - 1], inMantShift >>> shift};

endmodule


