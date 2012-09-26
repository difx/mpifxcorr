/***************************************************************************
 *   Copyright (C) 2005-2012 by Adam Deller & Richard Dodson               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
//===========================================================================
// SVN properties (DO NOT CHANGE)
//
// $Id$
// $HeadURL$
// $LastChangedRevision$
// $Author$
// $LastChangedDate$
//
//============================================================================

/** \file architecture.h
 *  \brief File contains mapping for vector functions to specific architectures
 */

#include "config.h"
 
#ifndef ARCHITECTURE_H
#define ARCHITECTURE_H

#define INTEL   1
#define AMD     2
#define GENERIC 3

//define the MPI tags
#define CR_TERMINATE      0
#define CR_VALIDVIS       1
#define CR_RECEIVETIME    2
#define CR_PROCESSDATA    3
#define CR_PROCESSCONTROL 4
#define DS_TERMINATE      5
#define DS_PROCESS        6

//define the architecture to be compiled for here
#if HAVE_IPP
#define ARCH INTEL
#endif

//if no architecture is selected, default to generic 
#ifndef ARCH
#define ARCH GENERIC
#endif



//set up the function mapping for the intel architecture
#if(ARCH == INTEL)
#include <ipps.h>
#include <ippvm.h>
#include <ippcore.h>

//start with the data types
#define u8                       Ipp8u
#define u16                      Ipp16u
#define s16                      Ipp16s
#define cs16                     Ipp16sc
#define s32                      Ipp32s
#define u32                      Ipp32u
#define f32                      Ipp32f
#define cf32                     Ipp32fc
#define cf64                     Ipp64fc
#define f64                      Ipp64f
#define s64                      Ipp64s
#define u64                      Ipp64u

//and the constant values
#define vecFFTSpecR_f32          IppsFFTSpec_R_32f
#define vecFFTSpecC_f32          IppsFFTSpec_C_32f
#define vecFFTSpecC_cf32         IppsFFTSpec_C_32fc
#define vec2DFFTSpecC_cf32       IppiFFTSpec_C_32fc
#define vecDFTSpecC_cf32         IppsDFTSpec_C_32fc
#define vecDFTSpecR_f32          IppsDFTSpec_R_32f
#define vecFFTSpec_s16           IppsFFTSpec_R_16s
#define vecHintAlg               IppHintAlgorithm
#define vecRndZero               ippRndZero
#define vecRndNear               ippRndNear
#define vecHamming               ippWinHamming
#define vecTrue                  ippTrue
#define MAX_S32                  IPP_MAX_32S
#define MAX_U32                  IPP_MAX_32U
#define MAX_S16                  IPP_MAX_16S
#define MAX_U16                  IPP_MAX_16U
#define TWO_PI                   IPP_2PI
#define vecFFT_NoReNorm          IPP_FFT_NODIV_BY_ANY
#define vecFFT_ReNorm            IPP_FFT_DIV_INV_BY_N
#define vecAlgHintFast           ippAlgHintFast
#define vecAlgHintAccurate       ippAlgHintAccurate
#define vecNoErr                 ippStsNoErr
#define vecStatus                IppStatus

//now the vector functions themselves
#define vectorAlloc_u8(length)   ippsMalloc_8u(length)
#define vectorAlloc_s16(length)  ippsMalloc_16s(length)
#define vectorAlloc_cs16(length) ippsMalloc_16sc(length)
#define vectorAlloc_s32(length)  ippsMalloc_32s(length)
#define vectorAlloc_f32(length)  ippsMalloc_32f(length)
#define vectorAlloc_cf32(length) ippsMalloc_32fc(length)
#define vectorAlloc_f64(length)  ippsMalloc_64f(length)
#define vectorAlloc_cf64(length) ippsMalloc_64fc(length)

#define vectorFree(memptr)       ippsFree(memptr)


#define vectorAdd_f32_I(src, srcdest, length)                               ippsAdd_32f_I(src, srcdest, length)
#define vectorAdd_f64_I(src, srcdest, length)                               ippsAdd_64f_I(src, srcdest, length)
#define vectorAdd_s16_I(src, srcdest, length)                               ippsAdd_16s_I(src, srcdest, length)
#define vectorAdd_s32_I(src, srcdest, length)                               ippsAdd_32s_ISfs(src, srcdest, length, 0)
#define vectorAdd_cf32_I(src, srcdest, length)                              ippsAdd_32fc_I(src, srcdest, length)
#define vectorAdd_cf64_I(src, srcdest, length)                              ippsAdd_64fc_I(src, srcdest, length)
#define vectorAddC_f64(src, val, dest, length)                              ippsAddC_64f(src, val, dest, length)
#define vectorAddC_f32(src, val, dest, length)                              ippsAddC_32f(src, val, dest, length)
#define vectorAddC_f32_I(val, srcdest, length)                              ippsAddC_32f_I(val, srcdest, length)
#define vectorAddC_s16_I(val, srcdest, length)                              ippsAddC_16s_I(val, srcdest, length)
#define vectorAddC_f64_I(val, srcdest, length)                              ippsAddC_64f_I(val, srcdest, length)

#define vectorAddProduct_cf32(src1, src2, accumulator, length)              ippsAddProduct_32fc(src1, src2, accumulator, length)

#define vectorConj_cf32(src, dest, length)                                  ippsConj_32fc(src, dest, length)
#define vectorConjFlip_cf32(src, dest, length)                              ippsConjFlip_32fc(src, dest, length)

#define vectorCopy_u8(src, dest, length)                                    ippsCopy_8u(src, dest, length)
#define vectorCopy_s16(src, dest, length)                                   ippsCopy_16s(src, dest, length)
#define vectorCopy_s32(src, dest, length)                                   ippsCopy_32f((f32*)src, (f32*)dest, length)
#define vectorCopy_f32(src, dest, length)                                   ippsCopy_32f(src, dest, length)
#define vectorCopy_cf32(src, dest, length)                                  ippsCopy_32fc(src, dest, length)
#define vectorCopy_f64(src, dest, length)                                   ippsCopy_64f(src, dest, length)

#define vectorCos_f32(src, dest, length)                                    ippsCos_32f_A11(src, dest, length)

#define vectorConvertScaled_s16f32(src, dest, length, scalefactor)          ippsConvert_16s32f_Sfs(src, dest, length, scalefactor)
#define vectorConvertScaled_f32s16(src, dest, length, rndmode, scalefactor) ippsConvert_32f16s_Sfs(src, dest, length, rndmode, scalefactor)
#define vectorConvertScaled_f32u8(src, dest, length, rndmode, scalefactor)  ippsConvert_32f8u_Sfs(src, dest, length, rndmode, scalefactor)
#define vectorConvert_f32s32(src, dest, length, rndmode)                    ippsConvert_32f32s_Sfs(src, dest, length, rndmode, 0)
#define vectorConvert_s16f32(src, dest, length)                             ippsConvert_16s32f(src, dest, length)
#define vectorConvert_s32f32(src, dest, length)                             ippsConvert_32s32f(src, dest, length)
#define vectorConvert_f64f32(src, dest, length)                             ippsConvert_64f32f(src, dest, length)

#define vectorDivide_f32(src1, src2, dest, length)                          ippsDiv_32f(src1, src2, dest, length)
#define vectorDivC_cf32_I(val, srcdest, length)                             ippsDivC_32fc_I(val, srcdest, length)
#define vectorDivC_f32_I(val, srcdest, length)                              ippsDivC_32f_I(val, srcdest, length)

#define vectorDotProduct_f64(src1, src2, length, output)                    ippsDotProd_64f(src1, src2, length, output)

#define vectorFlip_f64_I(srcdest, length)                                   ippsFlip_64f_I(srcdest, length)

#define vectorMagnitude_cf32(src, dest, length)                             ippsMagnitude_32fc(src, dest, length)

#define vectorMean_cf32(src, length, mean, hint)                            ippsMean_32fc(src, length, mean, hint)

#define vectorMul_f32(src1, src2, dest, length)                             ippsMul_32f(src1, src2, dest, length)
#define vectorMul_f32_I(src, srcdest, length)                               ippsMul_32f_I(src, srcdest, length)
#define vectorMul_cf32_I(src, srcdest, length)                              ippsMul_32fc_I(src, srcdest, length)
#define vectorMul_cf32(src1, src2, dest, length)                            ippsMul_32fc(src1, src2, dest, length)
#define vectorMul_f32cf32(src1, src2, dest, length)                         ippsMul_32f32fc(src1, src2, dest, length)
#define vectorMulC_f32(src, val, dest, length)                              ippsMulC_32f(src, val, dest, length)
#define vectorMulC_cs16_I(val, srcdest, length)                             ippsMulC_16sc_ISfs(val, srcdest, length, 0)
#define vectorMulC_f32_I(val, srcdest, length)                              ippsMulC_32f_I(val, srcdest, length)
#define vectorMulC_cf32_I(val, srcdest, length)                             ippsMulC_32fc_I(val, srcdest, length)
#define vectorMulC_cf32(src, val, dest, length)                             ippsMulC_32fc(src, val, dest, length)
#define vectorMulC_f64_I(val, srcdest, length)                              ippsMulC_64f_I(val, srcdest, length)
#define vectorMulC_f64(src, val, dest, length)                              ippsMulC_64f(src, val, dest, length)

#define vectorPhase_cf32(src, dest, length)                                 ippsPhase_32fc(src, dest, length)

#define vectorRealToComplex_f32(real, imag, complex, length)                ippsRealToCplx_32f(real, imag, complex, length)

#define vectorReal_cf32(complex, real, length)                              ippsReal_32fc(complex, real, length)

#define vectorSet_f32(val, dest, length)                                    ippsSet_32f(val, dest, length)

#define vectorSin_f32(src, dest, length)                                    ippsSin_32f_A11(src, dest, length)

#define vectorSinCos_f32(src, sin, cos, length)                             ippsSinCos_32f_A11(src, sin, cos, length)

#define vectorSplitScaled_s16f32(src, dest, numchannels, chanlen)           ippsSplitScaled_16s32f_D2L(src, dest, numchannels, chanlen)

#define vectorSquare_f32_I(srcdest, length)                                 ippsSqr_32f_I(srcdest, length)
#define vectorSquare_f64_I(srcdest, length)                                 ippsSqr_64f_I(srcdest, length)

#define vectorSub_f32_I(src, srcdest, length)                               ippsSub_32f_I(src, srcdest, length)
#define vectorSub_s32(src1, src2, dest, length)                             ippsSub_32s_Sfs(src1, src2, dest, length, 0)
#define vectorSub_cf32_I(src, srcdest, length)                              ippsSub_32fc_I(src, srcdest, length)

#define vectorSum_cf32(src, length, sum, hint)                              ippsSum_32fc(src, length, sum, hint)

#define vectorZero_cf32(dest, length)                                       ippsZero_32fc(dest, length)
#define vectorZero_cf64(dest, length)                                       ippsZero_64fc(dest, length)
#define vectorZero_f32(dest, length)                                        ippsZero_32f(dest, length)
#define vectorZero_s16(dest, length)                                        ippsZero_16s(dest, length)
#define vectorZero_s32(dest, length)                                        genericZero_32s(dest, length)

