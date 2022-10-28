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

#include "helpers.h"

size_t sasWarningCnt = 0;
size_t sasErrorCnt = 0;

uint64_t randomBits()
{
	uint64_t out = 0;
	uint8_t* outU8 = (uint8_t*) &out;
	for(size_t byte = 0; byte < sizeof(out); byte++)
	{
		// coverity[DC.WEAK_CRYPTO]
		outU8[byte] = rand();
	}

	return out;
}

// Will generate random Double with exponent uniformly within given thresholds
// And fractionZero none = 0 ... 1 = all
double randomDouble(int expMin, int expMax, float fractionZero)
{
	// coverity[DC.WEAK_CRYPTO]
	const double isZero = (double) rand();
	if(isZero / RAND_MAX < fractionZero)
	{
		return 0;
	}

	doubleUnion outU64;

	// sign
	// coverity[DC.WEAK_CRYPTO]
	outU64.u64 = rand() % 2 ? 1 : 0;

	// Exponent
	outU64.u64 <<= 11;
	// Generate random number in expMax - expMin
	const size_t expDiff = abs(expMax - expMin);
	const uint16_t clogExpDiff = std::ceil(std::log2(expDiff)) + 1;

	uint16_t expOffset;
	while(1) // TODO: very dirty
	{
		// coverity[DC.WEAK_CRYPTO]
		expOffset = rand() & ((1UL << clogExpDiff) - 1);
		if(expOffset <= expDiff)
		{
			break;
		}
	}

	// Range is expMin + 1023 ... expMax + 1023
	uint64_t exponent = expMin + 1023 + expOffset;
	outU64.u64 |= exponent;

	// Mantissa
	outU64.u64 <<= 52;
	outU64.u64 |= (randomBits() & ((1UL << 52) - 1)); // Mask bits higher than 52

	return outU64.flt;
}

void printBinary(const uint8_t * pData, size_t nBits, size_t lineBreakAfter)
{
	int lineBreakCnt = 0;
	for(int bit = nBits - 1; bit >= 0; bit--)
	{
		if(lineBreakCnt && (0 == (lineBreakCnt % lineBreakAfter)))
		{
			sasInfo("\n");
		}
		lineBreakCnt++;

		const size_t byte = bit / 8;
		const size_t bitoff = bit % 8;

		sasInfo("%d", pData[byte] & (1 << bitoff) ? 1 : 0);
	}
}

void matrixPrint(const double * data, size_t rows, size_t cols, size_t stride)
{
	for(size_t row = 0; row < rows; row++)
	{
		for(size_t col = 0; col < cols; col++)
		{
			sasDebug("%f, ", data[row * stride + col]);
		}
		sasDebug("\n");
	}
}


int bitsCopy(uint8_t * pData, size_t nData, size_t startBit, uint8_t* bits, uint8_t nBits)
{
	const size_t endBit = startBit + nBits;
	const size_t nDataRequ = endBit / 8 + (endBit % 8 ? 1 : 0);
	if(nDataRequ > nData)
	{
		sasError("Destination buffer too small (%lu > %lu)\n", nDataRequ, nData);
		return -1;
	}

	for(uint8_t bit = 0; bit < nBits; bit++)
	{
		const size_t destBit = startBit + bit;
		const size_t destByte = destBit / 8;
		const size_t destByteBitOffset = destBit % 8;

		if(bits[bit / 8] & (1ULL << (bit % 8)))
		{
			pData[destByte] |= 1 << destByteBitOffset;
		}
		else
		{
			pData[destByte] &= ~(1 << destByteBitOffset);
		}
	}

	return 0;
}

int elemSet(sNFp16_t * pData, double value)
{
	// TODO: Handle nan
	//  20'b{8'b: -127 biased exp, 12'sb: signed mantissa with leading 1}
	const floatUnion uval = {(float) value};

	uint32_t tmp = (uval.u32 >> 23) & BIT_MASK(8);
	tmp <<= 12;

	int32_t signedMantissa = (uval.u32 >> 13) & BIT_MASK(10);
	if(isnormal(value))
	{
		signedMantissa |= 1 << 10;
	}

	if(uval.u32 & (1 << 31))
	{
		signedMantissa = -signedMantissa;
	}

	tmp |= signedMantissa & BIT_MASK(12);

	if(bitsCopy((uint8_t*) pData, sizeof(*pData), 0, (uint8_t*) &tmp, 20))
	{
		sasError("bitsCopy failed\n");
		return -1;
	}

	return 0;
}

int elemSet(sNFp32_t * pData, double value)
{
	// TODO: Handle nan
	//  33'b{8'b: -127 biased exp, 25'sb: signed mantissa with leading 1}
	const floatUnion uval = {(float) value};
	uint64_t tmp = (uval.u32 >> 23) & BIT_MASK(8);
	tmp <<= 25;

	int32_t signedMantissa = uval.u32 & BIT_MASK(23);
	if(isnormal(value))
	{
		signedMantissa |= 1 << 23;
	}

	if(uval.u32 & (1 << 31))
	{
		signedMantissa = -signedMantissa;
	}

	tmp |= signedMantissa & BIT_MASK(25);

	if(bitsCopy((uint8_t*) pData, sizeof(*pData), 0, (uint8_t*) &tmp, 33))
	{
		sasError("bitsCopy failed\n");
		return -1;
	}

	return 0;
}

