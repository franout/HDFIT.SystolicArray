/*
 * Copyright (c) 2022, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. Neither the name of the TensorFlow project nor the names of
 *    its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define HW_SIMULATION 1 
#define HW_RTL_SIMULATION 1
#define TEST_EN 0
#define VERBOSE_OPS_OUTPUT_EN 1

// TODO: The following quickfix handles Systolic Array pipeline read-before-write issues
// When dispatching single MMAs (i.e. not tiled), care needs to be taken that
// They do not read/write to the same output positions if they'll be in the
// Systolic Array's pipeline at the same time. This is a "quickfix" because it assumes
// knowledge on the SA pipeline depth
#define OUT_POSITION_QUICKFIX_EN 1

#include <stdio.h>
#include <float.h>

#include <string>
#include <cstdlib>
#include <memory>
#include <sys/time.h>

#include <stddef.h>
#include <stdint.h>


#if HW_SIMULATION
#include "systolicArraySim.h"
#endif // HW_SIMULATION

#include "faultInjector.h"

static size_t errorCnt = 0;
static size_t warningCnt = 0;

#define fiError(...) \
		do { \
			fprintf(stderr, "Error (%s:%i): ", __FILE__, __LINE__); \
			fprintf(stderr, __VA_ARGS__); \
			fflush(stderr); \
			errorCnt++; \
		} while(0)

#define WARNING_EN 0
#if WARNING_EN
#define fiWarning(...) \
	do { \
		printf("Warning (%s:%i): ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		fflush(stdout); \
		warningCnt++; \
	} while(0)
#else
#define fiWarning(...) \
	do { \
		warningCnt++; \
	} while(0)
#endif

#define DEBUG_EN 0
#if DEBUG_EN
#define fiDebug(...) \
	do { \
		printf(__VA_ARGS__); \
		fflush(stdout); \
	} while(0)
#else // !DEBUG_EN
#define fiDebug(...)
#endif // !DEBUG_EN

#if 0
#define fiFaultDebug(...) \
	do { \
		printf(__VA_ARGS__); \
		fflush(stdout); \
	} while(0)
#else
#define fiFaultDebug(...)
#endif

#define fiInfo(...) \
	do { \
		printf(__VA_ARGS__); \
		fflush(stdout); \
	} while(0)

typedef enum {
        TFFIMODE_NONE,
        TFFIMODE_TRANSIENT,
        TFFIMODE_PERMANENT
} TfFiMode_t;

typedef enum {
        TFFICORRUPTION_NONE,
        TFFICORRUPTION_STUCKHIGH,
        TFFICORRUPTION_STUCKLOW,
        TFFICORRUPTION_FLIP
} TfFiCorruption_t;

typedef enum {
        TFFIBITS_NONE,
        TFFIBITS_EVERYWHERE,
        TFFIBITS_MANTISSA
} TfFiBits_t;

typedef struct {
        size_t OpsCntTotal; // specified in advance by user
        size_t OpsCnt; // current running ops cnt
        size_t OpFi; // Call Fi at this op

        // Non RTL Sim:
        size_t OpFiBitPos; // Bit pos. for last fi call

        // Non HW rel. error sim:
        float OpFiRelError; // Relative error

        // RTL Sim:
        int8_t ErrorDetected; //  parity, residue, or protocol error raised inside RTL
        std::vector<uint16_t> ModuleInstanceChain;
        uint32_t AssignUUID;
        uint16_t BitPos;

        // Other
        TfFiMode_t Mode;
        TfFiCorruption_t Corruption;
        TfFiBits_t Bits;

        // MPI rank of process
        int Rank;

        // Pointer to file storing TfFI output
        FILE* OutFile;

        void* MmaFi;
} TfFi_t;

#if OUT_POSITION_QUICKFIX_EN
static const size_t mMmaPositions_quickfix = 2;
static const size_t nMmaPositions_quickfix = 2;
#endif // OUT_POSITION_QUICKFIX_EN

static TfFi_t * TfFi = NULL;

static uint64_t rand_uint64() {
	uint64_t val = 0;
	for (size_t i=0; i<5; i++) {
		// Calling rand() 5 times, leveraging 15 bits at a time
		// RAND_MAX is guaranteed to be at least 32767
		// Multiplying val by RAND_MAX + 1 is equivalent to bit-shifting
		// by RAND_MAX'2 bit width - assuming it is a power of 2 - 1
		// After that, we can sum (or bitwise OR) to a new rand() call

		// coverity[DC.WEAK_CRYPTO]
		val = val * ((uint64_t)RAND_MAX + 1) + rand();
	}
	return val;
}

__attribute__((visibility("default"))) int TfFiInit(int rank)
{
	fiDebug("%s called\n", __func__);

	if(TfFi != NULL)
	{
		fiError("Library already initialized\n");
		return -1;
	}

	TfFi = new TfFi_t;

	// Init rand
	timeval tod;
	if(gettimeofday(&tod, NULL))
	{
		fiError("gettimeofday failed\n");
		return -1;
	}

	srand(tod.tv_usec * tod.tv_sec);

	TfFi->Rank = rank;
	TfFi->OpFi = SIZE_MAX;
	TfFi->OpsCnt = 0;
	TfFi->OpsCntTotal = 0;

	TfFi->Mode = TFFIMODE_NONE;
	TfFi->Corruption = TFFICORRUPTION_NONE;
	TfFi->Bits = TFFIBITS_NONE;

	TfFi->OpFiBitPos = 0;
	TfFi->OpFiRelError = 0;
	TfFi->ErrorDetected = 0;

	

#if HW_SIMULATION
	TfFi->MmaFi = (void*) new SystolicArraySim();
#else // !HW_SIMULATION
	TfFi->MmaFi = nullptr;
#endif // !HW_SIMULATION

	// Using stdout as default output channel
	TfFi->OutFile = stdout;
	if(const char* fiFile_env = std::getenv(TFFIOUTPUT_ENV_VAR)) {
		std::string fiFile(fiFile_env);
		if(fiFile == TFFIOUTPUT_STDOUT_CONST) {
			TfFi->OutFile = stdout;
		} else if(fiFile == TFFIOUTPUT_STDERR_CONST) {
			TfFi->OutFile = stderr;
		} else if((TfFi->OutFile = fopen(fiFile_env, "a")) == NULL){
			fiError("Unable to open file %s for output!\n", fiFile_env);
			return -1;
		}
	}

	return 0;
}

#if HW_SIMULATION
#if 0
static int mmaFiExec(SystolicArraySim * saSim)
{
#if HW_RTL_SIMULATION
	return saSim->ExecRtl(true);
#else // !HW_RTL_SIMULATION
	return saSim->ExecCsim();
#endif // !HW_RTL_SIMULATION

	fiError("Shouldn't get here\n");

	return -1;
}

static int mmaFiReset(SystolicArraySim * saSim)
{
#if HW_RTL_SIMULATION
	return saSim->FiResetRTL();
#else // !HW_RTL_SIMULATION
	return saSim->FiResetCsim();
#endif // !HW_RTL_SIMULATION

	fiError("Shouldn't get here\n");

	return -1;
}

static int mmaFiSet(SystolicArraySim * saSim, TfFi_t * TfFi)
{
	SystolicArraySim::fiMode mode;
	switch(TfFi->Mode)
	{
	default: // no break intended
	case TFFIMODE_NONE:
		fiError("mmaFiSet with unknown error\n");
		return -1;

	case TFFIMODE_TRANSIENT:
		mode = SystolicArraySim::fiMode::Transient;
		break;

	case TFFIMODE_PERMANENT:
		mode = SystolicArraySim::fiMode::Permanent;
		break;
	}

#if HW_RTL_SIMULATION
	if((TFFIBITS_EVERYWHERE != TfFi->Bits) || (TFFICORRUPTION_FLIP != TfFi->Corruption))
	{
		fiError("RTL Simulation only supports TFFIBITS_EVERYWHERE and TFFICORRUPTION_FLIP\n");
		return -4;
	}

	SystolicArraySim::faultRTL_t fault = saSim->FiSetRTL(mode);
	if(SystolicArraySim::fiMode::None == fault.Mode)
	{
		fiError("FiSetRTL failed\n");
		return -5;
	}

	TfFi->AssignUUID = fault.AssignUUID;
	TfFi->BitPos = fault.BitPos;
	TfFi->ModuleInstanceChain = fault.ModuleInstanceChain;

#else // !HW_RTL_SIMULATION
	SystolicArraySim::fiBits bits;
	switch(TfFi->Bits)
	{
	default: // no break intended
	case TFFIBITS_NONE:
		fiError("mmaFiSet with unknown error\n");
		return -1;

	case TFFIBITS_EVERYWHERE:
		bits = SystolicArraySim::fiBits::Everywhere;
		break;

	case TFFIBITS_MANTISSA:
		bits = SystolicArraySim::fiBits::Mantissa;
		break;
	}

	SystolicArraySim::fiCorruption corruption;
	switch(TfFi->Corruption)
	{
	default: // no break intended
	case TFFICORRUPTION_NONE:
		fiError("mmaFiSet with unknown error\n");
		return -1;

	case TFFICORRUPTION_STUCKHIGH:
		corruption = SystolicArraySim::fiCorruption::StuckHigh;
		break;

	case TFFICORRUPTION_STUCKLOW:
		corruption = SystolicArraySim::fiCorruption::StuckLow;
		break;

	case TFFICORRUPTION_FLIP:
		corruption = SystolicArraySim::fiCorruption::Flip;
		break;
	}

	SystolicArraySim::faultCsim_t fault = saSim->FiSetCsim(
			SystolicArraySim::fiCsimPlace::Everywhere,
			bits,
			corruption,
			mode);

	if(SystolicArraySim::fiCsimPlace::None == fault.Place)
	{
		fiError("FiSetCsim failed\n");
		return -5;
	}

	TfFi->OpFiBitPos = fault.BitPos;
#endif // !HW_RTL_SIMULATION

	return 0;
}
#endif // HW_SIMULATION
#endif 

__attribute__((visibility("default"))) int TfFiSet()
{
	if(NULL == TfFi)
	{
		fiError("Library uninitialized\n");
 		return -1;
	}

	fiDebug("%s called\n", __func__);

	TfFi->OpsCntTotal = 0;
	if(const char* OpsCntTotal_env = std::getenv(TFFIOPSCNT_ENV_VAR)) {
		try {
			TfFi->OpsCntTotal = (size_t)std::stoull(OpsCntTotal_env);
		} catch (const std::exception& e) {
			fiError("Invalid %s setting for environment variable %s!\n", OpsCntTotal_env, TFFIOPSCNT_ENV_VAR);
			return -1;
		}
	} else {
		fiError("%s environment variable uninitialized!\n", TFFIOPSCNT_ENV_VAR);
		return -1;
	}

	TfFi->OpFi = TfFi->OpsCntTotal>0 ? rand_uint64() % TfFi->OpsCntTotal : 0;
	TfFi->OpsCnt = 0;
        
	TfFi->Mode = TFFIMODE_NONE;
	if(const char* fiMode_env = std::getenv(TFFIMODE_ENV_VAR)) {
		std::string fiMode(fiMode_env);
		if(fiMode == TFFIMODE_TRANSIENT_CONST) {
			TfFi->Mode = TFFIMODE_TRANSIENT;
		} else if(fiMode == TFFIMODE_PERMANENT_CONST) {
			TfFi->Mode = TFFIMODE_PERMANENT;
		} else if(fiMode != TFFIMODE_NONE_CONST) {
			fiError("Invalid %s setting for environment variable %s!\n", fiMode_env, TFFIMODE_ENV_VAR);
			return -1;
		}
	} else {
		fiError("%s environment variable uninitialized!\n", TFFIMODE_ENV_VAR);
		return -1;
	}

	TfFi->Corruption = TFFICORRUPTION_NONE;
	if(const char* fiCorruption_env = std::getenv(TFFICORRUPTION_ENV_VAR)) {
		std::string fiCorruption(fiCorruption_env);
		if(fiCorruption == TFFICORRUPTION_STUCKHIGH_CONST) {
			TfFi->Corruption = TFFICORRUPTION_STUCKHIGH;
		} else if(fiCorruption == TFFICORRUPTION_STUCKLOW_CONST) {
			TfFi->Corruption = TFFICORRUPTION_STUCKLOW;
		} else if(fiCorruption == TFFICORRUPTION_FLIP_CONST) {
			TfFi->Corruption = TFFICORRUPTION_FLIP;
		} else if(fiCorruption != TFFICORRUPTION_NONE_CONST) {
			fiError("Invalid %s setting for environment variable %s!\n", fiCorruption_env, TFFICORRUPTION_ENV_VAR);
			return -1;
		}
	} else {
		fiError("%s environment variable uninitialized!\n", TFFICORRUPTION_ENV_VAR);
		return -1;
	}

	TfFi->Bits = TFFIBITS_NONE;
	if(const char* fiBits_env = std::getenv(TFFIBITS_ENV_VAR)) {
		std::string fiBits(fiBits_env);
		if(fiBits == TFFIBITS_EVERYWHERE_CONST) {
			TfFi->Bits = TFFIBITS_EVERYWHERE;
		} else if(fiBits == TFFIBITS_MANTISSA_CONST) {
			TfFi->Bits = TFFIBITS_MANTISSA;
		} else if(fiBits != TFFIBITS_NONE_CONST) {
			fiError("Invalid %s setting for environment variable %s!\n", fiBits_env, TFFIBITS_ENV_VAR);
			return -1;
		}
	} else {
		fiError("%s environment variable uninitialized!\n", TFFIBITS_ENV_VAR);
		return -1;
	}

#if (HW_SIMULATION && HW_RTL_SIMULATION)
	if ((TFFIBITS_EVERYWHERE != TfFi->Bits) && (TFFIBITS_NONE != TfFi->Bits))
	{
		fiError("Can't specify bit positions in RTL simulation\n");
		return -4;
	}

	if ((TFFICORRUPTION_FLIP != TfFi->Corruption) && (TFFICORRUPTION_NONE != TfFi->Corruption))
	{
		fiError("Corruption other than flip not implemented for RTL simulation\n");
		return -4;
	}
#endif // (HW_SIMULATION && HW_RTL_SIMULATION)

	if(TFFIMODE_PERMANENT == TfFi->Mode)
	{
#if HW_SIMULATION
	/*	if(mmaFiSet((SystolicArraySim*) TfFi->MmaFi, TfFi))
		{
			fiError("mmaFiSet failed\n");
			return -4;
		}*/
