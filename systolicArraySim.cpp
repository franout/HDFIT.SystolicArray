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

#include <stdio.h>
#include <float.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

#include <climits>
#include <memory>
#include <cmath>

#include "verilated.h"

#ifdef NETLIST
#include "netlistFaultInjector.hpp"
#endif // NETLIST

#ifdef NETLIST
#include "VSystolicArray_netlist.h"
#else // !NETLIST
#include "VSystolicArray.h"
#endif // !NETLIST

#include "helpers.h"

#include "systolicArraySim.h"

#ifdef VERILATED_VSYSTOLICARRAY_NETLIST_H_
#define testBench_t VSystolicArray_netlist
#else // !VERILATED_VSYSTOLICARRAY_NETLIST_H_
#define testBench_t VSystolicArray
#endif // !VERILATED_VSYSTOLICARRAY_NETLIST_H_

static const double unitTestRelTolerance = 0.0000000003;
static int unitTestExponentRange = INT_MAX; // TODO: Having this global is ugly

template <typename Enumeration>
auto to_integer(Enumeration const value)
    -> typename std::underlying_type<Enumeration>::type
{
    return static_cast<typename std::underlying_type<Enumeration>::type>(value);
}


SystolicArraySim::SystolicArraySim()
{
	// Call  commandArgs  first!
#if 0
	const char appName[][20] = {"SystolicArraySim"};
	Verilated::commandArgs(1, (const char **) &appName); // TODO: Find out what the args look like
#endif

	//  Instantiate our design
	TbVoid_ = (void *) new testBench_t;

#ifdef NETLIST
	// Initialize NetlistFaultInjector
	auto netlistFaultInjector = new NetlistFaultInjector;
	if(netlistFaultInjector->Init()) // TODO: Would be nicer if it used the struct required as input
	{
		sasError("NetlistFaultInjector Init failed\n");
	}

	NetlistFaultInjectorVoid_ = (void*) netlistFaultInjector;
#endif // NETLIST
}

SystolicArraySim::~SystolicArraySim() {
	delete (testBench_t*) TbVoid_;

#ifdef NETLIST
	delete (NetlistFaultInjector *) NetlistFaultInjectorVoid_;
#endif // NETLIST
}

int SystolicArraySim::DispatchMma(const job_t &job)
{
#if DEBUG_VERBOSE
	sasDebug("Dispatched Job:\n");
	sasDebug("A =\n");
	matrixPrint(job.MatA, Config_.Mmma, Config_.Kmma, job.StrideA);
	sasDebug("B =\n");
	matrixPrint(job.MatB, Config_.Kmma, Config_.Nmma, job.StrideB);
	sasDebug("C =\n");
	matrixPrint(job.MatC, Config_.Mmma, Config_.Nmma, job.StrideC);
#endif // DEBUG_VERBOSE

	JobQueue_.push_back({0, job});

	return 0;
}

int SystolicArraySim::DispatchMma(const job_t &job, size_t mCnt, size_t nCnt)
{
#if DEBUG_VERBOSE
	sasDebug("Dispatched %lu x %lu MMAs:\n", mCnt, nCnt);
	sasDebug("A =\n");
	matrixPrint(job.MatA, mCnt * Mmma(), Ktile(), job.StrideA);
	sasDebug("B =\n");
	matrixPrint(job.MatB, Ktile(), nCnt * Nmma(), job.StrideB);
	sasDebug("C =\n");
	matrixPrint(job.MatC, mCnt * Mmma(), nCnt * Nmma(), job.StrideC);
#endif // DEBUG_VERBOSE

	// Left buffer larger than right buffer: Walk through rows first
	for(size_t row = 0; row < mCnt * Mmma(); row += Mmma())
	{
		const double * Ap = job.MatA + row * job.StrideA;
		for(size_t col = 0; col < nCnt * Nmma(); col += Nmma())
		{
			const double * Bp = job.MatB + col;
			double * Cp = job.MatC + row * job.StrideC + col;

			const job_t jobMma = {
					Ap, job.StrideA,
					Bp, job.StrideB,
					Cp, job.StrideC};

			if(DispatchMma(jobMma))
			{
				sasError("DispatchMma failed\n");
				return -1;
			}
		}
	}

	return 0;
}

int SystolicArraySim::DispatchTile(const job_t &job)
{
#if DEBUG_VERBOSE
	sasDebug("Dispatched Tile:\n");
	sasDebug("A =\n");
	matrixPrint(job.MatA, Mtile(), Ktile(), job.StrideA);
	sasDebug("B =\n");
	matrixPrint(job.MatB, Ktile(), Ntile(), job.StrideB);
	sasDebug("C =\n");
	matrixPrint(job.MatC, Mtile(), Ntile(), job.StrideC);
#endif // DEBUG_VERBOSE

	// Left buffer larger than right buffer: Walk through rows first
	for(size_t row = 0; row < Mtile(); row += Mmma())
	{
		const double * Ap = job.MatA + row * job.StrideA;
		for(size_t col = 0; col < Ntile(); col += Nmma())
		{
			const double * Bp = job.MatB + col;
			double * Cp = job.MatC + row * job.StrideC + col;

			const job_t jobMma = {
					Ap, job.StrideA,
					Bp, job.StrideB,
					Cp, job.StrideC};

			if(DispatchMma(jobMma))
			{
				sasError("DispatchMma failed\n");
				return -1;
			}
		}
	}

	return 0;
}

// Non-netlist simulation
[[maybe_unused]] static int setValue(VlWide<3> * out, size_t outIndex, double in)
{
	// TODO: Handle nan
	//  65'b{11'b: -1023 biased exp, 54'sb: signed mantissa with leading 1}
	const doubleUnion uval = {in};

	const uint16_t tmpExp = (uval.u64 >> 52) & BIT_MASK(11);

	int64_t signedMantissa = uval.u64 & BIT_MASK(52);
	if(std::isnormal(in))
	{
		signedMantissa |= 1ULL << 52;
	}

	if(uval.u64 & (1ULL << 63))
	{
		signedMantissa = -signedMantissa;
	}

	signedMantissa &= BIT_MASK(54);

	if(bitsCopy((uint8_t*) out[outIndex].data(), sizeof(out[0]), 0, (uint8_t*) &signedMantissa, 54))
	{
		sasError("bitsCopy failed\n");
		return -1;
	}

	if(bitsCopy((uint8_t*) out[outIndex].data(), sizeof(out[0]), 54, (uint8_t*) &tmpExp, 11))
	{
		sasError("bitsCopy failed\n");
		return -1;
	}

	return 0;
}