#define vectorMax_f32(src, dest, length)                                    ippsMax_32f(src, dest, length)
#define vectorMin_f32(src, dest, length)                                    ippsMin_32f(src, dest, length)
#define vectorMove_cf32(src, dest, length)                                  ippsMove_32fc(src, dest, length)
#define vectorMove_f32(src, dest, length)                                   ippsMove_32f(src, dest, length)
#define vectorStdDev_f32(src, length, stddev, hint)                         ippsStdDev_32f(src,length, stddev, hint)
#define vectorAbs_f32_I(srcdest, length)                                    ippsAbs_32f_I(srcdest,length)
#define vectorMaxIndx_f32(srcdest, length, max, imax)                       ippsMaxIndx_32f(srcdest,length, max, imax)
#define vectorFFTInv_CCSToR_32f(src, dest,fftspec,buff)                     ippsFFTInv_CCSToR_32f(src, dest,fftspec,buff)

// Get Error string
#define vectorGetStatusString(code)                                         ippGetStatusString(code)

// FFT Functions

#define vectorInitFFTR_f32(fftspec, order, flag, hint)                      ippsFFTInitAlloc_R_32f(fftspec, order, flag, hint)
#define vectorInitFFTC_f32(fftspec, order, flag, hint)                      ippsFFTInitAlloc_C_32f(fftspec, order, flag, hint)
#define vectorInitFFTC_cf32(fftspec, order, flag, hint)                     ippsFFTInitAlloc_C_32fc(fftspec, order, flag, hint)
//#define vectorInitFFT_f32(fftspec, order, flag, hint)                       ippsDFTInitAlloc_R_32f(fftspec, order, flag, hint)
#define vectorInitDFTC_cf32(fftspec, order, flag, hint)                     ippsDFTInitAlloc_C_32fc(fftspec, order, flag, hint)
#define vectorInitDFTR_f32(fftspec, order, flag, hint)                     ippsDFTInitAlloc_R_32f(fftspec, order, flag, hint)
#define vectorInitFFT_s16(fftspec, order, flag, hint)                       ippsFFTInitAlloc_R_16s(fftspec, order, flag, hint)
#define vectorGetFFTBufSizeR_f32(fftspec, buffersize)                       ippsFFTGetBufSize_R_32f(fftspec, buffersize)
#define vectorGetFFTBufSizeC_f32(fftspec, buffersize)                       ippsFFTGetBufSize_C_32f(fftspec, buffersize)
#define vectorGetFFTBufSizeC_cf32(fftspec, buffersize)                      ippsFFTGetBufSize_C_32fc(fftspec, buffersize)
//#define vectorGetFFTBufSize_f32(fftspec, buffersize)                        ippsDFTGetBufSize_R_32f(fftspec, buffersize)
#define vectorGetDFTBufSizeC_cf32(fftspec, buffersize)                      ippsDFTGetBufSize_C_32fc(fftspec, buffersize)
#define vectorGetDFTBufSizeR_f32(fftspec, buffersize)                      ippsDFTGetBufSize_R_32f(fftspec, buffersize)
#define vectorGetFFTBufSize_s16(fftspec, buffersize)                        ippsFFTGetBufSize_R_16s(fftspec, buffersize)
#define vectorFreeFFTR_f32(fftspec)                                         ippsFFTFree_R_32f(fftspec)
#define vectorFreeFFTC_f32(fftspec)                                         ippsFFTFree_C_32f(fftspec)
#define vectorFreeFFTC_cf32(fftspec)                                        ippsFFTFree_C_32fc(fftspec)
#define vectorFreeDFTC_cf32(fftspec)                                        ippsDFTFree_C_32fc(fftspec)
#define vectorFreeDFTR_f32(fftspec)                                        ippsDFTFree_R_32f(fftspec)
#define vectorFreeDFTC_f32(fftspec)                                        ippsDFTFree_R_32f(fftspec)
#define vectorFFT_RtoC_f32(src, dest, fftspec, fftbuffer)                   ippsFFTFwd_RToCCS_32f(src, dest, fftspec, fftbuffer)
#define vectorFFT_CtoC_f32(srcre, srcim, destre, destim, fftspec, fftbuff)  ippsFFTFwd_CToC_32f(srcre, srcim, destre, destim, fftspec, fftbuff)
#define vectorFFT_CtoC_cf32(src, dest, fftspec, fftbuff)                    ippsFFTFwd_CToC_32fc(src, dest, fftspec, fftbuff)
#define vectorDFT_CtoC_cf32(src, dest, fftspec, fftbuff)                    ippsDFTFwd_CToC_32fc(src, dest, fftspec, fftbuff)
#define vectorDFT_RtoC_f32(src, dest, fftspec, fftbuff)                    ippsDFTFwd_RToCCS_32f(src, dest, fftspec, fftbuff)
//#define vectorFFT_RtoC_f32(src, dest, fftspec, fftbuffer)                   ippsDFTFwd_RToCCS_32f(src, dest, fftspec, fftbuffer)

#define vectorScaledFFT_RtoC_s16(src, dest, fftspec, fftbuffer, scale)      ippsFFTFwd_RToCCS_16s_Sfs(src, dest, fftspec, fftbuffer, scale)

#define vectorGenerateFIRLowpass_f64(freq, taps, length, window, normalise) ippsFIRGenLowpass_64f(freq, taps, length, window, normalise)

// For difx_monitor // Note 2D
#define vector2DFFT_CtoC_cf32(src, sstp, dest, dstp, fftspec, fftbuff)    ippiFFTFwd_CToC_32fc_C1R(src, sstp, dest, dstp, fftspec, fftbuff)
#define vector2DFreeFFTC_cf32(fftspec)                                    ippiFFTFree_C_32fc(fftspec)
#define vector2DInitFFTC_cf32(fftspec, orderx, ordery, flag, hint)              ippiFFTInitAlloc_C_32fc(fftspec, orderx, ordery, flag, hint)
#define vectorSqrt_f32_I(srcdest, len)                                    ippsSqrt_32f_I(srcdest, len)

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <fftw3.h>

//start with the types
typedef struct {
  short re;
  short im;
} sc16;

// Danger here: fftw_complex value[1] is equivilent to float value[0]=real part and value[1]=imag part. 
// These structures are implicitly the same, but is it guaranteed? 
typedef struct {
  float re;
  float im;
} fc32;  // = fftwf_complex

typedef struct {
  double re;
  double im;
} fc64;  // = fftw_complex

// These fill in for the IppsFFTSpec_R_32f structures. 
typedef struct { 
  fftwf_plan p;
  f32 * in; 
  cf32 * out; // Perhaps should be fftwf_complex
  int len; int len2; int len3;
} GenFFTPtrRf32;

typedef struct { 
  fftwf_plan p;
  f32 * out; 
  cf32 * in; // Perhaps should be fftwf_complex
  int len; int len2; int len3;
} GenFFTPtrCRf32;

typedef struct { 
  fftwf_plan p;
  cf32 * in; // Perhaps should be fftwf_complex
  cf32 * out; // Perhaps should be fftwf_complex
  int len; int len2; int len3;
} GenFFTPtrCfc32;

typedef struct { 
  fftwf_plan p;
  f32 * in_re; 
  f32 * in_im; 
  f32 * out_re; 
  f32 * out_im; 
  int len; int len2; int len3;
} GenFFTPtrCf32;

typedef struct { 
  fftw_plan p;
  cf64 * in; 
  cf64 * out; 
  int len; int len2; int len3;
} GenFFTPtrC64;

//then the generic functions
// Copy // Replace with memmove(d,s,l*size(d[0]))
inline vecStatus genericCopy_u8(const u8 * src, u8 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]; return vecNoErr; }
inline vecStatus genericCopy_s16(const s16 * src, s16 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]; return vecNoErr; }
inline vecStatus genericCopy_s32(const s32 * src, s32 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]; return vecNoErr; }
inline vecStatus genericCopy_f32(const f32 * src, f32 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]; return vecNoErr; }
inline vecStatus genericCopy_cf32(const cf32 * src, cf32 * dest, int length)
{ for(int i=0;i<length;i++) {dest[i].re = src[i].re;dest[i].im = src[i].im;} return vecNoErr; }
inline vecStatus genericCopy_f64(const f64 * src, f64 * dest, int length)
{ for(int i=0;i<length;i++) {dest[i] = src[i];} return vecNoErr; }

// Zero // Replace with memset(d,0,l*size(d[0]))
inline vecStatus genericZero_32s(s32 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = 0; return vecNoErr; } // memset(dest,0,length*sizeof(dest[0]))
inline vecStatus genericZero_32f(f32 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = 0; return vecNoErr; }
inline vecStatus genericZero_32fc(cf32 * dest, int length)
{ memset((void *) dest, 0, length*sizeof(dest[0])); return vecNoErr; } 
//{ for(int i=0;i<length;i++) { dest[i].re = 0; dest[i].im = 0; } return vecNoErr; }

inline vecStatus genericZero_64fc(cf64 * dest, int length)
{for(int i=0;i<length;i++) { dest[i].re = 0; dest[i].im = 0; } return vecNoErr;}
inline vecStatus genericZero_16s(s16 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = 0; return vecNoErr; }

//Add
/* #define vectorAdd_f32_I(src, srcdest, length)                               ippsAdd_32f_I(src, srcdest, length) */
inline vecStatus genericAdd_32f_I(const f32 *src, f32 *srcdest, int length)
{ for(int i=0;i<length;i++) srcdest[i] += src[i]; return vecNoErr; }
/* #define vectorAdd_f64_I(src, srcdest, length)                               ippsAdd_64f_I(src, srcdest, length) */
inline vecStatus genericAdd_64f_I(const f64 *src, f64 *srcdest, int length)
{ for(int i=0;i<length;i++) srcdest[i] += src[i]; return vecNoErr; }
/* #define vectorAdd_s16_I(src, srcdest, length)                               ippsAdd_16s_I(src, srcdest, length) */
inline vecStatus genericAdd_16s_I(const s16 *src, s16 *srcdest, int length)
{ for(int i=0;i<length;i++) srcdest[i] += src[i]; return vecNoErr; }
/* #define vectorAdd_s32_I(src, srcdest, length)                               ippsAdd_32s_ISfs(src, srcdest, length, 0) */
inline vecStatus genericAdd_32s_I(const s32 *src, s32 *srcdest, int length)
{ for(int i=0;i<length;i++) srcdest[i] += src[i]; return vecNoErr; }
/* #define vectorAdd_cf32_I(src, srcdest, length)                              ippsAdd_32fc_I(src, srcdest, length) */
inline vecStatus genericAdd_32fc_I(const cf32 *src, cf32 *srcdest, int length)
{ for(int i=0;i<length;i++) {srcdest[i].re += src[i].re;srcdest[i].im += src[i].im;} return vecNoErr; }
/* #define vectorAdd_cf64_I(src, srcdest, length)                              ippsAdd_64fc_I(src, srcdest, length) */
inline vecStatus genericAdd_64fc_I(cf64 *src, cf64 *srcdest, int length)
{ for(int i=0;i<length;i++) {srcdest[i].re += src[i].re;srcdest[i].im += src[i].im;} return vecNoErr; }
/* #define vectorAddC_f64(src, val, dest, length)                              ippsAddC_64f(src, val, dest, length) */
inline vecStatus genericAddC_64f(const f64 *src,const f64 val, f64 *dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]+val; return vecNoErr; }
/* #define vectorAddC_f32(src, val, dest, length)                              ippsAddC_32f(src, val, dest, length) */
inline vecStatus genericAddC_32f(const f32 *src,const f32 val, f32 *dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]+val; return vecNoErr; }
/* #define vectorAddC_f32_I(val, srcdest, length)                              ippsAddC_32f_I(val, srcdest, length) */
inline vecStatus genericAddC_32f_I(const f32 val, f32 *srcdest, int length)
{ for(int i=0;i<length;i++) srcdest[i] += val; return vecNoErr; }
/* #define vectorAddC_s16_I(val, srcdest, length)                              ippsAddC_16s_I(val, srcdest, length) */
inline vecStatus genericAddC_16s_I(const s16 val, s16 *srcdest, int length)
{ for(int i=0;i<length;i++) srcdest[i] += val; return vecNoErr; }
/* #define vectorAddC_f64_I(val, srcdest, length)                              ippsAddC_64f_I(val, srcdest, length) */
inline vecStatus genericAddC_64f_I(const f64 val, f64 *srcdest, int length)
{ for(int i=0;i<length;i++) srcdest[i] += val; return vecNoErr; }