#else // !HW_SIMULATION
		fiError("Can't simulate permanent faults without hw simulation\n");
		return -4;
#endif // !HW_SIMULATION
	}

	return 0;
}

__attribute__((visibility("default"))) void TfFiPrint()
{
	if(NULL == TfFi)
	{
		fiError("Library uninitialized\n");
		return;
	}

	if(NULL == TfFi->OutFile)
	{
		fiError("No valid output file set\n");
		return;
	}

#if (!VERBOSE_OPS_OUTPUT_EN)
	if(TfFi->Rank != 0 && TfFi->Mode == TFFIMODE_NONE) { return; }
#endif

	fprintf(TfFi->OutFile, "[HDFIT]\t Rank %i: OpsCnt = %lu\n", TfFi->Rank, TfFi->OpsCnt);
	if(TfFi->Mode != TFFIMODE_NONE) {
		fprintf(TfFi->OutFile, "[HDFIT]\t\t FI enabled on rank = %i\n", TfFi->Rank);
		fprintf(TfFi->OutFile, "[HDFIT]\t\t FI at op = %lu\n", TfFi->OpFi);
#if (HW_SIMULATION && HW_RTL_SIMULATION)
		fprintf(TfFi->OutFile, "[HDFIT]\t\t RTL errors = %u\n", TfFi->ErrorDetected);
		fprintf(TfFi->OutFile, "[HDFIT]\t\t Assign UUID = %u\n", TfFi->AssignUUID);
		fprintf(TfFi->OutFile, "[HDFIT]\t\t Module instance chain = ");
		if( TfFi->ModuleInstanceChain.size() == 0 ) {
			fprintf(TfFi->OutFile, "0\n");
		} else {
			for(size_t idx=0; idx < TfFi->ModuleInstanceChain.size(); idx++) {
				fprintf(TfFi->OutFile, (idx==0 ? "%u" : "-%u"), TfFi->ModuleInstanceChain[idx]);
			}
			fprintf(TfFi->OutFile, "\n");
		}
		fprintf(TfFi->OutFile, "[HDFIT]\t\t Bit pos = %u\n", TfFi->BitPos);
#else
		fprintf(TfFi->OutFile, "[HDFIT]\t\t Bit pos = %lu\n", TfFi->OpFiBitPos);
#endif // (HW_SIMULATION && HW_RTL_SIMULATION)
		if(warningCnt>0) {
			fprintf(TfFi->OutFile, "[HDFIT]\t\t This run produced one or more warnings.\n");
#if (WARNING_EN==0)
			fprintf(TfFi->OutFile, "[HDFIT]\t\t Enable WARNING_EN in order to see them.\n");
#endif
		}
		fflush(TfFi->OutFile);
	}
}