// Netlist simulation
[[maybe_unused]] static int setValue(WData* pData, size_t nData, size_t nBitsElem, size_t pos, double value)
{
	if(65 != nBitsElem)
	{
		sasError("nBitsData = %lu not implemented (only implemented for 65'b double so far)\n", nBitsElem);
		return -1;
	}

	if(nBitsElem * pos >= 8 * nData)
	{
		sasError("Pos doesn't fit into destination\n");
		return -1;
	}

	// TODO: Write function to create this 65-bit double representation. Code is replicated.
	// TODO: Handle nan
	//  65'b{11'b: -1023 biased exp, 54'sb: signed mantissa with leading 1}
	const doubleUnion uval = {value};
	const uint16_t tmpExp = (uval.u64 >> 52) & BIT_MASK(11);

	int64_t signedMantissa = uval.u64 & BIT_MASK(52);
	if(std::isnormal(value))
	{
		signedMantissa |= 1ULL << 52;
	}

	if(uval.u64 & (1ULL << 63))
	{
		signedMantissa = -signedMantissa;
	}

	signedMantissa &= BIT_MASK(54);

	if(bitsCopy((uint8_t*) pData, nData, pos * nBitsElem, (uint8_t*) &signedMantissa, 54))
	{
		sasError("bitsCopy failed\n");
		return -1;
	}

	if(bitsCopy((uint8_t*) pData, nData, pos * nBitsElem + 54, (uint8_t*) &tmpExp, 11))
	{
		sasError("bitsCopy failed\n");
		return -1;
	}

	return 0;
}

// Non-netlist simulation
[[maybe_unused]] static double getValue(VlWide<3> * in, size_t index)
{
	return toDouble(in[index]);
}

// Netlist simulation
[[maybe_unused]] static double getValue(const WData * pData, size_t nData, size_t nBitsElem, size_t pos)
{
	if(65 != nBitsElem)
	{
		sasError("nBitsData = %lu not implemented (only implemented for 65'b double so far)\n", nBitsElem);
		return -1;
	}

	if(nBitsElem * (pos + 1) >= 8 * nData)
	{
		sasError("Pos doesn't fit into destination\n");
		return -1;
	}

	VlWide<3> tmp;
	for(size_t index = 0; index < sizeof(tmp.m_storage) / sizeof(tmp.m_storage[0]); index++)
	{
		tmp[index] = 0;
	}

	const size_t bitStart = pos * nBitsElem;
	uint8_t* tmpU8 = (uint8_t*) tmp.data();
	for(size_t bit = 0; bit < 65; bit++)
	{
		size_t tmpByte = bit / 8;
		uint8_t tmpBit = bit % 8;

		size_t dataByte = (bitStart + bit) / 8;
		uint8_t dataBit = (bitStart + bit) % 8;

		if(((const uint8_t*)pData)[dataByte] & (1 << dataBit))
		{
			tmpU8[tmpByte] |= 1 << tmpBit;
		}
	}

	return toDouble(tmp);
}

size_t SystolicArraySim::CyclesRequired(size_t jobCnt) const
{
	if(0 == jobCnt)
	{
		return 0;
	}

	return JobCycleDone_ + (jobCnt - 1) *(JobCyclePassedFirstStage_ + 1) + 1;
}

size_t SystolicArraySim::JobsDoneInCycles(size_t cycleCnt) const
{
	if(JobCycleDone_ > cycleCnt)
	{
		return 0;
	}

	return (cycleCnt - JobCycleDone_ - 1) / (JobCyclePassedFirstStage_ + 1) + 1;
}