/* #define vectorAddProduct_cf32(src1, src2, accumulator, length)              ippsAddProduct_32fc(src1, src2, accumulator, length) */
inline vecStatus genericAddProduct_32fc(const cf32 *src1,const cf32 *src2, cf32 *accumulator, int length)
{ // accumulator[0].re=accumulator[0].im=0; 
  for(int i=0;i<length;i++) 
    { accumulator[i].re += src1[i].re*src2[i].re-src1[i].im*src2[i].im; 
      accumulator[i].im += src1[i].re*src2[i].im+src1[i].im*src2[i].re; }
     return vecNoErr; }

/* #define vectorConj_cf32(src, dest, length)                                  ippsConj_32fc(src, dest, length) */
inline vecStatus genericConj_32fc(const cf32 *src, cf32 *dest, int length)
{ for(int i=0;i<length;i++) {dest[i].re = src[i].re;dest[i].im = -src[i].im;} return vecNoErr; }

/* #define vectorConjFlip_cf32(src, dest, length)                              ippsConjFlip_32fc(src, dest, length) */
inline vecStatus genericConjFlip_32fc(const cf32 *src, cf32 *dest, int length)
{ for(int i=0;i<length;i++) {dest[i].re = src[length-1-i].re;dest[i].im = -src[length-1-i].im;} return vecNoErr; }

/* #define vectorCos_f32(src, dest, length)                                    ippsCos_32f_A11(src, dest, length) */
inline vecStatus genericCos_32f(const f32 *src, f32 *dest, int length)
{ for(int i=0;i<length;i++) {dest[i] = cosf(src[i]);} return vecNoErr; }

/* #define vectorConvertScaled_s16f32(src, dest, length, scalefactor)          ippsConvert_16s32f_Sfs(src, dest, length, scalefactor) */
inline vecStatus genericConvert_16s32f(const s16 *src, f32 *dest, int length, int scalefactor) 
{f32 tmp;tmp=powf(2,-scalefactor); for(int i=0;i<length;i++) dest[i] = src[i]*tmp; return vecNoErr; }

/* #define vectorConvertScaled_f32s16(src, dest, length, rndmode, scalefactor) ippsConvert_32f16s_Sfs(src, dest, length, rndmode, scalefactor) */
inline vecStatus genericConvert_32f16s(const f32 *src, s16 *dest, int length, int rndmode, int scalefactor) 
{f32 tmp;tmp=powf(2,-scalefactor); for(int i=0;i<length;i++) dest[i] = src[i]*tmp; return vecNoErr; }
/* #define vectorConvertScaled_f32u8(src, dest, length, rndmode, scalefactor)  ippsConvert_32f8u_Sfs(src, dest, length, rndmode, scalefactor) */
inline vecStatus genericConvert_32f8u(const f32 *src, u8 *dest, int length, int rndmode, int scalefactor) 
{f32 tmp;tmp=powf(2,-scalefactor); for(int i=0;i<length;i++) dest[i] = src[i]*tmp; return vecNoErr; }
/* #define vectorConvert_f32s32(src, dest, length, rndmode)                    ippsConvert_32f32s_Sfs(src, dest, length, rndmode, 0) */
inline vecStatus genericConvert_32f32s(const f32 * src, s32 * dest, int length, int rndmode) // Have not included rounding mode!!!
{ for(int i=0;i<length;i++) dest[i] = src[i]; return vecNoErr; }
inline vecStatus genericConvert_16s32f(const s16 * src, f32 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]; return vecNoErr; }
inline vecStatus genericConvert_32s32f(const s32 * src, f32 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]; return vecNoErr; }
inline vecStatus genericConvert_64f32f(const f64 * src, f32 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]; return vecNoErr; }
inline vecStatus genericConvert_32f64f(const f32 * src, f64 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]; return vecNoErr; }


/* #define vectorDivide_f32(src1, src2, dest, length)                          ippsDiv_32f(src1, src2, dest, length) */
inline vecStatus genericDivide_32f(const f32* src1,const f32 *src2, f32 *dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src1[i]/src2[i]; return vecNoErr; }

/* #define vectorDotProduct_f64(src1, src2, length, output)                    ippsDotProd_64f(src1, src2, length, output) */
inline vecStatus genericDotProduct_64f(const f64 *src1, const f64 *src2, int length, f64* output)
{ output[0]=0; 
  for(int i=0;i<length;i++) { output[0] += src1[i]*src2[i];} return vecNoErr; }

/* #define vectorFlip_f64_I(srcdest, length)                                   ippsFlip_64f_I(srcdest, length) */
inline vecStatus genericFlip_64f_I(f64 *srcdest, int length)
{ f64 tmp; for(int i=0;i<length;i++) {tmp = srcdest[i];  srcdest[i] = srcdest[length-1-i];  srcdest[length-1-i] =tmp;} return vecNoErr; }

//#define vectorMagnitude_cf32(src, dest, length)                             ippsMagnitude_32fc(src, dest, length)
// Check type of SRC
inline vecStatus genericMagnitude_32fc(const cf32 *src, f32 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = sqrt(src[i].re*src[i].re+src[i].im*src[i].im); return vecNoErr; }

/* #define vectorMean_cf32(src, length, mean, hint)                            ippsMean_32fc(src, length, mean, hint) */
inline vecStatus genericMean_32fc(cf32 *src, int length ,cf32* mean, int hint) // Alg options not used
{ mean[0].re=mean[0].im=0; 
  for(int i=0;i<length;i++) {mean[0].re += (src[i].re); mean[0].im += (src[i].im);}
  if (length>0) {mean[0].re /= length; mean[0].im /= length;} return vecNoErr; }

/* #define vectorMul_f32(src1, src2, dest, length)                             ippsMul_32f(src1, src2, dest, length) */
inline vecStatus genericMul_32f(const f32 *src1, const f32 *src2, f32 *dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src1[i]*src2[i]; return vecNoErr; }
/* #define vectorMul_f32_I(src, srcdest, length)                               ippsMul_32f_I(src, srcdest, length) */
inline vecStatus genericMul_32f_I(const f32 *src, f32 *srcdest, int length)
{ for(int i=0;i<length;i++) srcdest[i] *= src[i]; return vecNoErr; }
/* #define vectorMul_cf32_I(src, srcdest, length)                              ippsMul_32fc_I(src, srcdest, length) */
inline vecStatus genericMul_32fc_I(const cf32 *src, cf32 *srcdest, int length)
{f32 tmp; for(int i=0;i<length;i++) 
	    {tmp=srcdest[i].re*src[i].re-srcdest[i].im*src[i].im;
	      srcdest[i].im = srcdest[i].re*src[i].im+srcdest[i].im*src[i].re;
	      srcdest[i].re = tmp;} 
  return vecNoErr; }
/* #define vectorMul_cf32(src1, src2, dest, length)                            ippsMul_32fc(src1, src2, dest, length) */
inline vecStatus genericMul_32fc(const cf32 *src1, const cf32 *src2, cf32 *dest, int length)
{ for(int i=0;i<length;i++) 
    {dest[i].re = src1[i].re*src2[i].re-src1[i].im*src2[i].im; 
     dest[i].im = src1[i].re*src2[i].im+src1[i].im*src2[i].re;} 
      return vecNoErr; }
/* #define vectorMul_f32cf32(src1, src2, dest, length)                         ippsMul_32f32fc(src1, src2, dest, length) */
inline vecStatus genericMul_32f32fc(const f32 *src1,const cf32 *src2, cf32 *dest, int length)
{ for(int i=0;i<length;i++) {dest[i].re = src1[i]*src2[i].re; dest[i].im = src1[i]*src2[i].im;} return vecNoErr; }

/* #define vectorMulC_f32(src, val, dest, length)                              ippsMulC_32f(src, val, dest, length) */
inline vecStatus genericMulC_32f(const f32 *src, const f32 val, f32 *dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]*val; return vecNoErr; }
/* #define vectorMulC_cs16_I(val, srcdest, length)                             ippsMulC_16sc_ISfs(val, srcdest, length, 0) */
/* inline vecStatus genericMulC_16sc_I(sc16 val, sc16 *srcdest, int length) */
/* {s16 tmp;  for(int i=0;i<length;i++)  */
/* 	     { tmp = srcdest[i].re*val.re-srcdest[i].im*val.im;  */
/* 	       srcdest[i].im = srcdest[i].re*val.im+srcdest[i].im*val.re; */
/* 	       srcdest[i].re = tmp;}  */
/*   return vecNoErr; } */

/* #define vectorMulC_f32_I(val, srcdest, length)                              ippsMulC_32f_I(val, srcdest, length) */
inline vecStatus genericMulC_32f_I(const f32 val, f32 *srcdest, int length)
{ for(int i=0;i<length;i++) srcdest[i] *= val; return vecNoErr; }
/* #define vectorMulC_cf32_I(val, srcdest, length)                             ippsMulC_32fc_I(val, srcdest, length) */
inline vecStatus genericMulC_32fc_I(const cf32 val, cf32 *srcdest, int length)
{f32 tmp; for(int i=0;i<length;i++) 
	    {tmp=srcdest[i].re*val.re-srcdest[i].im*val.im;
	      srcdest[i].im = srcdest[i].re*val.im+srcdest[i].im*val.re;
	      srcdest[i].re=tmp;} 
  return vecNoErr; }
/* #define vectorMulC_cf32(src, val, dest, length)                             ippsMulC_32fc(src, val, dest, length) */
inline vecStatus genericMulC_32fc(const cf32 *src,const cf32 val, cf32 *dest, int length)
{ for(int i=0;i<length;i++) 
    {dest[i].re = src[i].re*val.re-src[i].im*val.im;
     dest[i].im = src[i].re*val.im+src[i].im*val.re;} 
  return vecNoErr; }
/* #define vectorMulC_f64_I(val, srcdest, length)                              ippsMulC_64f_I(val, srcdest, length) */
inline vecStatus genericMulC_64f_I(const f64 val, f64 *srcdest, int length)
{ for(int i=0;i<length;i++) {srcdest[i] *= val;} return vecNoErr; }
/* #define vectorMulC_f64(src, val, dest, length)                              ippsMulC_64f(src, val, dest, length) */
inline vecStatus genericMulC_64f(const f64 *src,const f64 val, f64 *dest, int length)
{ for(int i=0;i<length;i++) {dest[i] = src[i]*val;} return vecNoErr; }