__attribute__((visibility("default"))) int TfFiClose() 
{
	if (NULL == TfFi)
	{
		fiError("Library uninitialized\n");
		return -1;
	}

#if HW_SIMULATION
	if (TfFi->MmaFi != NULL) {
		delete (SystolicArraySim*)TfFi->MmaFi;
		TfFi->MmaFi = NULL;
	}
#endif // HW_SIMULATION

	
	//TODO: do stdout and stderr stay constant over time?
	if (TfFi->OutFile != NULL && TfFi->OutFile != stdout && TfFi->OutFile != stderr) {
		fclose(TfFi->OutFile);
	}

	delete TfFi;
	TfFi = NULL;
	return 0;
}
#if 0
// [in] 			column major matrix
// [trans] 			if nonzero, the underlying array holds the transpose of in
// [rowCnt, colCnt] Number of rows / columns of in
// [ld] 			column-stride for underlying array. I.e. if trans the value is at least colCnt, rowCnt otherwise.
// returns pointer to row major matrix with standard strides
template<typename T>
static std::shared_ptr<T[]> toRowMajorStandardStride(const T * in, int trans, long ld, long rowCnt, long colCnt)
{
	if(nullptr == in)
	{
		fiError("nullptr\n");
		return nullptr;
	}

	if((0 > trans) ||(1 < trans))
	{
		fiError("Unsupported Trans\n");
		return nullptr;
	}

	std::shared_ptr<T[]> out(new T[rowCnt * colCnt]);

	if((trans) && (ld == rowCnt))
	{
		// Nothing to do
		memcpy(out.get(), in, sizeof(T) * rowCnt * colCnt);
		return out;
	}

	for(long row = 0; row < rowCnt; row++)
	{
		for(long col = 0; col < colCnt; col++)
		{
			// non-trans: ld >= rowCnt, else ld >= colCnt
			const long inIndex = trans ? row * ld + col : row + col * ld;
			out[row * colCnt + col] = in[inIndex];
		}
	}

	return out;
}