int SystolicArraySim::IoSet(void * TbVoid, std::deque<queueEntry_t> * jobs, bool clkHigh)
{
	if(jobs->empty())
	{
		sasError("deque is empty\n");
		return -1;
	}

	std::vector<queueEntry_t *> concurrentJobs = {&jobs->front()};
	for(size_t job = 1; job < jobs->size(); job++)
	{
		if(jobs->at(job - 1).JobCycle > JobCyclePassedFirstStage_) // check that previous job has freed first stage
		{
			concurrentJobs.push_back(&jobs->at(job));
		}
		else
		{
			break;
		}
	}

	testBench_t * Tb = (testBench_t*) TbVoid;

#ifdef NETLIST
	const size_t MmmaRTL = (sizeof(Tb->out.m_storage) * 8) / 65;
#else // !NETLIST
	const size_t MmmaRTL = (sizeof(Tb->out->m_storage) * 8) / 65;
#endif // !NETLIST

	for(size_t job = 0; job < concurrentJobs.size(); job++)
	{
		job_t * jobp = &concurrentJobs[job]->Job;

		for(size_t m = 0; m < MmmaRTL; m++)
		{
			// Dispatch Order
			// Cycle 0			: k = 0, n = 0
			// Cycle 1			: k = 1, n = 0
			// Cycle 2			: k = 0, n = 1
			// Cycle 3			: k = 1, n = 1
			// Cycle 4          : k = 0, n = 2
			// ...
			// Cycle FmaCycles	: k = 2, n = 0
			// Cycle FmaCycles+1: k = 3, n = 0
			// Cycle FmaCycles+2: k = 2, n = 1
			// ...

			// Left matrix input
			// The left matrix does not change with n so needs only to be set for n = 0
			// Note that the next k-value (= next FMA input) need only be set once
			// the previous k output (= previous FMA Output) has been produced.
			// To add some complications, each SA row is separated into two independent phase-shifted FMAs
			const bool lInEvenK = (0 == concurrentJobs[job]->JobCycle % FmaCycles_);
			const bool lInOddK = (0 == (concurrentJobs[job]->JobCycle - 1) % FmaCycles_) && concurrentJobs[job]->JobCycle;
			if(lInEvenK || lInOddK)
			{
				const size_t k = 2 * (concurrentJobs[job]->JobCycle / FmaCycles_) + (lInEvenK ? 0 : 1);
				if(k < Kmma())
				{
#ifdef NETLIST
					if(setValue(Tb->multLeft.data(), sizeof(Tb->multLeft.m_storage), 65, m * Kmma() + k, jobp->MatA[m * jobp->StrideA + k]))
#else // !NETLIST
					if(setValue(Tb->multLeft[0], m * Kmma() + k, jobp->MatA[m * jobp->StrideA + k]))
#endif // !NETLIST
					{
						sasError("setValue failed\n");
						return -1;
					};
				}
			}

			// Right matrix input
			const size_t nCnt = std::min(concurrentJobs[job]->JobCycle / 2 + 1, Nmma());
			for(size_t n = 0; n < nCnt; n++)
			{
				const size_t nJobCycle = concurrentJobs[job]->JobCycle - 2 * n;
				const bool rInEvenK = (0 == nJobCycle % FmaCycles_);
				const bool rInOddK = (0 == (nJobCycle - 1) % FmaCycles_) && nJobCycle;
				if(rInEvenK || rInOddK)
				{
					const size_t k = 2 * (nJobCycle / FmaCycles_) + (rInEvenK ? 0: 1);
					if(k < Kmma())
					{
#ifdef NETLIST
						if(setValue(Tb->multRight.data(), sizeof(Tb->multRight.m_storage), 65, k, jobp->MatB[k * jobp->StrideB + n]))
#else // !NETLIST
						if(setValue(Tb->multRight, k, jobp->MatB[k * jobp->StrideB + n]))
#endif // !NETLIST
						{
							sasError("setValue failed\n");
							return -1;
						}
					}
				}
			}

			// Acc: Each time a new "n" is added
			if(0 == (concurrentJobs[job]->JobCycle % 2))
			{
				const size_t n = concurrentJobs[job]->JobCycle / 2;
				if(n < Nmma())
				{
#ifdef NETLIST
					if(setValue(Tb->acc.data(), sizeof(Tb->acc.m_storage), 65, m, jobp->MatC[m * jobp->StrideC + n]))
#else // !NETLIST
					if(setValue(Tb->acc, m, jobp->MatC[m * jobp->StrideC + n]))
#endif // !NETLIST
					{
						sasError("setValue failed\n");
						return -1;
					}
				}
			}

			// Gather output
			if(JobCycleOutputStart_ <= concurrentJobs[job]->JobCycle)
			{
				const size_t cycleOffset = concurrentJobs[job]->JobCycle - JobCycleOutputStart_;
				if(0 == (cycleOffset % 2))
				{
					const size_t n = cycleOffset / 2;
					if(n > Nmma())
					{
						sasError("Unexpected n: Job should have been removed already\n");
						return -1;
					}
#ifdef NETLIST
					jobp->MatC[m * jobp->StrideC + n] = getValue(Tb->out.data(), sizeof(Tb->out.m_storage), 65, m);
#else // !NETLIST
					jobp->MatC[m * jobp->StrideC + n] = getValue(Tb->out, m);
#endif // !NETLIST
				}
			}
		}
	}

	if(JobCycleDone_ == jobs->front().JobCycle)
	{
		// Are we only simulating a single column of the SA?
		// Then calculate the other entries directly
		if(MmmaRTL != Mmma()) // TODO: This also means fault is always injected into the first SA column!
		{
			job_t * jobp = &jobs->front().Job;
			for(size_t row = MmmaRTL; row < Mmma(); row++)
			{
				for(size_t col = 0; col < Nmma(); col++)
				{
					for(size_t k = 0; k < Kmma(); k++)
					{
						jobp->MatC[row * jobp->StrideC + col] += jobp->MatA[row * jobp->StrideA + k] * jobp->MatB[k * jobp->StrideB + col];
					}
				}
			}
		}

		jobs->pop_front();
	}
	else if(JobCycleDone_ < jobs->front().JobCycle)
	{
		sasError("Jobcycle threshold breached (have %lu)!\n", jobs->front().JobCycle);
		return -4;
	}

	for(size_t job = 0; job < concurrentJobs.size(); job++)
	{
		concurrentJobs[job]->JobCycle++;
	}

	return 0;
}

SystolicArraySim::faultRTL_t SystolicArraySim::FiSetRTL(fiMode mode)
{
#ifdef NETLIST
	if(fiMode::None == mode)
	{
		sasError("Setting None-fault\n");
		return faultRTL_t();
	}

	NetlistFaultInjector * netlistFaultInjector = (NetlistFaultInjector*) NetlistFaultInjectorVoid_;
	size_t fiSignalWidth = 0;

	if(netlistFaultInjector->RandomFiGet(
			&FaultRTL_.ModuleInstanceChain,
			&FaultRTL_.AssignUUID,
			&fiSignalWidth))
	{
		sasError("RandomFiGet failed\n");
		return faultRTL_t();
	}

	FaultRTL_.BitPos = randomBits() % fiSignalWidth;

	if(fiMode::Transient == mode)
	{
		CycleCnt_ = 0;
		const size_t cyclesRequired = CyclesRequired(JobQueue_.size());
		if(0 == cyclesRequired)
		{
			sasError("Trying to set transient fault with empty JobQueue\n");
			return faultRTL_t();
		}

		FaultRTLTransCycle_ = randomBits() % cyclesRequired;
	}

	FaultRTL_.Mode = mode;

	sasFaultPrint("Set FaultRTL_:\n\tModule Instance Chain: ");
	for(const auto &inst: FaultRTL_.ModuleInstanceChain)
	{
		sasFaultPrint("%u, ", inst);
	}
	sasFaultPrint("\n\tAssignUUID = %u\n\tBitPos = %u\n\tMode = %i\n",
			FaultRTL_.AssignUUID, FaultRTL_.BitPos, (int) FaultRTL_.Mode);

	return FaultRTL_;

#else // !NETLIST
	sasError("Only available with NETLIST\n");
	return faultRTL_t();
#endif // !NETLIST
}

