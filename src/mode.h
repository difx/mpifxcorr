/***************************************************************************
 *   Copyright (C) 2006 by Adam Deller                                     *
 *                                                                         *
 *   This program is free for non-commercial use: see the license file     *
 *   at http://astronomy.swin.edu.au:~adeller/software/difx/ for more      *
 *   details.                                                              *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                  *
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
#ifndef MODE_H
#define MODE_H

#include "architecture.h"
#include "configuration.h"
#include <iostream>
#include <fstream>
#include <cstdlib>

using namespace std;

/**
@class Mode 
@brief Abstract superclass for all modes.  Provides station-based processing functionality

Possesses all methods necessary for mode functionality but should not be instantiated as it does not build a
lookup table or handle unpacking - this is particular to each subclass of mode.  Station-based processing
(FFT, fringe rotation, fraction sample correction etc) is handled via the process method, based on
the provided data and control (delay) arrays

@author Adam Deller
*/
class Mode{
public:
 /**
  * Constructor: allocates memory, extracts stream information and calculates number of lookups etc
  * @param conf The configuration object, containing all information about the duration and setup of this correlation
  * @param confindex The index of the configuration this Mode is for
  * @param dsindex The index of the datastream this Mode is for
  * @param recordedbandchan The number of channels for each recorded subband
  * @param bpersend The number of FFT blocks to be processed in a message
  * @param gsamples The number of additional guard samples at the end of a message
  * @param nrecordedfreqs The number of recorded frequencies for this Mode
  * @param recordedbw The bandwidth of each of these IFs
  * @param recordedfreqclkoffs The time offsets in microseconds to be applied post-F for each of the frequencies
  * @param recordedfreqlooffs The LO offsets in Hz for each recorded frequency
  * @param nrecordedbands The total number of subbands recorded
  * @param nzoombands The number of subbands to be taken from withing the recorded bands - can be zero
  * @param nbits The number of bits per sample
  * @param unpacksamp The number of samples to unpack in one hit
  * @param fbank Whether to use a polyphase filterbank to channelise (instead of FFT)
  * @param fringerotorder The interpolation order across an FFT (Oth, 1st or 2nd order; 0th = post-F)
  * @param arraystridelen The number of samples to stride when doing complex multiplies to implement sin/cos operations efficiently
  * @param cacorrs Whether cross-polarisation autocorrelations are to be calculated
  * @param bclock The recorder clock-out frequency in MHz ("block clock")
  */
  Mode(Configuration * conf, int confindex, int dsindex, int recordedbandchan, int bpersend, int gsamples, int nrecordedfreqs, double recordedbw, double * recordedfreqclkoffs, double * recordedfreqlooffs, int nrecordedbands, int nzoombands, int nbits, int unpacksamp, bool fbank, int fringerotorder, int arraystridelen, bool cacorrs, double bclock);

 /**
  * Stores the delay information for the current block series
  * @param d The array of delays at the start of each FFT chunk
  */
  void setDelays(f64 * d);

 /**
  * Stores the raw data for the current block series
  * @param d The data array
  * @param dbytes The number of bytes in the data array
  * @param btime The time of the first sample in the data array
  */
  void setData(u8 * d, int dbytes, double btime);

 /**
  * Calculates fringe rotation and fractional sample correction arrays and FFTs, and autocorrelates
  * @param index The index of the FFT chunk to process
  * @return Fraction of samples that were good for this FFT
  */
  float process(int index);

 /**
  * Sets the autocorrelation arrays to contain 0's
  */
  void zeroAutocorrelations();

 /**
  * Stores the times for the first FFT chunk to be processed
  * @param seconds The offset in seconds from the start of the correlation
  * @param ns The offset in nanoseconds from the integer second
  */
  inline void setOffsets(int seconds, int ns) { offsetseconds = seconds; offsetns = ns; }

 /**
  * Grabs the pointer to an autocorrelation array
  * @param crosspol Whether to return the crosspolarisation autocorrelation for this band
  * @param outputband The band index
  */
  inline cf32* getAutocorrelation(bool crosspol, int outputband) { return autocorrelations[(crosspol)?1:0][outputband]; }

 /**
  * Gets the expected decorrelation ("van Vleck correction" ) for a given number of bits.
  * All cases other than 1 and 2 are approximate only!!!
  * @param nbits The number of bits
  * @return The square root of the decorrelation correction
  */
  static inline float getDecorrelationPercentage(int nbits) { return decorrelationpercentage[nbits-1]; }

 /**
  * @return Whether this Mode is writing cross-polarisation auto-corelations
  */
  inline bool writeCrossAutoCorrs() { return calccrosspolautocorrs; }

 /**
  * @return Whether this Mode was initialied ok
  */
  inline bool initialisedOK() { return initok; }

 /**
  * Returns a pointer to the FFT'd data of the specified product
  * @param outputband The band to get
  * @return Pointer to the FFT'd data (complex 32 bit float)
  */
  inline cf32* getFreqs(int outputband) { return fftoutputs[outputband]; };

 /**
  * Returns a pointer to the FFT'd and conjugated data of the specified product
  * @param outputband The band to get
  * @return Pointer to the conjugate of the FFT'd data (complex 32 bit float)
  */
  inline cf32* getConjugatedFreqs(int outputband) { return conjfftoutputs[outputband]; }

  virtual ~Mode();