/* #define vectorPhase_cf32(src, dest, length)                                 ippsPhase_32fc(src, dest, length) */
inline vecStatus genericPhase_32fc(const cf32 *src, f32 *dest, int length)
{ for(int i=0;i<length;i++) {dest[i] = atan2(src[i].im,src[i].re);} return vecNoErr; }

/* #define vectorRealToComplex_f32(real, imag, complex, length)                ippsRealToCplx_32f(real, imag, complex, length) */
inline vecStatus genericRealToCplx_32f(const f32 *real, const f32 *imag, cf32 *complx, int length)
{ for(int i=0;i<length;i++) {if (real) complx[i].re = real[i]; else complx[i].re=0; if (imag) complx[i].im=imag[i]; else complx[i].im=0;} return vecNoErr; }

/* #define vectorReal_cf32(complex, real, length)                              ippsReal_32fc(complex, real, length) */
inline vecStatus genericReal_32fc(const cf32 *c, f32 *re, int l)
{ for(int i=0;i<l;i++) {re[i] = c[i].re;} return vecNoErr; }

inline vecStatus genericSet_32f(const f32 val, f32 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = val; return vecNoErr; } // memset?

/* #define vectorSin_f32(src, dest, length)                                    ippsSin_32f_A11(src, dest, length) */
inline vecStatus genericSin_32f(const f32 *src, f32 *dest, int length)
{ for(int i=0;i<length;i++) {dest[i] = sinf(src[i]);} return vecNoErr; }

/* #define vectorSinCos_f32(src, sin, cos, length)                             ippsSinCos_32f_A11(src, sin, cos, length) */
inline vecStatus genericSinCos_32f(const f32 *src, f32 *sin, f32 *cos, int length)
{ for(int i=0;i<length;i++) {sincosf(src[i],sin+i,cos+i);} return vecNoErr; }

/* #define vectorSplitScaled_s16f32(src, dest, numchannels, chanlen)           ippsSplitScaled_16s32f_D2L(src, dest, numchannels, chanlen) */
inline vecStatus genericSplitScaled_16s32f(const s16 *src, f32 **dest, int numchannels, int chanlen)
{ s16 max=-MAX_S16; s16 min=MAX_S16; for(int n=0;n<numchannels*chanlen;n++) {max = (max>src[n])? max:src[n]; min = (min<src[n])? min:src[n];}
  f32 scale; scale = 2.0/(max-min); // printf("RGD: Scale %f Max %d Min %d\n",scale,max,min);
  for(int n=0;n<chanlen;n++) for(int m=0;m<numchannels;m++) {
      if (max==min) dest[m][n]=0; 
      else dest[m][n] = -1.0+scale*(src[n*numchannels+m]-min);
      //printf("RGD: Scale %f Max %d Min %d: %d %f %d %d\n",scale,max,min,src[n*chanlen+m],dest[n][m],n,m);
    } return vecNoErr; }

/* #define vectorSquare_f32_I(srcdest, length)                                 ippsSqr_32f_I(srcdest, length) */
inline vecStatus genericSqr_32f_I(f32 *srcdest, int length)
{ for(int i=0;i<length;i++) {srcdest[i] *= srcdest[i];} return vecNoErr; }
/* #define vectorSquare_f64_I(srcdest, length)                                 ippsSqr_64f_I(srcdest, length) */
inline vecStatus genericSqr_64f_I(f64 *srcdest, int length)
{ for(int i=0;i<length;i++) {srcdest[i] *= srcdest[i];} return vecNoErr; }

/* #define vectorSub_f32_I(src, srcdest, length)                               ippsSub_32f_I(src, srcdest, length) */
inline vecStatus genericSub_32f_I(const f32 *src, f32 *srcdest, int length)
{ for(int i=0;i<length;i++) {srcdest[i] -= src[i];} return vecNoErr; }

/* #define vectorSub_s32(src1, src2, dest, length)                             ippsSub_32s_Sfs(src1, src2, dest, length, 0) */
inline vecStatus genericSub_32s(const s32 *src1,const s32 *src2, s32 *dest, int length)
{ for(int i=0;i<length;i++) {dest[i] = src1[i]-src2[i];} return vecNoErr; }
/* #define vectorSub_cf32_I(src, srcdest, length)                              ippsSub_32fc_I(src, srcdest, length) */
inline vecStatus genericSub_32fc_I(const cf32 *src, cf32 *srcdest, int length)
{ for(int i=0;i<length;i++) {srcdest[i].re -= src[i].re;srcdest[i].im -= src[i].im;} return vecNoErr; }

/* #define vectorSum_cf32(src, length, sum, hint)                              ippsSum_32fc(src, length, sum, hint) */
inline vecStatus genericSum_32fc(const cf32 *src, int length, cf32 *sum)
{ sum[0].re=0;sum[0].im=0; for(int i=0;i<length;i++) {sum[0].re += src[i].re;sum[0].im += src[i].im;} return vecNoErr; }

// Bpass
/* #define vectorGenerateFIRLowpass_f64(freq, taps, length, window, normalise) ippsFIRGenLowpass_64f(freq, taps, length, window, normalise) */
//http://software.intel.com/sites/products/documentation/hpc/compilerpro/en-us/cpp/lin/ipp/ipps/ipps_ch6/functn_FIRGenLowpass.html
// I believe it is NEVER USED? 
// inline vecStatus genericNULLFIRGen(f64 freq, f64 *taps, int length, vecStatus window, vecStatus normalise) {return vecNoErr;} 

// FFTs
inline vecStatus genericInitFFTR_f32(GenFFTPtrRf32 **fftspec, int order, int flag, vecHintAlg hint) 
{ fftspec[0] = (GenFFTPtrRf32 *) malloc(sizeof(GenFFTPtrRf32)); 
  fftspec[0]->len = 2<<order;
  fftspec[0]->in = (f32 *) fftwf_malloc(fftspec[0]->len*sizeof(f32)); 
  fftspec[0]->out = (cf32 *) fftwf_malloc((fftspec[0]->len/2+1)*sizeof(cf32)); 
  fftspec[0]->p = fftwf_plan_dft_r2c_1d(fftspec[0]->len,fftspec[0]->in, (fftwf_complex *) fftspec[0]->out, FFTW_ESTIMATE); 
  return vecNoErr;} // Always FORWARD

inline vecStatus genericInitFFTC_cf32(GenFFTPtrCfc32 **fftspec, int order, int flag, vecHintAlg hint)
{ fftspec[0] = (GenFFTPtrCfc32 *) malloc(sizeof(GenFFTPtrCfc32)); 
  fftspec[0]->len = 2<<order;
  fftspec[0]->in = (cf32 *) fftwf_malloc(fftspec[0]->len*sizeof(cf32));
  fftspec[0]->out = (cf32 *) fftwf_malloc(fftspec[0]->len*sizeof(cf32));
  fftspec[0]->p = fftwf_plan_dft_1d(fftspec[0]->len,(fftwf_complex *) fftspec[0]->in,(fftwf_complex *) fftspec[0]->out, FFTW_FORWARD, FFTW_ESTIMATE); 
  return vecNoErr;} // Always FORWARD

inline vecStatus genGetFFTBufSizeR_f32(GenFFTPtrRf32* fftspec, int *buffersize)  { buffersize[0] = fftspec->len; return vecNoErr; }
inline vecStatus genGetFFTBufSizeC_cf32(GenFFTPtrCfc32* fftspec, int *buffersize)  { buffersize[0] = fftspec->len; return vecNoErr; }

inline vecStatus genFreeFFTR_f32(GenFFTPtrRf32* fftspec) { fftwf_free(fftspec->in);fftwf_free(fftspec->out);fftwf_destroy_plan(fftspec->p);free(fftspec);return vecNoErr; }
inline vecStatus genFreeFFTC_cf32(GenFFTPtrCfc32* fftspec) { fftwf_free(fftspec->in);fftwf_free(fftspec->out);fftwf_destroy_plan(fftspec->p);free(fftspec);return vecNoErr; }

// Should use memmove? Is the (len/2+1) copy working as expected? 
inline vecStatus genFFT_RtoC_f32(const f32* src, f32* dest, GenFFTPtrRf32*fftspec,u8 * fftbuffer)
   { memcpy(fftspec->in, src, fftspec->len*sizeof(f32)); 
     fftwf_execute(fftspec->p); 
     memcpy(dest, fftspec->out, (fftspec->len/2+1)*sizeof(cf32)); 
     return vecNoErr; }
inline vecStatus genFFT_CtoC_cf32(const cf32 *src, cf32 *dest, GenFFTPtrCfc32* fftspec, u8 *fftbuffer)  
   { memcpy(fftspec->in, src, fftspec->len*sizeof(cf32)); 
     fftwf_execute(fftspec->p);
     memcpy(dest, fftspec->out, fftspec->len*sizeof(cf32));
     return vecNoErr; }
inline vecStatus genFFT_CtoR_f32(const f32* src, f32* dest, GenFFTPtrRf32*fftspec,u8 * fftbuffer)
   { memcpy(fftspec->in, src, (fftspec->len/2+1)*sizeof(cf32)); 
     fftwf_execute(fftspec->p); 
     memcpy(dest, fftspec->out, fftspec->len*sizeof(f32)); 
     return vecNoErr; }


#endif /*Architecture == Intel */

#if(ARCH == AMD) // Use FRAMEWAVE?
#error "Sorry: AMD not yet implemented"
#endif  /*Architecture == AMD */

#if (ARCH == GENERIC) // Using FFTW 

#define u8                       unsigned char
#define u16                      unsigned short
#define s16                      short
#define cs16                     sc16
#define s32                      int
#define f32                      float
#define f64                      double
#define cf32                     fc32  // fftwf_complex
#define cf64                     fc64  // fftw_complex
#define s64                      long long
#define u64                      unsigned long long
#define vecStatus                int

//and the constant values
#define vecFFTSpecR_f32          GenFFTPtrRf32
#define vecFFTSpecC_f32          GenFFTPtrCf32  // not used
#define vecFFTSpecC_cf32         GenFFTPtrCfc32
#define vecDFTSpecC_cf32         GenFFTPtrCfc32
#define vecDFTSpecR_f32          GenFFTPtrRf32
#define vecNoErr                 0
#define vecTrue                  1
#define MAX_S16                  ((1<<15) - 1)
#define MAX_U16                  ((1<<16) - 1)
#define MAX_S32                  MAX_S16 // ((1<<31) - 1)
#define MAX_U32                  MAX_U16 // ((1<<32) - 1)
#define TWO_PI                   6.283185307179586476925286766559
#define vecHintAlg               int
#define vecFFT_NoReNorm          NULL // FFTW is always no renomalise
#define vecFFT_ReNorm            NULL // FFTW is always no renomalise
#define vecAlgHintFast           NULL // FFTW is always Fast
#define vecAlgHintAccurate       NULL

//vector allocation and deletion routines
#define vectorAlloc_u8(length)   new u8[length]
#define vectorAlloc_s16(length)  new s16[length]
#define vectorAlloc_cs16(length) new cs16[length]
#define vectorAlloc_s32(length)  new s32[length]
#define vectorAlloc_f32(length)  new f32[length]
#define vectorAlloc_cf32(length) new cf32[length]
#define vectorAlloc_f64(length)  new f64[length]
#define vectorAlloc_cf64(length) new cf64[length]

#define vectorFree(memptr)       delete [] memptr

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <fftw3.h>

//start with the types
typedef struct {
  short re;
  short im;
} sc16;