SystolicArraySim::faultCsim_t SystolicArraySim::FiSetCsim(
		fiCsimPlace place,
		fiBits bits,
		fiCorruption corruption,
		fiMode mode)
{
	if((fiCsimPlace::None == place) || (fiBits::None == bits) ||
			(fiCorruption::None == corruption) || (fiMode::None == mode))
	{
		sasError("Setting None-fault\n");
		return faultCsim_t();
	}

	if(fiCsimPlace::Everywhere == place)
	{
		// Assuming equal distribution across
		// inputs, Kmma multipliers, Kmma acc adders, 1 final column adder)
		// I.e. 2 * Kmma + 1 components (inputs have significant derating)
		// TODO: Multiplier much larger than adder
		// coverity[DC.WEAK_CRYPTO]
		const int randNr = rand();
		const int FractionRandMax = RAND_MAX / (2 * Kmma() + 1);
		if(randNr < Kmma() * FractionRandMax)
		{
			FaultCsim_.Place = fiCsimPlace::Multipliers;
		}
		else if(randNr < 2 * Kmma() * FractionRandMax)
		{
			FaultCsim_.Place = fiCsimPlace::AccAdders;
		}
		else if(randNr < (2 * Kmma() + 1) * FractionRandMax)
		{
			FaultCsim_.Place = fiCsimPlace::ColumnAdders;
		}
		else
		{
			FaultCsim_.Place = fiCsimPlace::Inputs;
		}
	}
	else
	{
		FaultCsim_.Place = place;
	}

	FaultCsim_.Corruption = corruption;

	if(fiMode::Transient == mode)
	{
		CycleCnt_ = 0;
		const size_t totalJobQueueCycles = JobQueue_.size() * Nmma();
		// coverity[DC.WEAK_CRYPTO]
		FaultCsimTransCycle_ = rand() % totalJobQueueCycles;
	}

	FaultCsim_.Mode = mode;

	switch(bits)
	{
	default: // no break intended
	case fiBits::None:
		sasError("Setting None fiBits\n");
		return faultCsim_t();

	case fiBits::Everywhere:
		// coverity[DC.WEAK_CRYPTO]
		FaultCsim_.BitPos = rand() % (sizeof(double) * 8);
		break;

	case fiBits::Mantissa:
		// coverity[DC.WEAK_CRYPTO]
		FaultCsim_.BitPos = rand() % 52;
		break;
	}

	// coverity[DC.WEAK_CRYPTO]
	FaultCsim_.Row = rand() % Mmma();

	sasFaultPrint("Set FaultCsim_: Place %i, Corruption %i, fiMode %i, Column %u, BitPos %u\n",
			to_integer(FaultCsim_.Place), to_integer(FaultCsim_.Corruption),
			to_integer(FaultCsim_.Mode), FaultCsim_.Row, FaultCsim_.BitPos);

	return FaultCsim_;
}

int SystolicArraySim::FiResetRTL()
{
	if(fiMode::None == FaultRTL_.Mode)
	{
		sasError("No fault was set!\n");
		return -1;
	}

	FaultRTL_ = faultRTL_t();
	FaultRTLTransCycle_ = SIZE_MAX;

	return 0;
}

int SystolicArraySim::FiResetCsim()
{
	if(fiCsimPlace::None == FaultCsim_.Place)
	{
		sasError("No fault was set!\n");
		return -1;
	}

	FaultCsim_ = faultCsim_t();
	FaultCsimTransCycle_ = SIZE_MAX;

	return 0;
}

static double corrupt(double in, SystolicArraySim::fiCorruption corruption, uint8_t bitPos)
{
	if(63 < bitPos)
	{
		sasError("bitPos > 64\n");
		return NAN;
	}

	doubleUnion inU64 = {in};
	switch(corruption)
	{
	default: // no break intended
	case SystolicArraySim::fiCorruption::None:
		return in;

	case SystolicArraySim::fiCorruption::Flip:
		inU64.u64 ^= 1UL << bitPos;
		break;

	case SystolicArraySim::fiCorruption::StuckHigh:
		inU64.u64 |= 1UL << bitPos;
		break;

	case SystolicArraySim::fiCorruption::StuckLow:
		inU64.u64 &= ~(1UL << bitPos);
		break;
	}

	sasFaultPrint("Corrupting %f -> %f\n", in, inU64.flt);

	return inU64.flt;
}

// out = out + A_1 * B_1 + ... + A_8 * B_8
// fi = nullptr if no fault injection intended
int SystolicArraySim::RowCsim(double * out, double * a, double * b, const faultCsim_t * fi) const
{
	// coverity[DC.WEAK_CRYPTO]
	int kFi = rand() % Kmma();

	for(size_t k = 0; k < Kmma(); k++)
	{
		if((k == kFi) && (nullptr != fi))
		{
			// Inputs
			double accIn = *out;
			double aIn = a[k];
			double bIn = b[k];
			if(fiCsimPlace::Multipliers == fi->Place)
			{
				// coverity[DC.WEAK_CRYPTO]
				size_t inRand = rand() % 3;
				if(0 == inRand) accIn = corrupt(*out, fi->Corruption, fi->BitPos);
				else if(1 == inRand) aIn = corrupt(aIn, fi->Corruption, fi->BitPos);
				else bIn = corrupt(bIn, fi->Corruption, fi->BitPos);
			}

			// Mul
			double mul = aIn * bIn;
			if(fiCsimPlace::Multipliers == fi->Place)
			{
				mul = corrupt(mul, fi->Corruption, fi->BitPos);
			}

			// Acc Add
			double acc = mul + accIn;
			if(fiCsimPlace::AccAdders == fi->Place)
			{
				acc = corrupt(acc, fi->Corruption, fi->BitPos);
			}
		}
		else
		{
			*out += a[k] * b[k];
		}
	}

	if((nullptr != fi) && (fiCsimPlace::ColumnAdders == fi->Place))
	{
		*out =  corrupt(*out, fi->Corruption, fi->BitPos);
	}

	return 0;
}

int SystolicArraySim::ExecCsim(size_t maxJobs)
{
	const size_t origJobs = JobQueue_.size();
	while(!JobQueue_.empty() && (origJobs - JobQueue_.size() < maxJobs))
	{
		// JobCycle = col for c sim
		job_t * job = &JobQueue_.front().Job;
		const size_t col = JobQueue_.front().JobCycle;

		// Calculate non-simulated cols
		for(size_t row = 0; row < Mmma(); row++)
		{
			if(FaultCsim_.Row == row)
			{
				continue;
			}

			for(size_t sum = 0; sum < Kmma(); sum++)
			{
				job->MatC[row * job->StrideC + col] += job->MatA[row * job->StrideA + sum] * job->MatB[sum * job->StrideB + col];
			}
		}

		// Calculate simulated row
		std::vector<double> leftIn(Kmma());
		std::vector<double> rightIn(Kmma());
		for(size_t sum = 0; sum < Kmma(); sum++)
		{
			leftIn[sum] = job->MatA[FaultCsim_.Row * job->StrideA + sum];
			rightIn[sum] = job->MatB[sum * job->StrideB + col];
		}

		const faultCsim_t * colCsimFi = ((CycleCnt_ == FaultCsimTransCycle_) || (fiMode::Permanent == FaultCsim_.Mode)) ? &FaultCsim_ : nullptr;
		if(RowCsim(&job->MatC[FaultCsim_.Row * job->StrideC + col], leftIn.data(), rightIn.data(), colCsimFi))
		{
			sasError("ColCsim failed\n");
			return -1;
		}

		CycleCnt_++;
		JobQueue_.front().JobCycle++;
		if(JobQueue_.front().JobCycle >= Nmma())
		{
			JobQueue_.pop_front();
		}
	}

	return 0;
}

