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

#include <math.h>
#include <stdlib.h>
#include <float.h>

#include <array>
#include <vector>

#include "verilated.h"

#ifdef NETLIST
#include "VFMA_netlist.h"
#else // !NETLIST
#include "VFMA.h"
#endif // !NETLIST

#include "helpers.h"

#include "systolicArraySim.h"

#ifdef VERILATED_VFMA_NETLIST_H_
#define testBench_t VFMA_netlist
#else // !VERILATED_VFMA_NETLIST_H_
#define testBench_t VFMA
#endif // !VERILATED_VFMA_NETLIST_H_


static int inputSet(
		testBench_t * tb,
		const double &mult1,
		const double &mult2,
		const double &acc)
{

	if(elemSet(&tb->mult1, mult1))
	{
		sasError("elemSet failed\n");
		return -1;
	}

	if(elemSet(&tb->mult2, mult2))
	{
		sasError("elemSet failed\n");
		return -1;
	}

	if(elemSet(&tb->acc, acc))
	{
		sasError("elemSet failed\n");
		return -1;
	}

	return 0;
}

static void Print(const testBench_t &tb)
{
	sasInfo("Got %.*f * %.*f + %.*f\n", DBL_DECIMAL_DIG, toDouble(tb.mult1), DBL_DECIMAL_DIG, toDouble(tb.mult2), DBL_DECIMAL_DIG, toDouble(tb.acc));
	sasInfo("Result %f\n", toDouble(tb.out));
}

double Float(bool sign, uint8_t exp, uint32_t mant)
{
	typedef union {
		float flt;
		uint32_t u32;
	} floatUnion;

	floatUnion flt;
	flt.u32 = sign ? 1 : 0;
	flt.u32 <<= 8;
	flt.u32 |= exp;
	flt.u32 <<= 23;
	flt.u32 |= BIT_MASK(23) & mant;

	return flt.flt;
}

