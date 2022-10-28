
`include "globals.svh"

module FmaNormalizer(
		input logic isInf,
		input exponent_t exp,
		input logic signed [$bits(accMantNormalSigned_t) + 1:0] mant,
		output accNormalSigned_t out
		);

	logic [$bits(accMantNormalSigned_t) + 1:0] negativeMant;
	always_comb negativeMant = ~mant + 1;
		
	logic [$bits(accMantNormalSigned_t) : 0] unsignedMant;
	always_comb unsignedMant = mant[$bits(mant) - 1] ? negativeMant[$bits(accMantNormalSigned_t):0] : mant[$bits(accMantNormalSigned_t):0];
	
	typedef logic [$clog2($bits(accMantNormalSigned_t))-1:0] lzd_t;
	lzd_t lzd;
	Lzd #(.WIDTH($bits(unsignedMant)-2)) lzd_inst (.in(unsignedMant[$bits(unsignedMant) - 3:0]), .lz(lzd)); // "$bits(unsignedMant)-2" : Handling bits before the "." individually

	logic signed [$bits(accMantNormalSigned_t):0] 			   renormedMant;
	logic signed [$bits(accMantNormalSigned_t):0] 			   renormedMantOut;
	logic signed [$bits(exponent_t) + 1:0] 			   renormedSignExp;
	exponent_t 							   renormedExpOut;
	logic 							   isZero;
	logic 							   infOut;
	logic signed [$bits(accMantNormalSigned_t) + 1:0] mant_shifted;
	
	always_comb begin
		// Handling bits before the "." individually
		// TODO: Seems overly complicated
		if(unsignedMant[$bits(unsignedMant) - 1]) begin
			mant_shifted = mant  >>> 2;
			renormedSignExp = $signed({2'b0, exp}) + 2;
			isZero = (renormedSignExp < 'sd0);
		end else if(unsignedMant[$bits(unsignedMant) - 2]) begin
			mant_shifted = mant  >>> 1;
			renormedSignExp = $signed({2'b0, exp}) + 1;
			isZero = (renormedSignExp < 'sd0);
		end else begin
			mant_shifted = mant <<< lzd ;
			renormedSignExp = $signed({2'b0, exp}) - $signed({{($bits(renormedSignExp) - $bits(lzd)){1'b0}}, lzd});
			isZero = (renormedSignExp < 'sd0) || (lzd == lzd_t'($bits(unsignedMant) - 2));
		end
		
		renormedMant = mant_shifted[$bits(renormedMant)-1:0];

		infOut = isInf || (renormedSignExp >= $signed({2'b00, EXP_MAX})); // TODO: Inf and nan handling not correct
            
		renormedExpOut = isZero ? '0 : renormedSignExp[$bits(exponent_t) - 1:0];
		renormedMantOut = isZero ? '0 : renormedMant;
	end

	always_comb out.Exp = infOut ? EXP_MAX : renormedExpOut;
	always_comb out.Mant = infOut ? {$bits(accMantNormalSigned_t){1'b0}} : renormedMantOut[$bits(accMantNormalSigned_t) - 1:0];

endmodule