  /** Constant for comparing two floats for equality (for freqs and bandwidths etc) */
  static const float TINY;

protected:
 /** 
  * Unpacks quantised data to float arrays.  The floating point values filled should
  * be in the range 0.0->1.0, and set appropriately to the expected input levels such that
  * the mean autocorrelation level at nominal sampler statistics is 0.??
  * @param sampleoffset The offset in number of time samples into the data array
  * @return The number of good samples unpacked scaled by the number of samples asked to unpack
  *         ie a weight in the range 0.0 to 1.0
  */
  virtual float unpack(int sampleoffset);
  
  Configuration * config;
  int configindex, datastreamindex, recordedbandchannels, blockspersend, guardsamples, twicerecordedbandchannels, numrecordedfreqs, numrecordedbands, numzoombands, numbits, bytesperblocknumerator, bytesperblockdenominator, offsetseconds, offsetns, order, flag, fftbuffersize, unpacksamples, bufferseconds, unpackstartsamples, datalengthbytes;
  int fringerotationorder, arraystridelength, numstrides;
  double recordedbandwidth, blockclock, sampletime; //MHz, microseconds
  double a0, b0, c0, a, b, c, quadadd1, quadadd2;
  double buffermicroseconds, fftstartmicrosec, fftdurationmicrosec;
  f32 dataweight;
  int samplesperblock, samplesperlookup, numlookups, delaylength, autocorrwidth;
  bool filterbank, calccrosspolautocorrs, fractionalLoFreq, initok;
  double * recordedfreqclockoffsets;
  double * recordedfreqlooffsets;
  u8 * data;
  s16 * lookup;
  s16 * linearunpacked;
  f32** unpackedarrays;
  cf32** fftoutputs;
  cf32** conjfftoutputs;
  //f32* fracmult;
  //f32* fracmultsin;
  //f32* fracmultcos;
  //f32* channelfreqs;
  //f32* lsbchannelfreqs;
  //cf32* complexfracmult;
  f64 * delays;
  cf32*** autocorrelations;
  vecFFTSpecR_f32 * pFFTSpecR;
  vecFFTSpecC_cf32 * pFFTSpecC;
  u8 * fftbuffer;
  vecHintAlg hint;

  //new arrays for strided complex multiply for fringe rotation and fractional sample correction
  cf32 * complexrotator;
  cf32 * complexunpacked;
  cf32 * fracsamprotator;
  cf32 * fftd;

  f64 * subxoff;
  f64 * subxval;
  f64 * subphase;
  f32 * subarg;
  f32 * subsin;
  f32 * subcos;

  f64 * stepxoff;
  f64 * stepxval;
  f64 * stepphase;
  f32 * steparg;
  f32 * stepsin;
  f32 * stepcos;
  cf32 * stepcplx;

  f32 * subchannelfreqs;
  f32 * lsbsubchannelfreqs;
  f32 * subfracsamparg;
  f32 * subfracsampsin;
  f32 * subfracsampcos;

  f32 * stepchannelfreqs;
  f32 * lsbstepchannelfreqs;
  f32 * stepfracsamparg;
  f32 * stepfracsampsin;
  f32 * stepfracsampcos;
  cf32 * stepfracsampcplx;

  //extras necessary for quadratic (order == 2)
  cf32 * piecewiserotator;
  cf32 * quadpiecerotator;

  f64 * subquadxval;
  f64 * subquadphase;
  f32 * subquadarg;
  f32 * subquadsin;
  f32 * subquadcos;

  f64 * stepxoffsquared;
  f64 * tempstepxval;

private:
  ///Array containing decorrelation percentages for a given number of bits
  static const float decorrelationpercentage[];
};

/** 
 @class LBAMode 
 @brief A mode for 'standard' LBA 2 bit data

 Assumes data has been compressed if running at 128 Mbps or lower ie no redundancy.  Assumes running on a LITTLE-ENDIAN MACHINE!!!
 @author Adam Deller
 */
class LBAMode : public Mode{
public:
 /**
  * Constructor: calls Mode constructor then creates lookup table
  * @param conf The configuration object, containing all information about the duration and setup of this correlation
  * @param confindex The index of the configuration this Mode is for
  * @param dsindex The index of the datastream this Mode is for
  * @param nchan The number of channels per subband
  * @param bpersend The number of FFT blocks to be processed in a message
  * @param gblocks The number of additional guard blocks at the end of a message
  * @param nfreqs The number of frequencies for this Mode
  * @param bw The bandwidth of each of these IFs
  * @param recordedfreqclkoffs The time offsets in microseconds to be applied post-F for each of the frequencies
  * @param recordedfreqlooffs The LO offsets in Hz for each recorded frequency
  * @param ninputbands The total number of subbands recorded
  * @param noutputbands The total number of subbands after prefiltering - not currently used (must be = numinputbands)
  * @param nbits The number of bits per sample
  * @param fbank Whether to use a polyphase filterbank to channelise (instead of FFT)
  * @param fringerotorder The interpolation order across an FFT (Oth, 1st or 2nd order; 0th = post-F)
  * @param arraystridelen The number of samples to stride when doing complex multiplies to implement sin/cos operations efficiently
  * @param cacorrs Whether cross-polarisation autocorrelations are to be calculated
  * @param unpackvalues 4 element array containing floating point unpack values for the four possible two bit values
  */
    LBAMode(Configuration * conf, int confindex, int dsindex, int nchan, int bpersend, int gblocks, int nfreqs, double bw, double * recordedfreqclkoffs, double * recordedfreqlooffs, int ninputbands, int noutputbands, int nbits, bool fbank, int fringerotorder, int arraystridelen, bool cacorrs, const s16* unpackvalues);

    ///unpack mapping for "standard" recording modes
    static const s16 stdunpackvalues[];
    ///unpack mapping for "vsop-style" recording modes
    static const s16 vsopunpackvalues[];
};

#endif