int UT_FMA()
{
	const double relDiffThr = 0.00000000008;

	const size_t FMA_CLOCKS = 12;
	testBench_t tb;

	std::vector<std::array<double,3>> exactTestSet = { // mult, mult, acc
			{1, 1, 1},
			{0, 0, 0},
			{1, 1, 0},
			{-1, 1, 0},
			{0, 0, 1},
			{0, 0, -1},
			{0, 1, 1},
			{1, 1, 1},
			{1, 1, -1},
			{1, 1, 0.5},
			{1, 1, -0.5},
			{0, 1, 1},
			{1, 0, 1},
			{0, 1, 0},
			{0, 0, 1},
			{0, 1, 1},
			{-1, 1, 1},
			{0, 1, -1},
			{1, 0, 1},
			{0, -1, 0},
			{-1, -1, 1},
			{INFINITY, INFINITY, INFINITY},
			{INFINITY, 1, 1},
			{INFINITY, INFINITY, 1},
			{1, INFINITY, 1},
			{1, 1, INFINITY},
			{INFINITY, -INFINITY, INFINITY},
			{-INFINITY, 1, 1},
			{-INFINITY, -INFINITY, 1},
			{1, -INFINITY, 1},
			{1, 1, -INFINITY},
			{NAN, INFINITY, 1},
			{NAN, INFINITY, NAN},
			{INFINITY, NAN, 1},
			{NAN, -INFINITY, NAN},
			{-INFINITY, NAN, 1},
			{NAN, 1, 1},
			{NAN, NAN, 1},
			{1, NAN, 1},
			{1, 1, NAN},
			{Float(0,0, 1), 1., 1.},
			{Float(0,0, 1), Float(0,0,111), 1.},
			{42, Float(0,0,111), 1.},
			{Float(0,0, 1), Float(0,0,111), Float(0,0,222)},
			{42, -42, Float(0,0,111)},
			{0, -42, Float(0,0,111)},
			{0, Float(0,0,111), 42},
			{Float(0,0,111), 0, 42},
			{585112387321856.000000, 602111490369871903981568.000000, 79124620813237695816029699618184888320.000000},
			{-585112387321856.000000, 602111490369871903981568.000000, 79124620813237695816029699618184888320.000000},
			{449396228445589332819968.000000, -308921025691648.000000, 0.000000},
			{19228064.000000, -13460653974510570165815048404992.000000, 0.000000},
	};

	size_t testNr = 0;
	for(const auto &test: exactTestSet)
	{
		testNr++;
#if DEBUG
		sasInfo("#%lu: \n\t%f * %f + %f\n\n", testNr, test[0], test[1], test[2]);
#endif // DEBUG

		if(inputSet(&tb, test[0], test[1], test[2]))
		{
			sasError("inputSet failed\n");
			return -1;
		}

		for(size_t clk = 0; clk < FMA_CLOCKS; clk++)
		{
			tb.clk = clk % 2;
			tb.eval();

#if DEBUG
			sasInfo("clk = %lu\n", clk);
			Print(tb);
			sasInfo("\n");
#endif // DEBUG
		}

		const double result = toDouble(tb.out);
		const double expected = test[0] * test[1] + test[2];
		const double diff = fabs(result - expected);
		const double relDiff = diff / fabs(expected);

		if((0 != diff) && !(
				(std::isinf(expected) || isnan(expected)) &&
				(std::isinf(result) || isnan(result)))) // TODO: Correct inf and nan handling in design
		{
			sasError("TestNr %lu: %f * %f + %f != %f (= %f, relDiff = %.*f)\n",
					testNr, test[0], test[1], test[2], result, expected, DBL_DECIMAL_DIG, relDiff);

			sasInfo("result:   ");
			printBinary((uint8_t*) &result, 8 * sizeof(result));
			sasInfo("\nexpected: ");
			printBinary((uint8_t*) &expected, 8 * sizeof(expected));
			sasInfo("\n");
			return -1;
		}
	}

	double maxRelDiff = 0;

#ifdef NETLIST
	const size_t randTestRunsPerRange = 10000;
#else // !NETLIST
	const size_t randTestRunsPerRange = 1000000;
#endif // !NETLSIT

	for(size_t randTest = 0; randTest < 3 * randTestRunsPerRange; randTest++)
	{
		testNr++;

		std::array<double,3> test;
		if(randTestRunsPerRange > randTest)
		{
			test[0] = randomDouble(-500, 500, 0.1);
			test[1] = randomDouble(-500, 500, 0.1);
			test[2] = randomDouble(-500, 500, 0.1);
		}
		else if(2 * randTestRunsPerRange > randTest)
		{
			test[0] = randomDouble(-53, 53, 0.1);
			test[1] = randomDouble(-53, 53, 0.1);
			test[2] = randomDouble(-53, 53, 0.1);
		}
		else
		{
			test[0] = randomDouble(-5, 5, 0.1);
			test[1] = randomDouble(-5, 5, 0.1);
			test[2] = randomDouble(-5, 5, 0.1);
		}

#if DEBUG
		sasInfo("#%lu: \n\t%f * %f + %f\n\n", testNr, test[0], test[1], test[2]);
#endif // DEBUG

		if(inputSet(&tb, test[0], test[1], test[2]))
		{
			sasError("inputSet failed\n");
			return -1;
		}

		for(size_t clk = 0; clk < FMA_CLOCKS; clk++)
		{
			tb.clk = clk % 2;
			tb.eval();

#if DEBUG
			Print(tb);
			sasInfo("\n");
#endif // DEBUG
		}

		const double expected = test[0] * test[1] + test[2];
		const double result = toDouble(tb.out);
		const double diff = fabs(result - expected);
		const double relDiff = diff / fabs(expected);

#if DEBUG
		sasInfo("Diff = %f (rel = %f)\n", diff, relDiff);
#endif // DEBUG

		if(maxRelDiff < relDiff)
		{
			maxRelDiff = relDiff;
		}

		if((isnan(expected) || isinf(expected)) && !(isinf(result) || isnan(result))) // TODO: Correct inf an nan handling in design
		{
			sasError("TestNr %lu: %f * %f + %f != %f (= %f)\n",
					testNr, test[0], test[1], test[2], result, expected);
			Print(tb);
			sasInfo("\n");
			return -2;

		}
		else if((relDiff > relDiffThr) && !((0 == expected) && (0 == result)))
		{
			sasError("TestNr %lu: %.*f * %.*f + %.*f != %.*f (= %.*f, relDiffThr = %.*f, relDiff = %.*f)\n",
					testNr, DBL_DECIMAL_DIG, test[0], DBL_DECIMAL_DIG, test[1], DBL_DECIMAL_DIG, test[2],  DBL_DECIMAL_DIG, result,
					DBL_DECIMAL_DIG, expected, DBL_DECIMAL_DIG, relDiffThr, DBL_DECIMAL_DIG, relDiff);
			Print(tb);
			sasInfo("\n");
			return -2;
		}
	}

#if DEBUG
	sasInfo("maxRelDiff = %.*f", DBL_DECIMAL_DIG, maxRelDiff);
#endif // DEBUG

	// pipeline tests
	std::vector<std::array<double,3>> pipeTestSet;
	for(size_t test = 0; test < 32; test++)
	{
		pipeTestSet.push_back({randomDouble(-5, 5, 0.1), randomDouble(-5, 5, 0.1), randomDouble(-5, 5, 0.1)});
	}

	for(size_t clk = 0; clk < FMA_CLOCKS - 2 + 2 * pipeTestSet.size(); clk++)
	{
		if(0 == clk % 2)
		{
			if(inputSet(&tb, pipeTestSet[clk / 2][0], pipeTestSet[clk / 2][1], pipeTestSet[clk / 2][2]))
			{
				sasError("inputSet failed\n");
				return -1;
			}
		}

		tb.clk = clk % 2;
		tb.eval();

		if((FMA_CLOCKS - 2 <= clk) && (0 == clk % 2))
		{
			const double result = toDouble(tb.out);
			const size_t testIndex = (clk - FMA_CLOCKS + 2) / 2;
			const double expected = pipeTestSet[testIndex][0] * pipeTestSet[testIndex][1] + pipeTestSet[testIndex][2];
			const double relError = fabs(result - expected) / fabs(expected);
			if(relError > relDiffThr)
			{
				sasError("Pipeline Test: Got %f, expected %f\n", result, expected);
				return -1;
			}
		}
	}

	return 0;
}

int main()
{
	srand(time(NULL));

	sasInfo("FMA UT:\n");
	if(UT_FMA())
	{
		sasFatal("UT_FMA failed\n");
	}
	sasInfo("\tSuccess\n");

	sasInfo("SystolicArray UT:\n");
	SystolicArraySim saSim;
	if(saSim.UnitTest())
	{
		sasFatal("UnitTest failed\n");
	}
	sasInfo("\tSuccess\n");

	return 0;
}