// [in]		row major matrix with standard stride
// [inOut]	col major matrix with specified trans and ld.
//			Only affected elements in inOut will be overwritten
template<typename T>
static int toColMajorWithStride(T * inOut, const T * in, int trans, long ld, long rowCnt, long colCnt)
{
	if((nullptr == in) || (nullptr == inOut))
	{
		fiError("nullptr\n");
		return -1;
	}

	if((trans) && (ld == rowCnt))
	{
		// Nothing to do
		memcpy(inOut, in, sizeof(T) * rowCnt * colCnt);
		return 0;
	}

	for(long col = 0; col < colCnt; col++)
	{
		for(long row = 0; row < rowCnt; row++)
		{
			// non-trans: ld >= rowCnt, else ld >= colCnt
			const long outIndex = trans ? row * ld + col : row + col * ld;
			inOut[outIndex] = in[row * colCnt + col];
		}
	}

	return 0;
}

template<typename T>
static int matrixConversionTest(const T * in, int trans, long ld, long rowCnt, long colCnt)
{
	fiDebug("matrixConversionTest: trans = %i, ld = %li, rows = %li, cols = %li\n",
			trans, ld, rowCnt, colCnt);

	// Create copy of input matrix
	// non-trans: ld >= rowCnt, else ld >= colCnt
	const size_t inSize = trans ? rowCnt * ld : colCnt * ld;

	std::vector<T> inCpy(in, in + inSize);
	std::vector<T> inSafeCpyVec = inCpy;
	const T* inSafeCpy = inSafeCpyVec.data();

	std::shared_ptr<T[]> inRowMaj = toRowMajorStandardStride(in, trans, ld, rowCnt, colCnt);
	if(nullptr == inRowMaj)
	{
		fiError("toRowMajorStandardStride failed\n");
		return -1;
	}

	if(toColMajorWithStride(inCpy.data(), inRowMaj.get(), trans, ld, rowCnt, colCnt))
	{
		fiError("toColMajorWithStride failed\n");
		return -2;
	}

	if(memcmp(inSafeCpy, inCpy.data(), 8 * inSize))
	{
		fiError("matrix back-conversion doesn't match original\n");
		return -2;
	}

	return 0;
}