// Danger here: fftw_complex value[1] is equivilent to float value[0]=real part and value[1]=imag part. 
// These structures are implicitly the same, but is it guaranteed? 
typedef struct {
  float re;
  float im;
} fc32;  // = fftwf_complex

typedef struct {
  double re;
  double im;
} fc64;  // = fftw_complex

// These fill in for the IppsFFTSpec_R_32f structures. 
typedef struct { 
  fftwf_plan p;
  f32 * in; 
  cf32 * out; // Perhaps should be fftwf_complex
  int len; int len2; int len3;
} GenFFTPtrRf32;

typedef struct { 
  fftwf_plan p;
  f32 * out; 
  cf32 * in; // Perhaps should be fftwf_complex
  int len; int len2; int len3;
} GenFFTPtrCRf32;

typedef struct { 
  fftwf_plan p;
  cf32 * in; // Perhaps should be fftwf_complex
  cf32 * out; // Perhaps should be fftwf_complex
  int len; int len2; int len3;
} GenFFTPtrCfc32;

typedef struct { 
  fftwf_plan p;
  f32 * in_re; 
  f32 * in_im; 
  f32 * out_re; 
  f32 * out_im; 
  int len; int len2; int len3;
} GenFFTPtrCf32;

typedef struct { 
  fftw_plan p;
  cf64 * in; 
  cf64 * out; 
  int len; int len2; int len3;
} GenFFTPtrC64;

//then the generic functions
// Copy // Replace with memmove(d,s,l*size(d[0]))
inline vecStatus genericCopy_u8(const u8 * src, u8 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]; return vecNoErr; }
inline vecStatus genericCopy_s16(const s16 * src, s16 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]; return vecNoErr; }
inline vecStatus genericCopy_s32(const s32 * src, s32 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]; return vecNoErr; }
inline vecStatus genericCopy_f32(const f32 * src, f32 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]; return vecNoErr; }
inline vecStatus genericCopy_cf32(const cf32 * src, cf32 * dest, int length)
{ for(int i=0;i<length;i++) {dest[i].re = src[i].re;dest[i].im = src[i].im;} return vecNoErr; }
inline vecStatus genericCopy_f64(const f64 * src, f64 * dest, int length)
{ for(int i=0;i<length;i++) {dest[i] = src[i];} return vecNoErr; }

// Zero // Replace with memset(d,0,l*size(d[0]))
inline vecStatus genericZero_32s(s32 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = 0; return vecNoErr; } // memset(dest,0,length*sizeof(dest[0]))
inline vecStatus genericZero_32f(f32 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = 0; return vecNoErr; }
inline vecStatus genericZero_32fc(cf32 * dest, int length)
{ memset((void *) dest, 0, length*sizeof(dest[0])); return vecNoErr; } 
//{ for(int i=0;i<length;i++) { dest[i].re = 0; dest[i].im = 0; } return vecNoErr; }

inline vecStatus genericZero_64fc(cf64 * dest, int length)
{for(int i=0;i<length;i++) { dest[i].re = 0; dest[i].im = 0; } return vecNoErr;}
inline vecStatus genericZero_16s(s16 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = 0; return vecNoErr; }

//Add
/* #define vectorAdd_f32_I(src, srcdest, length)                               ippsAdd_32f_I(src, srcdest, length) */
inline vecStatus genericAdd_32f_I(const f32 *src, f32 *srcdest, int length)
{ for(int i=0;i<length;i++) srcdest[i] += src[i]; return vecNoErr; }
/* #define vectorAdd_f64_I(src, srcdest, length)                               ippsAdd_64f_I(src, srcdest, length) */
inline vecStatus genericAdd_64f_I(const f64 *src, f64 *srcdest, int length)
{ for(int i=0;i<length;i++) srcdest[i] += src[i]; return vecNoErr; }
/* #define vectorAdd_s16_I(src, srcdest, length)                               ippsAdd_16s_I(src, srcdest, length) */
inline vecStatus genericAdd_16s_I(const s16 *src, s16 *srcdest, int length)
{ for(int i=0;i<length;i++) srcdest[i] += src[i]; return vecNoErr; }
/* #define vectorAdd_s32_I(src, srcdest, length)                               ippsAdd_32s_ISfs(src, srcdest, length, 0) */
inline vecStatus genericAdd_32s_I(const s32 *src, s32 *srcdest, int length)
{ for(int i=0;i<length;i++) srcdest[i] += src[i]; return vecNoErr; }
/* #define vectorAdd_cf32_I(src, srcdest, length)                              ippsAdd_32fc_I(src, srcdest, length) */
inline vecStatus genericAdd_32fc_I(const cf32 *src, cf32 *srcdest, int length)
{ for(int i=0;i<length;i++) {srcdest[i].re += src[i].re;srcdest[i].im += src[i].im;} return vecNoErr; }
/* #define vectorAdd_cf64_I(src, srcdest, length)                              ippsAdd_64fc_I(src, srcdest, length) */
inline vecStatus genericAdd_64fc_I(cf64 *src, cf64 *srcdest, int length)
{ for(int i=0;i<length;i++) {srcdest[i].re += src[i].re;srcdest[i].im += src[i].im;} return vecNoErr; }
/* #define vectorAddC_f64(src, val, dest, length)                              ippsAddC_64f(src, val, dest, length) */
inline vecStatus genericAddC_64f(const f64 *src,const f64 val, f64 *dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]+val; return vecNoErr; }
/* #define vectorAddC_f32(src, val, dest, length)                              ippsAddC_32f(src, val, dest, length) */
inline vecStatus genericAddC_32f(const f32 *src,const f32 val, f32 *dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]+val; return vecNoErr; }
/* #define vectorAddC_f32_I(val, srcdest, length)                              ippsAddC_32f_I(val, srcdest, length) */
inline vecStatus genericAddC_32f_I(const f32 val, f32 *srcdest, int length)
{ for(int i=0;i<length;i++) srcdest[i] += val; return vecNoErr; }
/* #define vectorAddC_s16_I(val, srcdest, length)                              ippsAddC_16s_I(val, srcdest, length) */
inline vecStatus genericAddC_16s_I(const s16 val, s16 *srcdest, int length)
{ for(int i=0;i<length;i++) srcdest[i] += val; return vecNoErr; }
/* #define vectorAddC_f64_I(val, srcdest, length)                              ippsAddC_64f_I(val, srcdest, length) */
inline vecStatus genericAddC_64f_I(const f64 val, f64 *srcdest, int length)
{ for(int i=0;i<length;i++) srcdest[i] += val; return vecNoErr; }

/* #define vectorAddProduct_cf32(src1, src2, accumulator, length)              ippsAddProduct_32fc(src1, src2, accumulator, length) */
inline vecStatus genericAddProduct_32fc(const cf32 *src1,const cf32 *src2, cf32 *accumulator, int length)
{ // accumulator[0].re=accumulator[0].im=0; 
  for(int i=0;i<length;i++) 
    { accumulator[i].re += src1[i].re*src2[i].re-src1[i].im*src2[i].im; 
      accumulator[i].im += src1[i].re*src2[i].im+src1[i].im*src2[i].re; }
     return vecNoErr; }

/* #define vectorConj_cf32(src, dest, length)                                  ippsConj_32fc(src, dest, length) */
inline vecStatus genericConj_32fc(const cf32 *src, cf32 *dest, int length)
{ for(int i=0;i<length;i++) {dest[i].re = src[i].re;dest[i].im = -src[i].im;} return vecNoErr; }

/* #define vectorConjFlip_cf32(src, dest, length)                              ippsConjFlip_32fc(src, dest, length) */
inline vecStatus genericConjFlip_32fc(const cf32 *src, cf32 *dest, int length)
{ for(int i=0;i<length;i++) {dest[i].re = src[length-1-i].re;dest[i].im = -src[length-1-i].im;} return vecNoErr; }

/* #define vectorCos_f32(src, dest, length)                                    ippsCos_32f_A11(src, dest, length) */
inline vecStatus genericCos_32f(const f32 *src, f32 *dest, int length)
{ for(int i=0;i<length;i++) {dest[i] = cosf(src[i]);} return vecNoErr; }

/* #define vectorConvertScaled_s16f32(src, dest, length, scalefactor)          ippsConvert_16s32f_Sfs(src, dest, length, scalefactor) */
inline vecStatus genericConvert_16s32f(const s16 *src, f32 *dest, int length, int scalefactor) 
{f32 tmp;tmp=powf(2,-scalefactor); for(int i=0;i<length;i++) dest[i] = src[i]*tmp; return vecNoErr; }

/* #define vectorConvertScaled_f32s16(src, dest, length, rndmode, scalefactor) ippsConvert_32f16s_Sfs(src, dest, length, rndmode, scalefactor) */
inline vecStatus genericConvert_32f16s(const f32 *src, s16 *dest, int length, int rndmode, int scalefactor) 
{f32 tmp;tmp=powf(2,-scalefactor); for(int i=0;i<length;i++) dest[i] = src[i]*tmp; return vecNoErr; }
/* #define vectorConvertScaled_f32u8(src, dest, length, rndmode, scalefactor)  ippsConvert_32f8u_Sfs(src, dest, length, rndmode, scalefactor) */
inline vecStatus genericConvert_32f8u(const f32 *src, u8 *dest, int length, int rndmode, int scalefactor) 
{f32 tmp;tmp=powf(2,-scalefactor); for(int i=0;i<length;i++) dest[i] = src[i]*tmp; return vecNoErr; }
/* #define vectorConvert_f32s32(src, dest, length, rndmode)                    ippsConvert_32f32s_Sfs(src, dest, length, rndmode, 0) */
inline vecStatus genericConvert_32f32s(const f32 * src, s32 * dest, int length, int rndmode) // Have not included rounding mode!!!
{ for(int i=0;i<length;i++) dest[i] = src[i]; return vecNoErr; }
inline vecStatus genericConvert_16s32f(const s16 * src, f32 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]; return vecNoErr; }
inline vecStatus genericConvert_32s32f(const s32 * src, f32 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]; return vecNoErr; }
inline vecStatus genericConvert_64f32f(const f64 * src, f32 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]; return vecNoErr; }
inline vecStatus genericConvert_32f64f(const f32 * src, f64 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]; return vecNoErr; }


/* #define vectorDivide_f32(src1, src2, dest, length)                          ippsDiv_32f(src1, src2, dest, length) */
inline vecStatus genericDivide_32f(const f32* src1,const f32 *src2, f32 *dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src1[i]/src2[i]; return vecNoErr; }

//#define vectorDivC_cf32_I(val, srcdest, length)                             ippsDivC_32fc_I(val, srcdest, length)
inline vecStatus genericDivC_32fc_I(const cf32 val, cf32 *srcdest, int length)
{ f32 atmp, ptmp, aval, pval; aval=sqrt(val.re*val.re+val.im*val.im); pval=atan2(val.im,val.re); 
  for(int i=0;i<length;i++) {atmp=sqrt(srcdest[i].re*srcdest[i].re+srcdest[i].im*srcdest[i].im)/aval;
                             ptmp=atan2(srcdest[i].im,srcdest[i].re)-pval;
			     srcdest[i].re=atmp*cos(ptmp);srcdest[i].im =atmp*sin(ptmp);} 
  return vecNoErr; }