int SystolicArraySim::FiRtlApply(void * TbVoid, const std::vector<uint16_t> &modInst, uint32_t assignNr, size_t fiBit)
{
#ifdef NETLIST
	testBench_t * Tb = (testBench_t*) TbVoid;

	// Set instance chain
	for(size_t inst = 0; inst < sizeof(Tb->GlobalFiModInstNr) / sizeof(Tb->GlobalFiModInstNr[0]); inst++)
	{
		if(modInst.size() > inst)
		{
			Tb->GlobalFiModInstNr[inst] = modInst[inst];
		}
		else
		{
			Tb->GlobalFiModInstNr[inst] = 0;
		}
	}

	Tb->GlobalFiNumber = assignNr;

	// Reset whatever was set before in fi signal
	memset(Tb->GlobalFiSignal.m_storage, 0, sizeof(Tb->GlobalFiSignal.m_storage));

	// Set specific bit
	const size_t bitsInArrayElem = sizeof(Tb->GlobalFiSignal.m_storage[0]) * 8;
	const size_t arrayIndex = fiBit / bitsInArrayElem;
	const size_t arrayBit = fiBit % bitsInArrayElem;

	Tb->GlobalFiSignal.m_storage[arrayIndex] = 1UL << arrayBit;

#else // !NETLIST
	sasError("Only available with NETLIST\n");
	return -1;
#endif // !NETLIST

	return 0;
}

int SystolicArraySim::FiRtlReset(void * TbVoid)
{
#ifdef NETLIST
	testBench_t * Tb = (testBench_t*) TbVoid;

	for(size_t inst = 0; inst < sizeof(Tb->GlobalFiModInstNr) / sizeof(Tb->GlobalFiModInstNr[0]); inst++)
	{
		Tb->GlobalFiModInstNr[inst] = 0;
	}

	return 0;

#else // !NETLIST
	return 0;
#endif // !NETLIST
}

bool SystolicArraySim::JobQueueReadBeforeWrite(const std::deque<queueEntry_t> &jobQueue) const
{
	const size_t jobsInPipe = JobCycleDone_ / JobCyclePassedFirstStage_;

	for(size_t job = 0; job < jobQueue.size(); job++)
	{
		for(size_t nextJob = job + 1; nextJob < std::min(job + jobsInPipe, jobQueue.size()); nextJob++)
		{
			if((jobQueue[job].Job.MatC == jobQueue[nextJob].Job.MatA) ||
					(jobQueue[job].Job.MatC == jobQueue[nextJob].Job.MatB) ||
					jobQueue[job].Job.MatC == jobQueue[nextJob].Job.MatC)
			{
				return true;
			}
		}
	}

	return false;
}

int SystolicArraySim::ExecRtl(bool fastTransient, bool fastTransientTest)
{
	// Run sanity check on jobqueue
	if(JobQueueReadBeforeWrite(JobQueue_))
	{
		sasError("Read before write in jobqueue\n");
		return -1;
	}

	// Set permanent fault if enabled
	if(fiMode::Permanent == FaultRTL_.Mode)
	{
		if(FiRtlApply(TbVoid_, FaultRTL_.ModuleInstanceChain, FaultRTL_.AssignUUID, FaultRTL_.BitPos))
		{
			sasError("FiRtlApply failed\n");
			return -1;
		}
	}
	else if(FiRtlReset(TbVoid_))
	{
		sasError("FiRtlReset failed\n");
		return -1;
	}

	// Skip jobs before transient fault happens
	if((fiMode::Transient == FaultRTL_.Mode) && fastTransient)
	{
		const size_t jobsBefore = FaultRTLTransCycle_ > JobCycleDone_ ? JobsDoneInCycles(FaultRTLTransCycle_ - JobCycleDone_) : 0;
		if(jobsBefore)
		{
			if(ExecCsim(jobsBefore))
			{
				sasError("ExecCsim failed\n");
				return -1;
			}

			// reset cycle cnt
			for(auto &job: JobQueue_)
			{
				job.JobCycle = 0;
			}

			// Set cycles
			CycleCnt_ = CyclesRequired(jobsBefore);

			sasDebug("Cycle %lu: fastTransient: Skip first jobs\n", CycleCnt_);
		}
	}

	// Start the actual simulation
	testBench_t * Tb = (testBench_t*) TbVoid_;

#ifdef NETLIST
	const size_t MmmaRTL = (sizeof(Tb->out.m_storage) * 8) / 65;
#else // !NETLIST
	const size_t MmmaRTL = (sizeof(Tb->out->m_storage) * 8) / 65;
#endif // !NETLIST

	if(MmmaRTL != Mmma())
	{
		sasDebug("RTL simulation running for %lu SA-columns out of %lu\n", MmmaRTL, Mmma());
	}

	// Perform simulation for chosen channel
	Tb->clk = 1;
	while(!JobQueue_.empty())
	{
		Tb->clk = Tb->clk ? 0 : 1;

		if(IoSet(Tb, &JobQueue_, Tb->clk))
		{
			sasError("inputSet failed\n");
			return -1;
		}

		// Fault injection
		if(fiMode::Transient == FaultRTL_.Mode)
		{
			if(CycleCnt_ == FaultRTLTransCycle_)
			{
				sasDebug("Cycle %lu: Setting transient fault\n", CycleCnt_);
				if(!fastTransientTest && FiRtlApply(TbVoid_, FaultRTL_.ModuleInstanceChain, FaultRTL_.AssignUUID, FaultRTL_.BitPos))
				{
					sasError("FiRtlApply failed\n");
					return -1;
				}
			}
			else if(FiRtlReset(TbVoid_))
			{
				sasError("FiRtlReset failed\n");
				return -1;
			}
		}

#if DEBUG_VERBOSE
		sasDebug("cycle = %lu:%s:\n", CycleCnt_, Tb->clk ? "H" : "L");
		sasDebug("\tout =");
		for(size_t m = 0; m < Mmma(); m++)
		{
			sasDebug("%.10f, ", getValue(Tb->out.data(), sizeof(Tb->out.m_storage), 65, m));
		}
		sasDebug("\n");
		printBinary((uint8_t*) Tb->out.data(), 130);
		sasInfo("\n");

		sasDebug("cycle = %lu:%s:\n", CycleCnt_, Tb->clk ? "H" : "L");
		sasDebug("\tout = %.10e\n", getValue(Tb->out, 0));
		sasDebug("\tmulLeft = \n");
		for(size_t row = 0; row < Mmma(); row++)
		{
			sasDebug("\t\t");
			for(size_t k = 0; k < Kmma(); k++)
			{
				sasDebug("%f,", getValue(Tb->multLeft[0], row * Kmma() + k));
			}
			sasDebug("\n");
		}

		sasDebug("\tmulRight = \n");
		sasDebug("\t\t");
		for(size_t k = 0; k < Kmma(); k++)
		{
			sasDebug("%f,", getValue(Tb->multRight, k));
		}
		sasDebug("\n");

		sasDebug("\tacc = \n");
		sasDebug("\t\t");
		for(size_t m = 0; m < Mmma(); m++)
		{
			sasDebug("%f,", getValue(Tb->acc, m));
		}
		sasDebug("\n");

		sasDebug("\tfmaAccOut:\n\t\t");
		for(size_t k = 0; k < Kmma() - 1; k++)
		{
			sasDebug("%.10e, ", getValue(Tb->rootp->SystolicArray__DOT__fma_m__BRA__0__KET____DOT__fmaAccOut.m_storage, k));
		}
		sasDebug("\n");

		sasDebug("cycle = %lu:%s, dpdpas_dv = %i; out = (%f, %f, %f, %f, %f, %f, %f, %f)\n",
				CycleCnt_, Tb->gcreudpasclk ? "H" : "L", Tb->ga_dpdpas_dv,
						getValue(Tb->dpdpas_result, 0), getValue(Tb->dpdpas_result, 1), getValue(Tb->dpdpas_result, 2), getValue(Tb->dpdpas_result, 3),
						getValue(Tb->dpdpas_result, 4), getValue(Tb->dpdpas_result, 5), getValue(Tb->dpdpas_result, 6), getValue(Tb->dpdpas_result, 7));
#endif // DEBUG_VERBOSE

		CycleCnt_++;

		Tb->eval();

		if(Tb->error)
		{
			if(false == DieError_)
			{
				sasDebug("dpdpas_dierr set!\n");
			}

			DieError_ = true;
		}

		// Run c-model if transient fault was "flushed" out
		if((fiMode::Transient == FaultRTL_.Mode) && fastTransient)
		{
			// Make sure rtl simulation hasn't output anything on the front job yet
			if((CycleCnt_ > FaultRTLTransCycle_ + JobCycleDone_ + 1) && (JobQueue_.front().JobCycle < JobCycleOutputStart_))
			{
				sasDebug("Cycle %lu: fastTransient: Skip remaining jobs\n", CycleCnt_);

				// reset cycle cnt
				for(auto &job: JobQueue_)
				{
					job.JobCycle = 0;
				}

				// carry out with c-model
				return ExecCsim();
			}
		}
	}

	return 0;
}