static int matrixConversionMulTest(int transa, int transb, blas_arg_t * args, const void * cOriginal, size_t elemSize)
{
	if(sizeof(double) != elemSize)
	{
		fiError("Unsupported Size\n");
		return -1;
	}

	fiDebug("matrixConversionMulTest: transa %i, transb %i, lda %li, ldb %li, ldc %li, alpha %f, beta %f\n",
			transa, transb, args->lda, args->ldb, args->ldc,
			*((double*) args->alpha), *((double*) args->beta));

	std::shared_ptr<double[]> matA = toRowMajorStandardStride((double*)args->a, transa, args->lda, args->m, args->k);
	if(nullptr == matA)
	{
		fiError("toRowMajorStandardStride failed\n");
		return -2;
	}

	std::shared_ptr<double[]> matB = toRowMajorStandardStride((double*)args->b, transb, args->ldb, args->k, args->n);
	if(nullptr == matB)
	{
		fiError("toRowMajorStandardStride failed\n");
		return -2;
	}

	std::vector<double> matGemm(args->m * args->n);
	for(long row = 0; row < args->m; row++)
	{
		for(long col = 0; col < args->n; col++)
		{
			matGemm[row * args->n + col] = 0;
			for(long sum = 0; sum < args->k; sum++)
			{
				matGemm[row * args->n + col] += *((double*)args->alpha) * matA[row * args->k + sum] * matB[sum * args->n + col];
			}
		}
	}

	// Add C ?
	const double beta = *((double *) args->beta);
	if(beta)
	{
		if(nullptr == cOriginal)
		{
			fiError("Original c is null!\n");
			return -4;
		}

		std::shared_ptr<double[]> matC = toRowMajorStandardStride((double*) cOriginal, 0, args->ldc, args->m, args->n);
		if(nullptr == matC)
		{
			fiError("toRowMajorStandardStride failed\n");
			return -2;
		}

		for(long index = 0; index < args->m * args->n; index++)
		{
			matGemm[index] += beta * matC[index];
		}
	}

	// convert back
	std::vector<double> cTmp(args->ldc * args->n);
		memcpy(cTmp.data(), args->c, sizeof(double) * args->ldc * args->n);

	if(toColMajorWithStride(cTmp.data(), matGemm.data(), 0, args->ldc, args->m, args->n))
	{
		fiError("toColMajorWithStride failed\n");
		return -5;
	}

	// Check results
	for(long index = 0; index < args->ldc * args->n; index++)
	{
		const double tolerance = fmax(2.e-15, fabs(((double*)args->c)[index]) / 1000.0);
		const double absDifference = fabs(cTmp[index] - ((double*)args->c)[index]);
		if(absDifference > tolerance)
		{
			fiError("Results don't match: %f vs %f (absDifference %.2e, tolerance %.2e)\n",
					cTmp[index], ((double*)args->c)[index], absDifference, tolerance);
			return -6;
		}
	}

	return 0;
}

[[maybe_unused]] static int RuntimeTests(int transa, int transb, blas_arg_t * args, const void * cOriginal, size_t elemSize)
{
	if(sizeof(double) != elemSize)
	{
		fiError("Only support double\n");
		return -1;
	}

	if(matrixConversionTest((double*)args->a, transa, args->lda, args->m, args->k))
	{
		fiError("matrixConversionTest failed\n");
		return -3;
	}

	if(matrixConversionTest((double*)args->b, transb, args->ldb, args->k, args->n))
	{
		fiError("matrixConversionTest failed\n");
		return -3;
	}

	if(matrixConversionTest((double*)args->c, 0, args->ldc, args->m, args->n))
	{
		fiError("matrixConversionTest failed\n");
		return -3;
	}

	if(matrixConversionMulTest(transa, transb, args, cOriginal, elemSize))
	{
		fiError("matrixConversionMulTest failed\n");
		return -3;
	}

	return 0;
}