/* #define vectorDotProduct_f64(src1, src2, length, output)                    ippsDotProd_64f(src1, src2, length, output) */
inline vecStatus genericDotProduct_64f(const f64 *src1, const f64 *src2, int length, f64* output)
{ output[0]=0; 
  for(int i=0;i<length;i++) { output[0] += src1[i]*src2[i];} return vecNoErr; }

/* #define vectorFlip_f64_I(srcdest, length)                                   ippsFlip_64f_I(srcdest, length) */
inline vecStatus genericFlip_64f_I(f64 *srcdest, int length)
{ f64 tmp; for(int i=0;i<length;i++) {tmp = srcdest[i];  srcdest[i] = srcdest[length-1-i];  srcdest[length-1-i] =tmp;} return vecNoErr; }

//#define vectorMagnitude_cf32(src, dest, length)                             ippsMagnitude_32fc(src, dest, length)
// Check type of SRC
inline vecStatus genericMagnitude_32fc(const cf32 *src, f32 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = sqrt(src[i].re*src[i].re+src[i].im*src[i].im); return vecNoErr; }

//#define vectorMax_f32(src, dest, length)                                    ippsMax_32f(src, dest, length)
inline vecStatus genericMax_32f(const f32 *val, int length, f32 *max)
{ max[0]=-1E30; for(int i=0;i<length;i++) max[0] = (max[0]>val[i])? max[0]:val[i]; return vecNoErr; }

//#define vectorMin_f32(src, dest, length)                                    ippsMin_32f(src, dest, length)
inline vecStatus genericMin_32f(const f32 *val, int length, f32 *min)
{ min[0]=1E30; for(int i=0;i<length;i++) min[0] = (min[0]<val[i])? min[0]:val[i]; return vecNoErr; }

/* #define vectorMean_cf32(src, length, mean, hint)                            ippsMean_32fc(src, length, mean, hint) */
inline vecStatus genericMean_32fc(cf32 *src, int length ,cf32* mean, int hint) // Alg options not used
{ mean[0].re=mean[0].im=0; 
  for(int i=0;i<length;i++) {mean[0].re += (src[i].re); mean[0].im += (src[i].im);}
  if (length>0) {mean[0].re /= length; mean[0].im /= length;} return vecNoErr; }

/* #define vectorMul_f32(src1, src2, dest, length)                             ippsMul_32f(src1, src2, dest, length) */
inline vecStatus genericMul_32f(const f32 *src1, const f32 *src2, f32 *dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src1[i]*src2[i]; return vecNoErr; }
/* #define vectorMul_f32_I(src, srcdest, length)                               ippsMul_32f_I(src, srcdest, length) */
inline vecStatus genericMul_32f_I(const f32 *src, f32 *srcdest, int length)
{ for(int i=0;i<length;i++) srcdest[i] *= src[i]; return vecNoErr; }
/* #define vectorMul_cf32_I(src, srcdest, length)                              ippsMul_32fc_I(src, srcdest, length) */
inline vecStatus genericMul_32fc_I(const cf32 *src, cf32 *srcdest, int length)
{f32 tmp; for(int i=0;i<length;i++) 
	    {tmp=srcdest[i].re*src[i].re-srcdest[i].im*src[i].im;
	      srcdest[i].im = srcdest[i].re*src[i].im+srcdest[i].im*src[i].re;
	      srcdest[i].re = tmp;} 
  return vecNoErr; }
/* #define vectorMul_cf32(src1, src2, dest, length)                            ippsMul_32fc(src1, src2, dest, length) */
inline vecStatus genericMul_32fc(const cf32 *src1, const cf32 *src2, cf32 *dest, int length)
{ for(int i=0;i<length;i++) 
    {dest[i].re = src1[i].re*src2[i].re-src1[i].im*src2[i].im; 
     dest[i].im = src1[i].re*src2[i].im+src1[i].im*src2[i].re;} 
      return vecNoErr; }
/* #define vectorMul_f32cf32(src1, src2, dest, length)                         ippsMul_32f32fc(src1, src2, dest, length) */
inline vecStatus genericMul_32f32fc(const f32 *src1,const cf32 *src2, cf32 *dest, int length)
{ for(int i=0;i<length;i++) {dest[i].re = src1[i]*src2[i].re; dest[i].im = src1[i]*src2[i].im;} return vecNoErr; }

/* #define vectorMulC_f32(src, val, dest, length)                              ippsMulC_32f(src, val, dest, length) */
inline vecStatus genericMulC_32f(const f32 *src, const f32 val, f32 *dest, int length)
{ for(int i=0;i<length;i++) dest[i] = src[i]*val; return vecNoErr; }
/* #define vectorMulC_cs16_I(val, srcdest, length)                             ippsMulC_16sc_ISfs(val, srcdest, length, 0) */
/* inline vecStatus genericMulC_16sc_I(sc16 val, sc16 *srcdest, int length) */
/* {s16 tmp;  for(int i=0;i<length;i++)  */
/* 	     { tmp = srcdest[i].re*val.re-srcdest[i].im*val.im;  */
/* 	       srcdest[i].im = srcdest[i].re*val.im+srcdest[i].im*val.re; */
/* 	       srcdest[i].re = tmp;}  */
/*   return vecNoErr; } */

/* #define vectorMulC_f32_I(val, srcdest, length)                              ippsMulC_32f_I(val, srcdest, length) */
inline vecStatus genericMulC_32f_I(const f32 val, f32 *srcdest, int length)
{ for(int i=0;i<length;i++) srcdest[i] *= val; return vecNoErr; }

inline vecStatus genericDivC_32f_I(const f32 val, f32 *srcdest, int length)
{ return genericMulC_32f_I(1/val, srcdest, length); }

/* #define vectorMulC_cf32_I(val, srcdest, length)                             ippsMulC_32fc_I(val, srcdest, length) */
inline vecStatus genericMulC_32fc_I(const cf32 val, cf32 *srcdest, int length)
{f32 tmp; for(int i=0;i<length;i++) 
	    {tmp=srcdest[i].re*val.re-srcdest[i].im*val.im;
	      srcdest[i].im = srcdest[i].re*val.im+srcdest[i].im*val.re;
	      srcdest[i].re = tmp;} 
  return vecNoErr; }
/* #define vectorMulC_cf32(src, val, dest, length)                             ippsMulC_32fc(src, val, dest, length) */
inline vecStatus genericMulC_32fc(const cf32 *src,const cf32 val, cf32 *dest, int length)
{ for(int i=0;i<length;i++) 
    {dest[i].re = src[i].re*val.re-src[i].im*val.im;
     dest[i].im = src[i].re*val.im+src[i].im*val.re;} 
  return vecNoErr; }
/* #define vectorMulC_f64_I(val, srcdest, length)                              ippsMulC_64f_I(val, srcdest, length) */
inline vecStatus genericMulC_64f_I(const f64 val, f64 *srcdest, int length)
{ for(int i=0;i<length;i++) {srcdest[i] *= val;} return vecNoErr; }
/* #define vectorMulC_f64(src, val, dest, length)                              ippsMulC_64f(src, val, dest, length) */
inline vecStatus genericMulC_64f(const f64 *src,const f64 val, f64 *dest, int length)
{ for(int i=0;i<length;i++) {dest[i] = src[i]*val;} return vecNoErr; }

/* #define vectorPhase_cf32(src, dest, length)                                 ippsPhase_32fc(src, dest, length) */
inline vecStatus genericPhase_32fc(const cf32 *src, f32 *dest, int length)
{ for(int i=0;i<length;i++) {dest[i] = atan2(src[i].im,src[i].re);} return vecNoErr; }

/* #define vectorRealToComplex_f32(real, imag, complex, length)                ippsRealToCplx_32f(real, imag, complex, length) */
inline vecStatus genericRealToCplx_32f(const f32 *real, const f32 *imag, cf32 *complx, int length)
{ for(int i=0;i<length;i++) {if (real) complx[i].re = real[i]; else complx[i].re=0; if (imag) complx[i].im=imag[i]; else complx[i].im=0;} return vecNoErr; }

/* #define vectorReal_cf32(complex, real, length)                              ippsReal_32fc(complex, real, length) */
inline vecStatus genericReal_32fc(const cf32 *c, f32 *re, int l)
{ for(int i=0;i<l;i++) {re[i] = c[i].re;} return vecNoErr; }

inline vecStatus genericSet_32f(const f32 val, f32 * dest, int length)
{ for(int i=0;i<length;i++) dest[i] = val; return vecNoErr; } // memset?

/* #define vectorSin_f32(src, dest, length)                                    ippsSin_32f_A11(src, dest, length) */
inline vecStatus genericSin_32f(const f32 *src, f32 *dest, int length)
{ for(int i=0;i<length;i++) {dest[i] = sinf(src[i]);} return vecNoErr; }

/* #define vectorSinCos_f32(src, sin, cos, length)                             ippsSinCos_32f_A11(src, sin, cos, length) */
inline vecStatus genericSinCos_32f(const f32 *src, f32 *sin, f32 *cos, int length)
/* { for(int i=0;i<length;i++) {sincosf(src[i],sin+i,cos+i);} return vecNoErr; } */
{ for(int i=0;i<length;i++) sin[i]=sinf(src[i]);
  for(int i=0;i<length;i++) cos[i]=cosf(src[i]);
  return vecNoErr; }

/* #define vectorSplitScaled_s16f32(src, dest, numchannels, chanlen)           ippsSplitScaled_16s32f_D2L(src, dest, numchannels, chanlen) */
inline vecStatus genericSplitScaled_16s32f(const s16 *src, f32 **dest, int numchannels, int chanlen)
{ s16 max=0; s16 min=0; for(int n=0;n<numchannels*chanlen;n++) {max = (max>src[n])? max:src[n]; min = (min<src[n])? min:src[n];}
  f32 scale; scale = 2.0/(max-min); 
  for(int n=0;n<chanlen;n++) for(int m=0;m<numchannels;m++) {
      if (max==min) dest[m][n]=0; 
      else dest[m][n] = -1.0+scale*(src[n*numchannels+m]-min);
    } return vecNoErr; }

/* #define vectorSquare_f32_I(srcdest, length)                                 ippsSqr_32f_I(srcdest, length) */
inline vecStatus genericSqr_32f_I(f32 *srcdest, int length)
{ for(int i=0;i<length;i++) {srcdest[i] *= srcdest[i];} return vecNoErr; }

inline vecStatus genericSqrt_32f_I(f32 *srcdest, int length)
{ for(int i=0;i<length;i++) {srcdest[i] = sqrt(srcdest[i]);} return vecNoErr; }

/* #define vectorSquare_f64_I(srcdest, length)                                 ippsSqr_64f_I(srcdest, length) */
inline vecStatus genericSqr_64f_I(f64 *srcdest, int length)
{ for(int i=0;i<length;i++) {srcdest[i] *= srcdest[i];} return vecNoErr; }

/* #define vectorSub_f32_I(src, srcdest, length)                               ippsSub_32f_I(src, srcdest, length) */
inline vecStatus genericSub_32f_I(const f32 *src, f32 *srcdest, int length)
{ for(int i=0;i<length;i++) {srcdest[i] -= src[i];} return vecNoErr; }

/* #define vectorSub_s32(src1, src2, dest, length)                             ippsSub_32s_Sfs(src1, src2, dest, length, 0) */
inline vecStatus genericSub_32s(const s32 *src1,const s32 *src2, s32 *dest, int length)
{ for(int i=0;i<length;i++) {dest[i] = src1[i]-src2[i];} return vecNoErr; }
/* #define vectorSub_cf32_I(src, srcdest, length)                              ippsSub_32fc_I(src, srcdest, length) */
inline vecStatus genericSub_32fc_I(const cf32 *src, cf32 *srcdest, int length)
{ for(int i=0;i<length;i++) {srcdest[i].re -= src[i].re;srcdest[i].im -= src[i].im;} return vecNoErr; }