static std::shared_ptr<double[]> randomMatrix(size_t M, size_t N, size_t stride)
{
	if(stride < N)
	{
		sasError("Stride can't be smaller than N\n");
		return nullptr;
	}

	const size_t elementCnt = M * stride;
	std::shared_ptr<double[]> out(new double[elementCnt]);
	if(nullptr == out)
	{
		sasError("malloc failed\n");
		return nullptr;
	}

	// might as well set all values to something
	for(size_t index = 0; index < elementCnt; index++)
	{
		out[index] = randomDouble(-unitTestExponentRange, unitTestExponentRange, 0.1);
	}

	return out;
}

template <typename T>
static bool resultCorrect(const double * expected, T got, size_t rowCnt, size_t colCnt)
{
#if SAS_DEBUG
	double largestDiff = 0;
	double largestDiffExpectedVal = NAN;
	double largestDiffActualVal = NAN;

	double largestRelDiff = 0;
	double largestRelDiffExpectedVal = NAN;
	double largestRelDiffActualVal = NAN;
#endif // SAS_DEBUG

	for(size_t index = 0; index < rowCnt * colCnt; index++)
	{
		const double diff = fabs(expected[index] - got[index]);

#if SAS_DEBUG
		if(largestDiff < diff)
		{
			largestDiff = diff;
			largestDiffExpectedVal = expected[index];
			largestDiffActualVal = got[index];
		}
#endif // SAS_DEBUG

		const double relDiff = diff / fabsf(expected[index]);

#if SAS_DEBUG
		if((largestRelDiff < relDiff) &&
				((relDiff != std::numeric_limits<double>::infinity()) || (expected[index] != 0))) // Inf is only an error if expected != 0
		{
			largestRelDiff = relDiff;
			largestRelDiffExpectedVal = expected[index];
			largestRelDiffActualVal = got[index];
		}
#endif // SAS_DEBUG

		if(relDiff > unitTestRelTolerance)
		{
			sasError("Index %lu (row %lu, col %lu): Got %f, expected %f (diff %.*f, rel. diff %.*f)\n",
					index, index / colCnt, index % colCnt, got[index], expected[index],
					DBL_DECIMAL_DIG, diff, DBL_DECIMAL_DIG, relDiff);

#if DEBUG_VERBOSE
			sasDebug("Got:\n");
			matrixPrint(matC, rowCnt, colCnt, colCnt);
			sasDebug("Expected:\n");
			matrixPrint(expected, rowCnt, colCnt, colCnt);
#endif // DEBUG_VERBOSE

			return false;
		}
	}

	sasDebug("Largest Rel. Diff %.*f (valExp %f, valAct %f), Abs. Diff %.*f (valExp %f, valAct %f)\n",
			DBL_DECIMAL_DIG, largestRelDiff,	largestRelDiffExpectedVal, largestRelDiffActualVal,
			DBL_DECIMAL_DIG, largestDiff,	largestDiffExpectedVal, largestDiffActualVal);

	return true;
}

