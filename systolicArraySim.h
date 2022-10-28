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

#ifndef SYSTOLICARRAYSIM_H_
#define SYSTOLICARRAYSIM_H_

#include <stdint.h>

#include <deque>
#include <vector>

class SystolicArraySim {
public:
	// NOTE: Constructor assumes srand() was called!
	SystolicArraySim();
	virtual ~SystolicArraySim();

	// Prevent copying (alternatively, implement copy/asgn duplicating cpy)
	SystolicArraySim & operator=(const SystolicArraySim&) = delete; // assignment operator
	SystolicArraySim(const SystolicArraySim &sa) = delete; // copy constructor

	const size_t &Mmma() const {return Config_.Mmma;};
	const size_t &Kmma() const {return Config_.Kmma;};
	const size_t &Nmma() const {return Config_.Nmma;};

	const size_t &Mtile() const {return Config_.Mtile;};
	const size_t &Ktile() const {return Config_.Kmma;};
	const size_t &Ntile() const {return Config_.Ntile;};

	const size_t &ThreadsPerSA() const {return Config_.ThreadCnt;};
	const size_t &SACnt() const {return Config_.SystolicArrayCnt;};

	size_t RequiredOutPositionsBetweenK() const {return 4;}; // = jobCycleDone / jobCyclePassedFirstStage // TODO: Put these into header

	typedef struct {
		const double * MatA; // row-major Mmma x Kmma / Mtile x Ktile matrix
		size_t StrideA; // >= Kmma / Ktile
		const double * MatB; // row-major Kmma x Nmma / .. tile matrix
		size_t StrideB; // >= Nmma / Ntile
		double * MatC; // row-major Mmma x Nmma / .. tile matrix
		size_t StrideC; // >= Nmma / Ntile
	} job_t;

	int DispatchMma(const job_t &job);
	int DispatchMma(const job_t &job, size_t mCnt, size_t nCnt); // mCnt (nCnt) MMA-sized rows (columns)
	int DispatchTile(const job_t &job); // optimized for buffer architecture

	// Exec will write to MatC as specified in job
	// fastTransient : Don't run simulation if transient fault not active
	// fastTransientTest: Pretend to be doing a fault injection, just don't set the fault (check if fastTransient works)
	int ExecRtl(bool fastTransient = false, bool fastTransientTest = false);
	int ExecCsim(size_t maxJobs = SIZE_MAX);

	bool ErrorDetected() const {return DieError_;}; //  parity, residue, or protocol error raised inside RTL

	static int UnitTest(); // Assumes srand was called outside!
	static int UnitTestNoFi(int exponentRange);

	// Fault stuff

	// For Csim fault sim
	enum class fiCorruption {
		None,
		StuckHigh,
		StuckLow,
		Flip};

	enum class fiMode {
		None,
		Transient,
		Permanent};

	enum class fiBits {
		None,
		Everywhere,
		Mantissa};

	enum class fiCsimPlace {
		None,
		Everywhere,
		Inputs,
		Multipliers,
		AccAdders,
		ColumnAdders}; // Don't add enums without chaing FiSetCsim

	typedef struct {
		fiCsimPlace Place = fiCsimPlace::None;
		fiCorruption Corruption = fiCorruption::None;
		fiMode Mode = fiMode::None;
		uint8_t BitPos = UINT8_MAX;
		uint8_t Row = 0;
	} faultCsim_t;

	// Returns the actual (random) fault chosen
	// NOTE: If transient fault is chosen, it will execute randomly
	// within current job-Queue - so dispatch jobs first.
	// Struct elements are set to "None" upon error
	faultCsim_t FiSetCsim(
			fiCsimPlace place,
			fiBits bits,
			fiCorruption corruption,
			fiMode mode);

	int FiResetCsim();

	// For RTL fault sim
	typedef struct {
		std::vector<uint16_t> ModuleInstanceChain;
		uint32_t AssignUUID = 0;
		uint16_t BitPos = UINT16_MAX;
		fiMode Mode = fiMode::None;
	} faultRTL_t;

	// Returns the actual (random) fault chosen
	// NOTE: If transient fault is chosen, it will execute randomly
	// within current job-Queue - so dispatch jobs first.
	// Struct elements are set to "None" upon error
	faultRTL_t FiSetRTL(fiMode mode);
	int FiResetRTL();

private:

	size_t CycleCnt_ = 0;
	bool DieError_ = false;

	typedef struct {
		size_t Mmma; // rcount
		size_t Kmma; // depth
		size_t Nmma; // exec_size
		size_t BufferLeftSize; // how many Mmma x Kmma fit in there
		size_t BufferRightSize; // how many Kmma x Nmma fit in there
		size_t Mtile;
		size_t Ntile;
		size_t ThreadCnt; // how many threads run in parallel on one SA?
		size_t SystolicArrayCnt; // how many SAs work in parallel?
	} config_t;

	const config_t Config_ = {
			8, 8, 8, // Mmma, Kmma, Nmma
			8, 2, // BufferLeftSize, BufferRightSize
			8 * 4, 4 * 8, // Mtile, Ntile
			4, 16}; // ThreadCnt, SystolicArrayCnt
	void * TbVoid_;

	typedef struct {
		size_t JobCycle;
		job_t Job;
	} queueEntry_t;

	std::deque<queueEntry_t> JobQueue_;
	bool JobQueueReadBeforeWrite(const std::deque<queueEntry_t> &jobQueue) const;

	int RowCsim(double * out, double * a, double * b, const faultCsim_t * fi = nullptr) const;

	int IoSet(void * Tb, std::deque<queueEntry_t> * jobs, bool clkHigh);

	const size_t FmaCycles_ = 12;
	const size_t JobCycleOutputStart_ = (Kmma() / 2) * FmaCycles_ + 4;
	const size_t JobCycleDone_ = JobCycleOutputStart_ + 2 * (Nmma() - 1);
	const size_t JobCyclePassedFirstStage_ = 2 * Nmma() + 1;

	size_t CyclesRequired(size_t jobCnt) const;
	size_t JobsDoneInCycles(size_t cycleCnt) const;

	static int MmaTest(size_t mCnt, size_t nCnt, bool cSim, bool fiEn, bool fastTrans, bool FastTransTest);
	static int MultiMmaTest(bool cSim);
	static int TileTest(bool cSim);
	static int GemmTest(bool cSim, const double * A, const double * B, const double * C, size_t M, size_t K, size_t N);

	// Fault stuff
	// For Csim fault sim
	faultCsim_t FaultCsim_;
	size_t FaultCsimTransCycle_ = SIZE_MAX; // for transient faults: In which cycle should fault occur?

	// For RTL fault sim
	faultRTL_t FaultRTL_;
	size_t FaultRTLTransCycle_ = SIZE_MAX; // for transient faults: In which cycle should fault occur?
	void * NetlistFaultInjectorVoid_ = nullptr;

	static int FiRtlApply(void * TbVoid, const std::vector<uint16_t> &modInst, uint32_t assignNr, size_t fiBit);
	static int FiRtlReset(void * TbVoid);
};

#endif /* SYSTOLICARRAYSIM_H_ */
