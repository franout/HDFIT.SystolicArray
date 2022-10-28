
`include "globals.svh"

module Normalizer (
		input exponent_t exp,
		input logic signed [$bits(accMantNormalSigned_t):0] mant,
		output accNormalSigned_t out
		);
	
	logic [$bits(mant) - 1 : 0] negativeLargeMant;
	always_comb negativeLargeMant = ~mant + 1;
		
	logic [$bits(mant) - 2 : 0] unsignedLargeMant;
	always_comb unsignedLargeMant = mant[$bits(mant) - 1] ? negativeLargeMant[$bits(unsignedLargeMant) - 1: 0] : mant[$bits(unsignedLargeMant) - 1:0];
	
	typedef logic [$clog2($bits(unsignedLargeMant))-1:0] lzd_t;
	lzd_t lzd;
	Lzd #(.WIDTH($bits(unsignedLargeMant)-1)) lzd_inst (.in(unsignedLargeMant[$bits(unsignedLargeMant) - 2:0]), .lz(lzd)); // "$bits(unsignedMant)-1" : Handling bits before the "." individually

	logic signed [$bits(accMantNormalSigned_t):0] 			   renormedMant;
	exponent_t 							   renormedExp;
			
	logic isInf;
	always_comb begin
		// Handling bits before the "." individually
		// TODO: Seems overly complicated
		if(unsignedLargeMant[$bits(unsignedLargeMant) - 1]) begin
			renormedMant = mant  >>> 1;
			renormedExp = exp + 1;
			isInf = (exp >= EXP_MAX - 1);
		end else begin
			renormedMant = mant <<< lzd ;
			renormedExp = exp - $signed({5'd0, lzd});
			isInf = (exp >= EXP_MAX);
		end
	end
			
	logic 							   isZero;
	always_comb isZero = (exp <= {5'd0, lzd});
									
	always_comb out.Mant = (isZero || isInf) ? '0 : renormedMant[$bits(accMantNormalSigned_t) - 1:0];
	always_comb out.Exp = isZero ? '0 : (isInf ? '1 : renormedExp);


endmodule