#if HW_SIMULATION
static int hwFi(TfFi_t * TfFi, int transa, int transb, blas_arg_t * args, const void * cOriginal, size_t elemSize)
{
	// TODO: Most of this code should probably be moved into systolicArraySim class
	if(sizeof(double) != elemSize)
	{
		fiError("Unsupported Size\n");
		return -1;
	}

#if TEST_EN
	// Test that conversions work
	if(RuntimeTests(transa, transb, args, cOriginal, elemSize))
	{
		fiError("RuntimeTests failed\n");
		return -3;
	}
	else
	{
		fiDebug("RuntimeTests passed\n");
	}
#endif // TEST_EN

	SystolicArraySim * saSim = (SystolicArraySim*) TfFi->MmaFi;

	// Choose output tile size
	const bool tileEn = (args->m > (long) saSim->Mtile()) && (args->n > (long) saSim->Ntile());

#if OUT_POSITION_QUICKFIX_EN
	// Multiple non-tileEn K-blocks require different mma positions in between to avoid pipeline read before write
	const size_t mMmaPositions = (args->k >= 2 * saSim->Kmma()) ?  mMmaPositions_quickfix : 1;
	const size_t nMmaPositions = (args->k >= 2 * saSim->Kmma()) ?  nMmaPositions_quickfix : 1;

	const long outMCnt = tileEn ? saSim->Mtile() : mMmaPositions * saSim->Mmma();
	const long outNCnt = tileEn ? saSim->Ntile() : nMmaPositions * saSim->Nmma();
	const long outKCnt = tileEn ? saSim->Ktile() : saSim->Kmma();

#else // !OUT_POSITION_QUICKFIX_EN
	const long outMCnt = tileEn ? saSim->Mtile() : saSim->Mmma();
	const long outNCnt = tileEn ? saSim->Ntile() : saSim->Nmma();
	const long outKCnt = tileEn ? saSim->Ktile() : saSim->Kmma();
#endif // OUT_POSITION_QUICKFIX_EN

	// Choose random output tile positions
	std::vector<long> outMPos;
	std::vector<long> outNPos;

	if(TFFIMODE_TRANSIENT == TfFi->Mode) // just one tile will be affected
	{
		// coverity[DC.WEAK_CRYPTO]
		outMPos.push_back((rand() % (args->m / outMCnt)) * outMCnt);

		// coverity[DC.WEAK_CRYPTO]
		outNPos.push_back((rand() % (args->n / outNCnt)) * outNCnt);
	}
	else if(TFFIMODE_PERMANENT == TfFi->Mode) // randomly distribute job across available Systolic Arrays
	{
#ifndef IMPLEMENTED
		if(!tileEn)
		{
			fiError("Permanent faults for smaller than tile sizes not implemented\n");
			return -3;
		}
#endif // ndef IMPLEMENTED

		// TODO: Below handing is not quite correct for the case when not tileEn
		// In that case, each SA would still have a smaller tiling algorithm to optimize buffer usage
		// rather than scheduling individual mma calls
		const size_t tileCnt = (args->m / outMCnt) * (args->n / outNCnt);
		const size_t maxSAParallel = tileCnt / saSim->ThreadsPerSA() ? tileCnt / saSim->ThreadsPerSA() : 1;

		// If a Systolic Array is used, it will be used with all its threads
		size_t tilesToFiCnt = 0;
		if(maxSAParallel <= saSim->SACnt())
		{
			// More SAs than needed...will this calc use the faulty SA?
			// coverity[DC.WEAK_CRYPTO]
			const int randSA = rand() % saSim->SACnt();
			if(randSA < (int) maxSAParallel)
			{
				// If we have less threads than ThreadsPerSA we can only schedule those
				tilesToFiCnt = (saSim->ThreadsPerSA() < tileCnt) ? saSim->ThreadsPerSA() : tileCnt;
			}
		}
		else // more SAs may run in parallel than we have .. use faulty SA multiple times?
		{
			const float multiUseFactor = ((float) maxSAParallel) / saSim->SACnt();

			tilesToFiCnt = floor(multiUseFactor) * saSim->ThreadsPerSA();

			// Use one more? E.g. factor is 2.5 then there is a chance faulty SA will be used thrice
			// Get the decimals times 1000, i.e. above would give 500
			const int kiloDec = floor(multiUseFactor * 1000) - floor(multiUseFactor) * 1000;
			// coverity[DC.WEAK_CRYPTO]
			const int randSA = rand() % 1000;
			if(randSA < kiloDec) // i.e. with numbers above: if rand < 500
			{
				tilesToFiCnt += saSim->ThreadsPerSA();
			}
		}

		if(0 == tilesToFiCnt)
		{
			fiFaultDebug("Perm. fault skipping this gemm\n");
			return 0;
		}

		// Generate tilesToFiCnt random out positions
		while(outMPos.size() < tilesToFiCnt) // TODO: Very dirty way to make sure we don't get the same pos multiple times
		{
			// coverity[DC.WEAK_CRYPTO]
			const long mPosCandidate = (rand() % (args->m / outMCnt)) * outMCnt;
			// coverity[DC.WEAK_CRYPTO]
			const long nPosCandidate = (rand() % (args->n / outNCnt)) * outNCnt;

			bool posUnique = true;
			for(size_t index = 0; index < outMPos.size(); index++)
			{
				if((mPosCandidate == outMPos[index]) && (nPosCandidate == outNPos[index]))
				{
					posUnique = false;
					break;
				}
			}

			if(posUnique)
			{
				outMPos.push_back(mPosCandidate);
				outNPos.push_back(nPosCandidate);
			}
		}
	}

	fiFaultDebug("Chose %s positions: ", tileEn ? "Tile" : "Mma");
	for(size_t pos = 0; pos < outMPos.size(); pos++)
	{
		fiFaultDebug("(%lu, %lu), ", outMPos[pos], outNPos[pos]);
	}
	fiFaultDebug("\n");

	// Convert matrices to row major with standard strides
	// TODO: Save overhead by just converting the submatrices required for the simulated Systolic Array!
	std::shared_ptr<double[]> matA = toRowMajorStandardStride((double*)args->a, transa, args->lda, args->m, args->k);
	if(nullptr == matA)
	{
		fiError("toRowMajorStandardStride failed\n");
		return -2;
	}

	// apply alpha to matA
	const double alpha = *((double *) args->alpha);
	if(1.0 != alpha)
	{
		for(long index = 0; index < args->m * args->k; index++)
		{
			matA[index] *= alpha;
		}
	}

	std::shared_ptr<double[]> matB = toRowMajorStandardStride((double*)args->b, transb, args->ldb, args->k, args->n);
	if(nullptr == matB)
	{
		fiError("toRowMajorStandardStride failed\n");
		return -2;
	}

	// Reset C elements calculated by chosen output mma / tile
	// TODO: Save overhead by just converting the required submatrix from C
	std::shared_ptr<double[]> matC = toRowMajorStandardStride((double*) args->c, 0, args->ldc, args->m, args->n);
	if(nullptr == matC)
	{
		fiError("toRowMajorStandardStride failed\n");
		return -2;
	}

	// If original C is added, then restore original C for SA calculated elements, else set to 0
	const double beta = *((double *) args->beta);
	if(beta) // Original C is added
	{
		if(nullptr == cOriginal)
		{
			fiError("Original c is null!\n");
			return -4;
		}

		std::shared_ptr<double[]> matCOriginal = toRowMajorStandardStride((double*) cOriginal, 0, args->ldc, args->m, args->n);
		if(nullptr == matCOriginal)
		{
			fiError("toRowMajorStandardStride failed\n");
			return -2;
		}

		for(size_t pos = 0; pos < outMPos.size(); pos++)
		{
			for(long row = outMPos[pos]; row < outMPos[pos] + outMCnt; row++)
			{
				for(long col = outNPos[pos]; col < outNPos[pos] + outNCnt; col++)
				{
					matC[row * args->n + col] = beta * matCOriginal[row * args->n + col];
				}
			}
		}
	}
	else // Set to zero if not added (because SA only supports adding to C)
	{
		for(size_t pos = 0; pos < outMPos.size(); pos++)
		{
			for(long row = outMPos[pos]; row < outMPos[pos] + outMCnt; row++)
			{
				for(long col = outNPos[pos]; col < outNPos[pos] + outNCnt; col++)
				{
					matC[row * args->n + col] = 0;
				}
			}
		}
	}

	// Dispatch to SA
	for(size_t pos = 0; pos < outMPos.size(); pos++)
	{
		for(long sum = 0; sum + outKCnt <= args->k; sum += outKCnt)
		{
			SystolicArraySim::job_t job = {
					matA.get() + outMPos[pos] * args->k + sum, (size_t) args->k,
					matB.get() + sum * args->n + outNPos[pos], (size_t) args->n,
					matC.get() + outMPos[pos] * args->n + outNPos[pos], (size_t) args->n};

			if(tileEn)
			{
				if(saSim->DispatchTile(job))
				{
					fiError("DispatchTile failed\n");
					return -5;
				}
			}
			else
			{
				if(saSim->DispatchMma(job, mMmaPositions, nMmaPositions))
				{
					fiError("DispatchMma failed\n");
					return -5;
				}
			}
		}

		if(TFFIMODE_TRANSIENT == TfFi->Mode)
		{
			if(mmaFiSet(saSim, TfFi))
			{
				fiError("mmaFiSet failed\n");
				return -7;
			}
		}

		if(mmaFiExec(saSim))
		{
			fiError("mmaFiExec failed\n");
			return -5;
		}

		if(saSim->ErrorDetected())
		{
			TfFi->ErrorDetected = 1;
			fiInfo("RTL raised error\n");
		}

		if(TFFIMODE_TRANSIENT == TfFi->Mode)
		{
			if(mmaFiReset(saSim))
			{
				fiError("mmaFiReset failed\n");
				return -5;
			}
		}

		// Handle K-rest?
		if(0 != (args->k % outKCnt))
		{
			for(long row = outMPos[pos]; row < outMPos[pos] + outMCnt; row++)
			{
				for(long col = outNPos[pos]; col < outNPos[pos] + outNCnt; col++)
				{
					for(long sum = outKCnt * (args->k / outKCnt); sum < args->k; sum++)
					{
						matC[row * args->n + col] += matA[row * args->k + sum] * matB[sum * args->n + col];
					}
				}
			}
		}
	}

#if TEST_EN
	// Create copy of gemm output without our modifications
	const size_t elemCntC = args->n * args->ldc;
	double * expectedC = (double *) malloc(sizeof(double) * elemCntC);
	if(nullptr == expectedC)
	{
		fiError("malloc failed\n");
		return -1;
	}

	memcpy(expectedC, args->c, sizeof(double) * elemCntC);
#endif // TEST_EN

	// Copy result back to C
	if(toColMajorWithStride((double *) args->c, matC.get(), 0, args->ldc, args->m, args->n))
	{
		fiError("toColMajorWithStride failed\n");
		return -5;
	}

#if TEST_EN
	for(size_t index = 0; index < elemCntC; index++)
	{
		const double actualC = ((double *) args->c)[index];
		if(0.001 < fabs(actualC - expectedC[index])) // TODO: Arbitrary threshold!!
		{
			fiError("Actual output index %lu doesn't match expected: %f vs %f\n", index, actualC, expectedC[index]);
			return -6;
		}
	}

	free(expectedC);
#endif // TEST_EN

	return 0;
}
#endif // HW_SIMULATION