/* #define vectorSum_cf32(src, length, sum, hint)                              ippsSum_32fc(src, length, sum, hint) */
inline vecStatus genericSum_32fc(const cf32 *src, int length, cf32 *sum)
{ sum[0].re=0;sum[0].im=0; for(int i=0;i<length;i++) {sum[0].re += src[i].re;sum[0].im += src[i].im;} return vecNoErr; }
inline vecStatus genericSum_32f(const f32 *src, int length, f32 *sum)
{ sum[0]=0; for(int i=0;i<length;i++) {sum[0] += src[i];} return vecNoErr; }

// Bpass
/* #define vectorGenerateFIRLowpass_f64(freq, taps, length, window, normalise) ippsFIRGenLowpass_64f(freq, taps, length, window, normalise) */
//http://software.intel.com/sites/products/documentation/hpc/compilerpro/en-us/cpp/lin/ipp/ipps/ipps_ch6/functn_FIRGenLowpass.html
// I believe it is NEVER USED? 
// inline vecStatus genericNULLFIRGen(f64 freq, f64 *taps, int length, vecStatus window, vecStatus normalise) {return vecNoErr;} 

// FFTs
inline vecStatus genericInitFFTR_f32(GenFFTPtrRf32 **fftspec, int order, int flag, vecHintAlg hint) 
{ fftspec[0] = (GenFFTPtrRf32 *) malloc(sizeof(GenFFTPtrRf32)); 
  fftspec[0]->len = 2<<order;fftspec[0]->len2 = 1;fftspec[0]->len3 = 1;
  fftspec[0]->in = (f32 *) fftwf_malloc(fftspec[0]->len*sizeof(f32)); 
  fftspec[0]->out = (cf32 *) fftwf_malloc((fftspec[0]->len/2+1)*sizeof(cf32)); 
  fftspec[0]->p = fftwf_plan_dft_r2c_1d(fftspec[0]->len,fftspec[0]->in, (fftwf_complex *) fftspec[0]->out, FFTW_ESTIMATE); 
  return vecNoErr;} // Always FORWARD

inline vecStatus genericInitFFTCR_f32(GenFFTPtrCRf32 **fftspec, int order, int flag, vecHintAlg hint) 
{ fftspec[0] = (GenFFTPtrCRf32 *) malloc(sizeof(GenFFTPtrCRf32)); 
  fftspec[0]->len = 2<<order;fftspec[0]->len2 = 1;fftspec[0]->len3 = 1;
  fftspec[0]->out = (f32 *) fftwf_malloc(fftspec[0]->len*sizeof(f32)); 
  fftspec[0]->in = (cf32 *) fftwf_malloc((fftspec[0]->len/2+1)*sizeof(cf32)); 
  fftspec[0]->p = fftwf_plan_dft_c2r_1d(fftspec[0]->len,(fftwf_complex *) fftspec[0]->in, fftspec[0]->out, FFTW_ESTIMATE); 
  return vecNoErr;} // Always BACKWARDS

inline vecStatus genericInitFFTC_cf32(GenFFTPtrCfc32 **fftspec, int order, int flag, vecHintAlg hint)
{ fftspec[0] = (GenFFTPtrCfc32 *) malloc(sizeof(GenFFTPtrCfc32)); 
  fftspec[0]->len = 2<<order;fftspec[0]->len2 = 1;fftspec[0]->len3 = 1;
  fftspec[0]->in = (cf32 *) fftwf_malloc(fftspec[0]->len*sizeof(cf32));
  fftspec[0]->out = (cf32 *) fftwf_malloc(fftspec[0]->len*sizeof(cf32));
  fftspec[0]->p = fftwf_plan_dft_1d(fftspec[0]->len,(fftwf_complex *) fftspec[0]->in,(fftwf_complex *) fftspec[0]->out, FFTW_FORWARD, FFTW_ESTIMATE); 
  return vecNoErr;} // Always FORWARD

inline vecStatus generic2DInitFFTC_32fc(GenFFTPtrCfc32 **fftspec, int orderx, int ordery, int flag, vecHintAlg hint)
{ int ox=2<<orderx; int oy=2<<ordery; 
  fftspec[0] = (GenFFTPtrCfc32 *) malloc(sizeof(GenFFTPtrCfc32)); 
  fftspec[0]->len = ox; fftspec[0]->len2 = oy; fftspec[0]->len3 = 1;
  fftspec[0]->in = (cf32 *) fftwf_malloc(fftspec[0]->len*sizeof(cf32));
  fftspec[0]->out = (cf32 *) fftwf_malloc(fftspec[0]->len*sizeof(cf32));
  fftspec[0]->p = fftwf_plan_dft_2d(ox,oy,(fftwf_complex *) fftspec[0]->in,(fftwf_complex *) fftspec[0]->out, FFTW_FORWARD, FFTW_ESTIMATE); 
  return vecNoErr;} // Always FORWARD

inline vecStatus genericMove_32fc(cf32 *src,cf32 *dest, int len) 
{ memmove(dest,src,len*sizeof(dest[0])); return vecNoErr;}

inline vecStatus genericMove_32f(f32 *src,f32 *dest, int len) 
{ memmove(dest,src,len*sizeof(dest[0])); return vecNoErr;}
//{ for (int i=(len-1);i<=0;i--) dest[i]=src[i]; return vecNoErr;} // Does not guarentee not over writing

inline vecStatus genericStdDev_32f(f32 *src, int len, f32 *stddev, vecHintAlg hint) 
{ f32 sum; genericSum_32f(src,len,&sum); sum /= len; if (!len) sum=0; stddev[0]=0;
  for(int i=0;i<len;i++) {stddev[0] += (src[i]-sum)*(src[i]-sum);} 
  if (len>1) stddev[0]=sqrt(stddev[0])/(len-1);
  return vecNoErr; }

inline vecStatus genericAbs_32f_I(f32 *srcdest, int len) 
{ for(int i=0;i<len;i++) {srcdest[i]= (srcdest[i]>0)? srcdest[i]:-srcdest[i];}  return vecNoErr; }

inline vecStatus genericMaxIndx_32f(f32 *src, int len, f32 *max, int *imax) 
{ imax[0]=-1;max[0]=-1E30;
  for(int i=0;i<len;i++) {if (src[i]>max[0]) { max[0]=src[i];imax[0]=i;}}
  return vecNoErr; }

//inline vecStatus genericZero_32s(s32 * dest, int length) { for(int i=0;i<length;i++) dest[i] = 0; return vecNoErr; }

#define vectorAdd_f32_I(src, srcdest, length)                               genericAdd_32f_I(src, srcdest, length)
#define vectorAdd_f64_I(src, srcdest, length)                               genericAdd_64f_I(src, srcdest, length)
#define vectorAdd_s16_I(src, srcdest, length)                               genericAdd_16s_I(src, srcdest, length)
#define vectorAdd_s32_I(src, srcdest, length)                               genericAdd_32s_ISfs(src, srcdest, length, 0)
#define vectorAdd_cf32_I(src, srcdest, length)                              genericAdd_32fc_I(src, srcdest, length)
#define vectorAdd_cf64_I(src, srcdest, length)                              genericAdd_64fc_I(src, srcdest, length)
#define vectorAddC_f64(src, val, dest, length)                              genericAddC_64f(src, val, dest, length)
#define vectorAddC_f32(src, val, dest, length)                              genericAddC_32f(src, val, dest, length)
#define vectorAddC_f32_I(val, srcdest, length)                              genericAddC_32f_I(val, srcdest, length)
#define vectorAddC_s16_I(val, srcdest, length)                              genericAddC_16s_I(val, srcdest, length)
#define vectorAddC_f64_I(val, srcdest, length)                              genericAddC_64f_I(val, srcdest, length)

#define vectorAddProduct_cf32(src1, src2, accumulator, length)              genericAddProduct_32fc(src1, src2, accumulator, length)

#define vectorConj_cf32(src, dest, length)                                  genericConj_32fc(src, dest, length)
#define vectorConjFlip_cf32(src, dest, length)                              genericConjFlip_32fc(src, dest, length)

//and finally other vector routines
#define vectorCopy_u8(src, dest, length)        genericCopy_u8(src, dest, length)
#define vectorCopy_s16(src, dest, length)       genericCopy_s16(src, dest, length)
#define vectorCopy_s32(src, dest, length)       genericCopy_s32(src, dest, length)
#define vectorCopy_f32(src, dest, length)       genericCopy_f32(src, dest, length)
#define vectorCopy_cf32(src, dest, length)       genericCopy_cf32(src, dest, length)
#define vectorCopy_f64(src, dest, length)       genericCopy_f64(src, dest, length)

#define vectorCos_f32(src, dest, length)                                    genericCos_32f(src, dest, length)

#define vectorConvertScaled_s16f32(src, dest, length, scalefactor)          genericConvert_16s32f(src, dest, length, scalefactor)
#define vectorConvertScaled_f32s16(src, dest, length, rndmode, scalefactor) genericConvert_32f16s(src, dest, length, rndmode, scalefactor)
#define vectorConvertScaled_f32u8(src, dest, length, rndmode, scalefactor)  genericConvert_32f8u(src, dest, length, rndmode, scalefactor)
#define vectorConvert_f32s32(src, dest, length, rndmode)                    genericConvert_32f32s(src, dest, length, rndmode, 0)
#define vectorConvert_s16f32(src, dest, length)                             genericConvert_16s32f(src, dest, length)
#define vectorConvert_s32f32(src, dest, length)                             genericConvert_32s32f(src, dest, length)
#define vectorConvert_f64f32(src, dest, length)                             genericConvert_64f32f(src, dest, length)

#define vectorDivide_f32(src1, src2, dest, length)                          genericDivide_32f(src1, src2, dest, length)

#define vectorDotProduct_f64(src1, src2, length, output)                    genericDotProduct_64f(src1, src2, length, output)

#define vectorFlip_f64_I(srcdest, length)                                   genericFlip_64f_I(srcdest, length)

#define vectorMagnitude_cf32(src, dest, length)                             genericMagnitude_32fc(src, dest, length)
#define vectorMax_f32(src, dest, length)                                    genericMax_32f(src, dest, length)
#define vectorMin_f32(src, dest, length)                                    genericMin_32f(src, dest, length)

#define vectorMean_cf32(src, length, mean, hint)                            genericMean_32fc(src, length, mean, hint)

#define vectorMul_f32(src1, src2, dest, length)                             genericMul_32f(src1, src2, dest, length)
#define vectorMul_f32_I(src, srcdest, length)                               genericMul_32f_I(src, srcdest, length)
#define vectorMul_cf32_I(src, srcdest, length)                              genericMul_32fc_I(src, srcdest, length)
#define vectorMul_cf32(src1, src2, dest, length)                            genericMul_32fc(src1, src2, dest, length)
#define vectorMul_f32cf32(src1, src2, dest, length)                         genericMul_32f32fc(src1, src2, dest, length)
#define vectorMulC_f32(src, val, dest, length)                              genericMulC_32f(src, val, dest, length)
#define vectorMulC_cs16_I(val, srcdest, length)                             genericMulC_16sc_I(val, srcdest, length)
#define vectorMulC_f32_I(val, srcdest, length)                              genericMulC_32f_I(val, srcdest, length)
#define vectorMulC_cf32_I(val, srcdest, length)                             genericMulC_32fc_I(val, srcdest, length)
#define vectorMulC_cf32(src, val, dest, length)                             genericMulC_32fc(src, val, dest, length)
#define vectorMulC_f64_I(val, srcdest, length)                              genericMulC_64f_I(val, srcdest, length)
#define vectorMulC_f64(src, val, dest, length)                              genericMulC_64f(src, val, dest, length)