int SystolicArraySim::MmaTest(size_t mCnt, size_t nCnt, bool cSim, bool fiEn, bool fastTrans, bool FastTransTest)
{
	SystolicArraySim sysArraySim;

	const size_t rowCnt = mCnt * sysArraySim.Mmma();
	const size_t colCnt = nCnt * sysArraySim.Nmma();

	std::shared_ptr<double[]> matA = randomMatrix(rowCnt, sysArraySim.Kmma(), sysArraySim.Kmma());
	std::shared_ptr<double[]> matB = randomMatrix(sysArraySim.Kmma(), colCnt, colCnt);
	std::shared_ptr<double[]> matC = randomMatrix(rowCnt, colCnt, colCnt);

	std::vector<double> expected(rowCnt * colCnt);
	memcpy(expected.data(), matC.get(), sizeof(double) * rowCnt * colCnt);
	for(size_t row = 0; row < rowCnt; row++)
	{
		for(size_t col = 0; col < colCnt; col++)
		{
			for(size_t sum = 0; sum < sysArraySim.Kmma(); sum++)
			{
				expected[row * colCnt + col] += matA[row * sysArraySim.Kmma() + sum] * matB[sum * colCnt + col];
			}
		}
	}

	for(size_t jobm = 0; jobm < mCnt; jobm++)
	{
		for(size_t jobn = 0; jobn < nCnt; jobn++)
		{
			job_t jobStr = {
					matA.get() + jobm * sysArraySim.Mmma() * sysArraySim.Kmma(), sysArraySim.Kmma(),
					matB.get() + jobn * sysArraySim.Nmma(), nCnt * sysArraySim.Nmma(),
					matC.get() + jobm * sysArraySim.Mmma() * nCnt * sysArraySim.Nmma() + jobn * sysArraySim.Nmma(), nCnt * sysArraySim.Nmma()};

			sysArraySim.DispatchMma(jobStr);
		}
	}

	faultRTL_t faultRTL;
	if(fiEn)
	{
		faultRTL = sysArraySim.FiSetRTL(fiMode::Transient);
		if(fiMode::None == faultRTL.Mode)
		{
			sasError("FiSetRTL failed\n");
			return -1;
		}
	}

	if(cSim)
	{
		if(sysArraySim.ExecCsim())
		{
			sasError("ExecCsim failed\n");
			return -1;
		}
	}
	else
	{
		if(sysArraySim.ExecRtl(fastTrans, FastTransTest))
		{
			sasError("ExecCycle failed\n");
			return -1;
		}

		if(!fiEn && sysArraySim.ErrorDetected())
		{
			sasError("False positive error detected\n");
			return -1;
		}
	}

	// Check if result correct
	if(!resultCorrect(expected.data(), matC, rowCnt, colCnt))
	{
		sasError("Output not correct\n");
		return -1;
	}

	return 0;
}

int SystolicArraySim::TileTest(bool cSim)
{
	SystolicArraySim sysArraySim;

	std::shared_ptr<double[]> matA = randomMatrix(sysArraySim.Mtile(), sysArraySim.Ktile(), sysArraySim.Ktile());
	std::shared_ptr<double[]> matB = randomMatrix(sysArraySim.Ktile(), sysArraySim.Ntile(), sysArraySim.Ntile());
	std::shared_ptr<double[]> matC = randomMatrix(sysArraySim.Mtile(), sysArraySim.Ntile(), sysArraySim.Ntile());

	std::vector<double> expected(sysArraySim.Mtile() * sysArraySim.Ntile());
	memcpy(expected.data(), matC.get(), sizeof(double) * sysArraySim.Mtile() * sysArraySim.Ntile());
	for(size_t row = 0; row < sysArraySim.Mtile(); row++)
	{
		for(size_t col = 0; col < sysArraySim.Ntile(); col++)
		{
			for(size_t sum = 0; sum < sysArraySim.Ktile(); sum++)
			{
				expected[row * sysArraySim.Ntile() + col] += matA[row * sysArraySim.Ktile() + sum] * matB[sum * sysArraySim.Ntile() + col];
			}
		}
	}

	job_t job = {
			matA.get(), sysArraySim.Ktile(),
			matB.get(), sysArraySim.Ntile(),
			matC.get(), sysArraySim.Ntile()
	};

	sysArraySim.DispatchTile(job);

	if(cSim)
	{
		if(sysArraySim.ExecCsim())
		{
			sasError("ExecCsim failed\n");
			return -1;
		}
	}
	else
	{
		if(sysArraySim.ExecRtl())
		{
			sasError("ExecCycle failed\n");
			return -1;
		}
	}

	// Check if result correct
	if(!resultCorrect(expected.data(), matC, sysArraySim.Mtile(), sysArraySim.Ntile()))
	{
		sasError("Output not correct\n");
		return -1;
	}

	return 0;
}

int SystolicArraySim::MultiMmaTest(bool cSim)
{
	SystolicArraySim sysArraySim;

	const size_t MmaMultipleCnt = 2;

	const size_t M = MmaMultipleCnt * sysArraySim.Mmma();
	const size_t K = 2 * sysArraySim.Kmma();
	const size_t N = MmaMultipleCnt * sysArraySim.Nmma();

	std::shared_ptr<double[]> Arand = randomMatrix(M, K, K);
	std::shared_ptr<double[]> Brand = randomMatrix(K, N, N);
	std::shared_ptr<double[]> Crand = randomMatrix(M, N, N);

	// Prepare output
	std::vector<double> out(M * N);
	for(size_t row = 0; row < M; row++)
	{
		for(size_t col = 0; col < N; col++)
		{
			out[row * N + col] = Crand[row * N + col];
		}
	}

	// Dispatch to SA
	for(long sum = 0; sum + sysArraySim.Kmma() <= K; sum += sysArraySim.Kmma())
	{
		SystolicArraySim::job_t job = {
				Arand.get() + sum, K,
				Brand.get() + sum * N, N,
				out.data(), N};


		if(sysArraySim.DispatchMma(job, MmaMultipleCnt, MmaMultipleCnt))
		{
			sasError("DispatchMma failed\n");
			return -5;
		}
	}

	if(!cSim)
	{
		if(sysArraySim.ExecRtl(false, false))
		{
			sasError("ExecRtl failed\n");
			return -6;
		}
	}
	else
	{
		if(sysArraySim.ExecCsim())
		{
			sasError("ExecCsim failed\n");
			return -6;
		}
	}

	// Calculate expected result
	std::vector<double> expected(M * N);
	for(size_t row = 0; row < M; row++)
	{
		for(size_t col = 0; col < N; col++)
		{
			expected[row * N + col] = Crand[row * N + col];

			for(size_t sum = 0; sum < K; sum++)
			{
				expected[row * N + col] += Arand[row * K + sum] * Brand[sum * N + col];
			}
		}
	}

	// Check if result correct
	if(!resultCorrect(expected.data(), out.data(), M, N))
	{
		sasError("Output not correct\n");
		return -1;
	}

	return 0;
}

