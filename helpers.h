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

#ifndef HELPERS_H_
#define HELPERS_H_

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include <vector>

#include "verilated.h"
#include "verilated_types.h"

extern size_t sasWarningCnt;
extern size_t sasErrorCnt;

#define SAS_DEBUG 0
#define DEBUG_VERBOSE 0
#define SAS_FI_PRINT 1

#if SAS_DEBUG
#define sasDebug(...) \
		do { \
			printf(__VA_ARGS__); \
			fflush(stdout); \
		} while(0)
#else // !SAS_DEBUG
#define sasDebug(...)
#endif // !SAS_DEBUG

#if SAS_FI_PRINT
#define sasFaultPrint(...) \
		do { \
			printf(__VA_ARGS__); \
			fflush(stdout); \
		} while(0)
#else // !SAS_FI_PRINT
#define sasFaultPrint(...)
#endif // !SAS_FI_PRINT

#define sasInfo(...)  do{printf(__VA_ARGS__); fflush(stdout);}while(0)
#define sasWarning(...) \
		do { \
			fprintf(stderr, "Warning (%s:%i): ", __FILE__, __LINE__); \
			if(errno) \
			{ \
				fprintf(stderr, "%s: ", strerror(errno)); \
			} \
			fprintf(stderr, __VA_ARGS__); \
			fflush(stderr); \
			sasWarningCnt++; \
		} while(0)

#define sasError(...) \
		do { \
			fprintf(stderr, "Error (%s:%i): ", __FILE__, __LINE__); \
			if(errno) \
			{ \
				fprintf(stderr, "%s: ", strerror(errno)); \
			} \
			fprintf(stderr, __VA_ARGS__); \
			fflush(stderr); \
			sasErrorCnt++; \
		} while(0)

#define sasFatal(...) \
	do{ \
		fprintf(stderr, "Fatal(%s:%i):", __FILE__, __LINE__); \
		fprintf(stderr, __VA_ARGS__); \
		fflush(stderr); \
		exit(EXIT_FAILURE); \
	} while(0)


#define BIT_MASK(NBITS) ((1ULL << NBITS) - 1)

typedef union {
	float flt;
	uint32_t u32;
} floatUnion;

typedef union {
	double flt;
	uint64_t u64;
} doubleUnion;

typedef IData sNFp16_t; // signed normal hf + bf (max(exp bits), max(mant bits))
typedef QData sNFp32_t; // signed normal fp32
typedef VlWide<3> sNFp64_t; // signed normal fp64

extern int elemSet(sNFp16_t * pData, double value);
extern int elemSet(sNFp32_t * pData, double value);
extern int elemSet(sNFp64_t * pData, double value);

extern int elemSet(uint8_t * pData, size_t nData, size_t nBitsElem, size_t pos, double value);
extern int elemGet(double * value, const uint8_t * pData, size_t nData, size_t nBitsElem, size_t pos);

extern void print(const sNFp16_t &data);
extern void print(const sNFp32_t &data);
extern void print(const sNFp64_t &data);

extern double toDouble(const sNFp16_t &data);
extern double toDouble(const sNFp32_t &data);
extern double toDouble(const sNFp64_t &data);

extern int bitsCopy(uint8_t * pData, size_t nData, size_t startBit, uint8_t* bits, uint8_t nBits);

extern void printBinary(const uint8_t * pData, size_t nBits, size_t lineBreakAfter = SIZE_MAX);
extern void matrixPrint(const double * data, size_t rows, size_t cols, size_t stride);

extern uint64_t randomBits();
extern double randomDouble(int expMin, int expMax, float fractionZero);

#endif /* HELPERS_H_ */