#define vectorPhase_cf32(src, dest, length)                                 genericPhase_32fc(src, dest, length)

#define vectorRealToComplex_f32(real, imag, complex, length)                genericRealToCplx_32f(real, imag, complex, length)

#define vectorReal_cf32(complex, real, length)                              genericReal_32fc(complex, real, length)

#define vectorSet_f32(val, dest, length)                                    genericSet_32f(val, dest, length)

#define vectorSin_f32(src, dest, length)                                    genericSin_32f(src, dest, length)

#define vectorSinCos_f32(src, sin, cos, length)                             genericSinCos_32f(src, sin, cos, length)

#define vectorSplitScaled_s16f32(src, dest, numchannels, chanlen)           genericSplitScaled_16s32f(src, dest, numchannels, chanlen)

#define vectorSquare_f32_I(srcdest, length)                                 genericSqr_32f_I(srcdest, length)
#define vectorSquare_f64_I(srcdest, length)                                 genericSqr_64f_I(srcdest, length)

#define vectorSub_f32_I(src, srcdest, length)                               genericSub_32f_I(src, srcdest, length)
#define vectorSub_s32(src1, src2, dest, length)                             genericSub_32s(src1, src2, dest, length)
#define vectorSub_cf32_I(src, srcdest, length)                              genericSub_32fc_I(src, srcdest, length)

#define vectorSum_cf32(src, length, sum, hint)                              genericSum_32fc(src, length, sum, hint)

#define vectorZero_cf32(dest, length)                                       genericZero_32fc(dest, length)
#define vectorZero_cf64(dest, length)                                       genericZero_64fc(dest, length)
#define vectorZero_f32(dest, length)                                        genericZero_32f(dest, length)
#define vectorZero_s16(dest, length)                                        genericZero_16s(dest, length)
#define vectorZero_s32(dest, length)                                        genericZero_32s(dest, length)

//Get Error string 
#define vectorGetStatusString(code)                                         "Error in Generic Functions"

// FFT Calls

#define vectorInitFFTR_f32(fftspec, order, flag, hint)    genericInitFFTR_f32(fftspec, order, flag, hint)
#define vectorInitFFTCR_f32(fftspec, order, flag, hint)   genericInitFFTCR_f32(fftspec, order, flag, hint)
#define vectorInitFFTC_cf32(fftspec, order, flag, hint)   genericInitFFTC_cf32(fftspec, order, flag, hint)
#define vectorInitDFTC_cf32(fftspec, order, flag, hint)   vectorInitFFTC_cf32(fftspec, order, flag, hint)
#define vectorInitDFTR_f32(fftspec, order, flag, hint)    vectorInitFFTR_f32(fftspec, order, flag, hint)
#define vectorInitDFTCR_f32(fftspec, order, flag, hint)   vectorInitFFTCR_f32(fftspec, order, flag, hint)
// I believe it is NEVER USED?
/* #define vectorInitFFTC_f32(fftspec, order, flag, hint)                      ippsFFTInitAlloc_C_32f(fftspec, order, flag, hint) */
/* //#define vectorInitFFT_f32(fftspec, order, flag, hint)                       ippsDFTInitAlloc_R_32f(fftspec, order, flag, hint) */
/* #define vectorInitFFT_s16(fftspec, order, flag, hint)                       ippsFFTInitAlloc_R_16s(fftspec, order, flag, hint) */


inline vecStatus genGetFFTBufSizeR_f32(GenFFTPtrRf32* fftspec, int *buffersize)  { buffersize[0] = fftspec->len; return vecNoErr; }
inline vecStatus genGetFFTBufSizeC_cf32(GenFFTPtrCfc32* fftspec, int *buffersize)  { buffersize[0] = fftspec->len; return vecNoErr; }

#define vectorGetFFTBufSizeR_f32(fftspec, buffersize)  genGetFFTBufSizeR_f32(fftspec, buffersize)
#define vectorGetFFTBufSizeC_cf32(fftspec, buffersize) genGetFFTBufSizeC_cf32(fftspec, buffersize)
#define vectorGetDFTBufSizeC_cf32(fftspec, buffersize) genGetFFTBufSizeC_cf32(fftspec, buffersize)
#define vectorGetDFTBufSizeR_f32(fftspec, buffersize)  genGetFFTBufSizeR_f32(fftspec, buffersize)
// I believe it is NEVER USED?
/* #define vectorGetFFTBufSizeC_f32(fftspec, buffersize)                       ippsFFTGetBufSize_C_32f(fftspec, buffersize) */
/* //#define vectorGetFFTBufSize_f32(fftspec, buffersize)                        ippsDFTGetBufSize_R_32f(fftspec, buffersize) */
/* #define vectorGetFFTBufSize_s16(fftspec, buffersize)                        ippsFFTGetBufSize_R_16s(fftspec, buffersize) */

inline vecStatus genFreeFFTR_f32(GenFFTPtrRf32* fftspec) { fftwf_free(fftspec->in);fftwf_free(fftspec->out);fftwf_destroy_plan(fftspec->p);free(fftspec);return vecNoErr; }
inline vecStatus genFreeFFTC_cf32(GenFFTPtrCfc32* fftspec) { fftwf_free(fftspec->in);fftwf_free(fftspec->out);fftwf_destroy_plan(fftspec->p);free(fftspec);return vecNoErr; }
inline vecStatus genFreeFFTCR_f32(GenFFTPtrCRf32* fftspec) { fftwf_free(fftspec->in);fftwf_free(fftspec->out);fftwf_destroy_plan(fftspec->p);free(fftspec);return vecNoErr; }

#define vectorFreeFFTR_f32(fftspec)    genFreeFFTR_f32(fftspec)
#define vectorFreeFFTC_cf32(fftspec)   genFreeFFTC_cf32(fftspec)
#define vectorFreeDFTC_cf32(fftspec)   genFreeFFTC_cf32(fftspec)
#define vectorFreeDFTR_f32(fftspec)    genFreeFFTR_f32(fftspec)
#define vectorFreeFFTCR_f32(fftspec)   genFreeFFTCR_f32(fftspec)
#define vectorFreeDFTCR_f32(fftspec)   genFreeFFTCR_f32(fftspec)
// I believe it is NEVER USED?
/* #define vectorFreeFFTC_f32(fftspec)                                         ippsFFTFree_C_32f(fftspec) */

// Should use memmove? Is the (len/2+1) copy working as expected? 
inline vecStatus genFFT_RtoC_f32(const f32* src,f32* dest, GenFFTPtrRf32*fftspec,u8 * fftbuffer)
   { memcpy(fftspec->in, src, fftspec->len*sizeof(f32)); 
     fftwf_execute(fftspec->p); 
     memcpy(dest, fftspec->out, (fftspec->len/2+1)*sizeof(cf32)); 
     return vecNoErr; }
inline vecStatus genFFT_CtoC_cf32(const cf32 *src, cf32 *dest, GenFFTPtrCfc32* fftspec, u8 *fftbuffer)  
   { memcpy(fftspec->in, src, fftspec->len*sizeof(cf32)); 
     fftwf_execute(fftspec->p);
     memcpy(dest, fftspec->out, fftspec->len*sizeof(cf32));
     return vecNoErr; }
inline vecStatus genFFT_CtoR_f32(const f32* src, f32* dest, GenFFTPtrRf32*fftspec,u8 * fftbuffer)
   { memcpy(fftspec->in, src, (fftspec->len/2+1)*sizeof(cf32)); 
     fftwf_execute(fftspec->p); 
     memcpy(dest, fftspec->out, fftspec->len*sizeof(f32)); 
     return vecNoErr; }
inline vecStatus gen2DFFT_CtoC_32fc(const cf32 *src, int sstp, cf32 *dest, int dstp, GenFFTPtrCfc32* fftspec, u8 *fftbuffer)
   { memcpy(fftspec->in, src, fftspec->len2*fftspec->len*sizeof(cf32)); 
     // Does the plan know the step size ahead of time ??? 
     fftwf_execute(fftspec->p);
     memcpy(dest, fftspec->out, fftspec->len2*fftspec->len*sizeof(cf32));
     return vecNoErr; }

#define vectorFFT_RtoC_f32(src, dest, fftspec, fftbuffer)   genFFT_RtoC_f32(src, dest, fftspec, fftbuffer)
#define vectorFFT_CtoC_cf32(src, dest, fftspec, fftbuffer)  genFFT_CtoC_cf32(src, dest, fftspec, fftbuffer)
#define vectorFFT_CtoR_f32(src, dest, fftspec, fftbuffer)     genFFT_CtoR_f32(src, dest, fftspec, fftbuffer)
#define vectorDFT_CtoC_cf32(src, dest, fftspec, fftbuffer)  vectorFFT_CtoC_cf32(src, dest, fftspec, fftbuffer)
#define vectorDFT_RtoC_f32(src, dest, fftspec, fftbuffer)   vectorFFT_RtoC_f32(src, dest, fftspec, fftbuffer)
#define vectorDFT_CtoR_f32(src, dest, fftspec, fftbuffer)     vectorFFT_CtoR_f32(src, dest, fftspec, fftbuffer)

#define vectorMove_cf32(src, dest, length)                 genericMove_32fc(src, dest, length)
#define vectorMove_f32(src, dest, length)                  genericMove_32f(src, dest, length)
#define vectorStdDev_f32(src, length, stddev, hint)         genericStdDev_32f(src, length, stddev, hint)
#define vectorAbs_f32_I(srcdest, length)                   genericAbs_32f_I(srcdest, length)
#define vectorMaxIndx_f32(srcdest, length, max, imax)      genericMaxIndx_32f(srcdest,length, max, imax)

// For difx_monitor // Note 2D
#define vector2DFFT_CtoC_cf32(src, sstp, dest, dstp, fftspec, fftbuff)       gen2DFFT_CToC_32fc(src, sstp, dest, dstp, fftspec, fftbuff)
#define vector2DFreeFFTC_cf32(fftspec)                                       genFreeFFTC_32fc(fftspec)
#define vector2DInitFFTC_cf32(fftspec, orderx, ordery, flag, hint)                 gen2DInitFFTC_32fc(fftspec, orderx, ordery, flag, hint)
#define vectorDivC_cf32_I(val, srcdest, length)                             genericDivC_32fc_I(val, srcdest, length)
#define vectorDivC_f32_I(val, srcdest, length)                              genericDivC_32f_I(val, srcdest, length)
#define vectorSqrt_f32_I(srcdest, len)                                      genericSqrt_32f_I(srcdest, len)
#define vectorFFTInv_CCSToR_32f(src, dest,fftspec,buff)    vectorFFT_CtoR_f32(src, dest,fftspec,buff)

#endif /* Generic Architecture */

#endif /* Defined architecture header */
// vim: shiftwidth=2:softtabstop=2:expandtab