// maxRelError in percent
// Returns relative error
template<typename T>
float gemmRelativeError(T * io, long fiIndex, long M, long N, long ld, float maxRelError)
{
	// Calculate average of absolute values
	double avg = 0;
	for(long col = 0; col < N; col++)
	{
		for(long row = 0; row < M; row++)
		{
			avg += fabs(io[row + col * ld]);
		}
	}

	avg /= M * N;

	// Create random number 0 ... maxRelError / 100
	// coverity[DC.WEAK_CRYPTO]
	const double randomFactor = (((double) rand()) * maxRelError / 100.) / RAND_MAX;
	// coverity[DC.WEAK_CRYPTO]
	io[fiIndex] += avg * randomFactor * (rand() % 2 ? -1. : 1.);

	return randomFactor;
}

// Dictates whether a given GEMM call is suitable for FI or not. Returns:
// <= 0 if the GEMM call is unsuitable for FI
//  > 0 if the GEMM call is suitable for FI
//  The optional opCntExt argument stores the new GEMM counter after this op
int selectedForFi(int transa, int transb, blas_arg_t * args, size_t elemSize, size_t * opCntExt)
{
	if(0 == args->m * args->k * args->n)
	{
		// not a real gemm
		return 0;
	}

	// Check if initialized
	if(NULL == TfFi)
	{
		fiDebug("Nullpointer: FI Off!\n");
		return 0;
	}

	// Check if Op is supported
	if((0 > transa) ||(1 < transa) || (0 > transb) || (1 < transb))
	{
		fiWarning("Unsupported Trans\n");
		return 0;
	}

	// Cnt Ops and check if we want to fi

	const size_t opsCntOld = TfFi->OpsCnt;

#if HW_SIMULATION
	SystolicArraySim * saSim = (SystolicArraySim*) TfFi->MmaFi;

	// below operation ordered on purpose s.t. division result is zero if divisor is larger
#if OUT_POSITION_QUICKFIX_EN
	// Can't dispatch two Kmma blocks without different M, N positions in between
	const size_t outPositions = (args->m / (mMmaPositions_quickfix * saSim->Mmma())) * (args->n / (nMmaPositions_quickfix * saSim->Nmma()));
	if((0 == outPositions) && (args->k >= 2 * saSim->Kmma()))
	{
		// TODO: In principle, e.g. 1 x 4 MMA outposition pattern would also work (just testing for 2 x 2 above)
		const size_t outPositionsReal = (args->m / saSim->Mmma()) * (args->n / saSim->Nmma());
		const size_t outPositionsRealTrans = (args->n / saSim->Mmma()) * (args->m / saSim->Nmma());
		if((outPositionsReal >= saSim->RequiredOutPositionsBetweenK()) ||
				(outPositionsRealTrans >= saSim->RequiredOutPositionsBetweenK()))
		{
			fiWarning("outPositions working in principle but skipped because not implemented\n");
		}

		fiWarning("Skipping %lu x %lu x %lu GEMM: Not enough output positions for Systolic Array\n", args->m, args->k, args->n);
		
		return 0;
	}
#else // !OUT_POSITION_QUICKFIX_EN
#error Implement correct out position handling
#endif // !OUT_POSITION_QUICKFIX_EN

	const size_t opCnt = (args->m / saSim->Mmma()) * (args->k / saSim->Kmma()) * (args->n / saSim->Nmma());
	const size_t opCntTrans = (args->n / saSim->Mmma()) * (args->k / saSim->Kmma()) * (args->m / saSim->Nmma());
	if(opCnt < opCntTrans) // TODO: Implement transpose gemm
	{
		fiWarning("opCnt is smaller than transpose opCnt (transpose not implemented)\n");
	}

	if(0 == opCnt)
	{
		fiDebug("Skipping %lu x %lu x %lu GEMM: Too small for Systolic Array\n", args->m, args->k, args->n);
		return 0;
	}
	else if(sizeof(double) != elemSize)
	{
		fiError("HW-Simulation: Only double implemented, got element size %lu\n", elemSize);
		
		return -2;
	}
#else // !HW_SIMULATION
	const size_t opCnt = 2 * args->m * args->k * args->n; // ops count used by open blas
#endif // !HW_SIMULATION

	// Increment the ops counter given as input
	if( opCntExt ) {
		*opCntExt = opCnt + opsCntOld;
	}

	// Check if enabled - done after updating total ops count
	if (TfFi->Mode == TFFIMODE_NONE) {

		return 0;
	}
	
	// Do we FI this op?
	switch(TfFi->Mode)
	{
	case TFFIMODE_TRANSIENT:
		if((opsCntOld > TfFi->OpFi) || (TfFi->OpFi >= (opCnt + opsCntOld)))
		{
			return 0;
		}
		break;

	case TFFIMODE_PERMANENT:
		// always FI
		break;

	case TFFIMODE_NONE: // no break intended
	default:
		fiError("Unknown TfFi->Mode: %i\n", TfFi->Mode);
		return -3;
	}

	return 1;
}

