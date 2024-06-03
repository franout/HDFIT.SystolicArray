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

#ifndef INTERFACE_FAULTINJECTOR_H_
#define INTERFACE_FAULTINJECTOR_H_

#ifdef __cplusplus
extern "C" {
#endif

#define TFFIOPSCNT_ENV_VAR "TFFI_OPSCNT"

#define TFFIMODE_ENV_VAR "TFFI_MODE"
#define TFFIMODE_NONE_CONST "NONE"
#define TFFIMODE_TRANSIENT_CONST "TRANSIENT"
#define TFFIMODE_PERMANENT_CONST "PERMANENT"

#define TFFICORRUPTION_ENV_VAR "TFFI_CORRUPTION"
#define TFFICORRUPTION_NONE_CONST "NONE"
#define TFFICORRUPTION_STUCKHIGH_CONST "STUCKHIGH"
#define TFFICORRUPTION_STUCKLOW_CONST "STUCKLOW"
#define TFFICORRUPTION_FLIP_CONST "FLIP"

#define TFFIBITS_ENV_VAR "TFFI_BITS"
#define TFFIBITS_NONE_CONST "NONE"
#define TFFIBITS_EVERYWHERE_CONST "EVERYWHERE"
#define TFFIBITS_MANTISSA_CONST "MANTISSA"

#define TFFIOUTPUT_ENV_VAR "BLASFI_OUTPUT"
#define TFFIOUTPUT_STDOUT_CONST "STDOUT"
#define TFFIOUTPUT_STDERR_CONST "STDERR"

extern int TfFiInit(int rank);
extern int TfFiSet();
extern int TfFiClose();
extern void TfFiPrint();

#ifdef __cplusplus
}
#endif


#endif /* INTERFACE_FAULTINJECTOR_H_ */