int SystolicArraySim::GemmTest(bool cSim, const double * matA, const double * matB, const double * matC, size_t M, size_t K, size_t N)
{
	SystolicArraySim sysArraySim;

	// Choose output tile size
	const bool tileEn = (M > sysArraySim.Mtile()) && (N > sysArraySim.Ntile());

	const long outMCnt = tileEn ? sysArraySim.Mtile() : sysArraySim.Mmma();
	const long outNCnt = tileEn ? sysArraySim.Ntile() : sysArraySim.Nmma();
	const long outKCnt = tileEn ? sysArraySim.Ktile() : sysArraySim.Kmma();

	// Prepare output
	std::vector<double> out(M * N);
	for(size_t row = 0; row < (M / outMCnt) * outMCnt; row++)
	{
		for(size_t col = 0; col < (N / outNCnt) * outNCnt; col++)
		{
			out[row * N + col] = matC[row * N + col];
		}
	}

	// Dispatch to SA
	for(long sum = 0; sum + outKCnt <= K; sum += outKCnt)
	{
		for(long outMPos = 0; outMPos + outMCnt <= M; outMPos += outMCnt)
		{
			for(long outNPos = 0; outNPos + outNCnt <= N; outNPos += outNCnt)
			{
				SystolicArraySim::job_t job = {
						matA + outMPos * K + sum, K,
						matB + sum * N + outNPos, N,
						out.data() + outMPos * N + outNPos, N};

				if(tileEn)
				{
					if(sysArraySim.DispatchTile(job))
					{
						sasError("DispatchTile failed\n");
						return -5;
					}
				}
				else
				{
					if(sysArraySim.DispatchMma(job))
					{
						sasError("DispatchMma failed\n");
						return -5;
					}
				}
			}
		}
	}

	if(!cSim)
	{
		if(sysArraySim.ExecRtl(false, false))
		{
			sasError("ExecRtl failed\n");
			return -6;
		}
	}
	else
	{
		if(sysArraySim.ExecCsim())
		{
			sasError("ExecCsim failed\n");
			return -6;
		}
	}

	// Handle K-rest?
	if(0 != (K % outKCnt))
	{
		for(long row = 0; row < (M / outMCnt) * outMCnt; row++)
		{
			for(long col = 0; col < (N / outNCnt) * outNCnt; col++)
			{
				for(long sum = outKCnt * (K / outKCnt); sum < K; sum++)
				{
					out.data()[row * N + col] += matA[row * K + sum] * matB[sum * N + col];
				}
			}
		}
	}

	// Calculate expected result
	std::vector<double> expected(M * N);
	for(size_t row = 0; row < (M / outMCnt) * outMCnt; row++)
	{
		for(size_t col = 0; col < (N / outNCnt) * outNCnt; col++)
		{
			expected[row * N + col] = matC[row * N + col];

			for(size_t sum = 0; sum < K; sum++)
			{
				expected[row * N + col] += matA[row * K + sum] * matB[sum * N + col];
			}
		}
	}

	// Check if result correct
	if(!resultCorrect(expected.data(), out.data(), M, N))
	{
		sasError("Output not correct\n");
		return -1;
	}

	return 0;
}

int SystolicArraySim::UnitTestNoFi(int exponentRange)
{
	unitTestExponentRange = exponentRange; // TODO: Having this global is ugly

	for(size_t mCnt = 1; mCnt < 8; mCnt++)
	{
		for(size_t nCnt = 1; nCnt < 8; nCnt++)
		{
			if(MmaTest(mCnt, nCnt, false, false, false, false))
			{
				sasError("rtl MmaTest (mCnt = %lu, nCnt = %lu) failed\n", mCnt, nCnt);
				return -1;
			}
		}
	}

	if(MultiMmaTest(false))
	{
		sasError("rtl MultiMmaTest failed\n");
		return -1;
	}

	if(TileTest(false))
	{
		sasError("rtl TileTest failed\n");
		return -1;
	}

	for(size_t matrixTest = 0; matrixTest < 5; matrixTest++)
	{
		std::shared_ptr<double[]> Arand = randomMatrix(14, 27, 27);
		std::shared_ptr<double[]> Brand = randomMatrix(27, 27, 27);
		std::shared_ptr<double[]> Crand = randomMatrix(14, 27, 27);

		if(GemmTest(false, Arand.get(), Brand.get(), Crand.get(), 14, 27, 27))
		{
			sasError("rtl GemmTest failed\n");
			return -1;
		}
	}

	return 0;
}

int SystolicArraySim::UnitTest()
{
	// Test cSim
	for(size_t mCnt = 1; mCnt < 8; mCnt++)
	{
		for(size_t nCnt = 1; nCnt < 8; nCnt++)
		{
			if(MmaTest(mCnt, nCnt, true, false, false, false))
			{
				sasError("cSim MmaTest failed (mCnt=%lu, nCnt=%lu)\n", mCnt, nCnt);
				return -1;
			}
		}
	}

	if(MultiMmaTest(true))
	{
		sasError("cSim MultiMmaTest failed\n");
		return -1;
	}

	if(TileTest(true))
	{
		sasError("cSim TileTest failed\n");
		return -1;
	}

	for(size_t matrixTest = 0; matrixTest < 5; matrixTest++)
	{
		std::shared_ptr<double[]> Arand = randomMatrix(14, 27, 27);
		std::shared_ptr<double[]> Brand = randomMatrix(27, 27, 27);
		std::shared_ptr<double[]> Crand = randomMatrix(14, 27, 27);

		if(GemmTest(true, Arand.get(), Brand.get(), Crand.get(), 14, 27, 27))
		{
			sasError("cSim GemmTest failed\n");
			return -1;
		}
	}

	// Test rtl
	// Test stuff without faults
	if(UnitTestNoFi(5))
	{
		sasError("UnitTestNoFi failed (exp. Range %i)\n", unitTestExponentRange);
		return -1;
	}

	if(UnitTestNoFi(100))
	{
		sasError("UnitTestNoFi failed (exp. Range %i)\n", unitTestExponentRange);
		return -1;
	}

#ifdef NETLIST
	// Test stuff with faults (and fast trans)
	unitTestExponentRange = 10; // TODO: Having this global is ugly
	for(size_t mCnt = 1; mCnt < 8; mCnt++)
	{
		for(size_t nCnt = 1; nCnt < 8; nCnt++)
		{
			if(MmaTest(mCnt, nCnt, false, true, true, true))
			{
				sasError("rtl fast transient MmaTest failed (mCnt=%lu, nCnt=%lu)\n", mCnt, nCnt);
				return -1;
			}
		}
	}
#endif // NETLIST

	return 0;
}