// See gemm.c for arg usage
// ldx specifies the column stride (fortran is column major), i.e. a(i,j) = a[i + j * lda]
int gemmFi(int transa, int transb, blas_arg_t * args, const void * cOriginal, size_t elemSize)
{
        fiDebug("M x K x N = %lu x %lu x %lu\n", args->m, args->k, args->n);

	int selected = selectedForFi(transa, transb, args, elemSize, &(TfFi->OpsCnt));
	if ( selected <= 0 )
	{
		return selected;
	}


	// Let's FI this!
#if HW_SIMULATION

	if(hwFi(TfFi, transa, transb, args, cOriginal, elemSize))
	{
		fiError("hwFi failed\n");
		return -3;
	}

#else // !HW_SIMULATION

	// coverity[DC.WEAK_CRYPTO]
	const int fiM = rand() % args->m;
	// coverity[DC.WEAK_CRYPTO]
	const int fiN = rand() % args->n;
	const int fiIndex = fiM + fiN * args->ldc;
	if(sizeof(float) == elemSize)
	{
#if DEBUG_EN
		const float oldValue = ((float *) args->c)[fiIndex];
#endif // DEBUG_EN

		// coverity[DC.WEAK_CRYPTO]
		const int fiBit = rand() % (sizeof(float) * 8);
		uint32_t * u32 = (uint32_t *) args->c;
		u32[fiIndex] ^= (1 << fiBit);

		TfFi->OpFiBitPos = fiBit;

#if DEBUG_EN
		const float fiValue = ((float *) args->c)[fiIndex];
#endif // DEBUG_EN

		fiDebug("Performed FI %f -> %f (Index %i, Bit %lu, rel Error %f)\n", oldValue, fiValue, fiIndex, TfFi->OpFiBitPos, blasFi->OpFiRelError);
	}
	else if(sizeof(double) == elemSize)
	{
#if DEBUG_EN
		const double oldValue = ((double *) args->c)[fiIndex];
#endif // DEBUG_EN

		// coverity[DC.WEAK_CRYPTO]
		const int fiBit = rand() % (sizeof(double) * 8);
		uint64_t * u64 = (uint64_t *) args->c;
		u64[fiIndex] ^= (1LU << fiBit);

		TfFi->OpFiBitPos = fiBit;

#if DEBUG_EN
		const double fiValue = ((double *) args->c)[fiIndex];
#endif // DEBUG_EN

		fiDebug("Performed FI %f -> %f (Index %i, Bit %lu, rel Error %f)\n", oldValue, fiValue, fiIndex, TfFi->OpFiBitPos, blasFi->OpFiRelError);
	}
	else
	{
		fiError("Fi of elem size %lu not implemented\n", elemSize);
		return -1;
	}
#endif // !HW_SIMULATION


	return 0;
}
#endif 