int elemSet(sNFp64_t * pData, double value)
{
	// TODO: Handle nan
	//  65'b{11'b: -1023 biased exp, 54'sb: signed mantissa with leading 1}
	const doubleUnion uval = {value};
	const uint16_t tmpExp = (uval.u64 >> 52) & BIT_MASK(11);

	int64_t signedMantissa = uval.u64 & BIT_MASK(52);
	if(isnormal(value))
	{
		signedMantissa |= 1ULL << 52;
	}

	if(uval.u64 & (1ULL << 63))
	{
		signedMantissa = -signedMantissa;
	}

	signedMantissa &= BIT_MASK(54);

	if(bitsCopy((uint8_t*) pData->data(), sizeof(*pData), 0, (uint8_t*) &signedMantissa, 54))
	{
		sasError("bitsCopy failed\n");
		return -1;
	}

	if(bitsCopy((uint8_t*) pData->data(), sizeof(*pData), 54, (uint8_t*) &tmpExp, 11))
	{
		sasError("bitsCopy failed\n");
		return -1;
	}

	return 0;
}

int elemSet(uint8_t * pData, size_t nData, size_t nBitsElem, size_t pos, double value)
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
	if(isnormal(value))
	{
		signedMantissa |= 1ULL << 52;
	}

	if(uval.u64 & (1ULL << 63))
	{
		signedMantissa = -signedMantissa;
	}

	signedMantissa &= BIT_MASK(54);

	if(bitsCopy(pData, nData, pos * nBitsElem, (uint8_t*) &signedMantissa, 54))
	{
		sasError("bitsCopy failed\n");
		return -1;
	}

	if(bitsCopy(pData, nData, pos * nBitsElem + 54, (uint8_t*) &tmpExp, 11))
	{
		sasError("bitsCopy failed\n");
		return -1;
	}

	return 0;
}

int elemGet(double * value, const uint8_t * pData, size_t nData, size_t nBitsElem, size_t pos)
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

	sNFp64_t tmp;
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

		if(pData[dataByte] & (1 << dataBit))
		{
			tmpU8[tmpByte] |= 1 << tmpBit;
		}
	}

	*value = toDouble(tmp);

	return 0;
}

double toDouble(const sNFp64_t &data)
{
	int64_t signedMant = data[1];
	signedMant <<= (sizeof(data[0]) * 8);
	signedMant |= data[0];

	signedMant &= BIT_MASK(54);

	const bool isNeg = signedMant & (1ULL << 53);
	if(isNeg)
	{
		signedMant |= 0xFFC0000000000000; // 54 2's comp -> 64 2's comp: Set bits 63..54 to one
		signedMant = -signedMant; // convert to positive number
	}

	uint64_t exp = data[2];
	exp <<= 10;
	exp |= (data[1] >> 22);
	exp &= BIT_MASK(11);

	doubleUnion uflt;
	uflt.u64 = isNeg ? 1 : 0;

	uflt.u64 <<= 11;
	uflt.u64 |= exp;

	uflt.u64 <<= 52;
	uflt.u64 |= signedMant & BIT_MASK(52); // TODO: implement non-normal numbers

	return uflt.flt;
}

double toDouble(const sNFp32_t &data)
{
	int32_t signedMant = data & BIT_MASK(25);
	const bool isNeg = signedMant & (1 << 24);
	if(isNeg)
	{
		signedMant |= 0xFE000000; // 25 2's comp -> 32 2's comp: Set bits 31..25 to one
		signedMant = -signedMant; // convert to positive number
	}

	floatUnion uflt;
	uflt.u32 = isNeg ? 1 : 0;

	uflt.u32 <<= 8;
	uflt.u32 |= (data >> 25) & BIT_MASK(8);

	uflt.u32 <<= 23;
	uflt.u32 |= signedMant & BIT_MASK(23); // TODO: implement non-normal numbers

	return uflt.flt;
}

double toDouble(const sNFp16_t &data)
{
	int32_t signedMant = data & BIT_MASK(12);
	const bool isNeg = signedMant & (1 << 11);
	if(isNeg)
	{
		signedMant |= 0xFFFFF000; // 12 2's comp -> 32 2's comp: Set bits 31..12 to one
		signedMant = -signedMant; // convert to positive number
	}

	floatUnion uflt;
	uflt.u32 = isNeg ? 1 : 0;

	uflt.u32 <<= 8;
	uflt.u32 |= (data >> 12) & BIT_MASK(8);

	uflt.u32 <<= 23;
	uflt.u32 |= signedMant & BIT_MASK(23); // TODO: implement non-normal numbers

	return uflt.flt;
}

void print(const sNFp32_t &data)
{
	sasInfo("%f", toDouble(data));
}

void print(const sNFp16_t &data)
{
	sasInfo("%f", toDouble(data));
}

void print(const sNFp64_t &data)
{
	sasInfo("%f", toDouble(data));
}


