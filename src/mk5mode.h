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
// $HeadURL: $
// $LastChangedRevision$
// $Author$
// $LastChangedDate$
//
//============================================================================
#include "mode.h"
#include "mark5access.h"

/** 
 @class Mk5Mode 
 @brief A mode for Mk4 formatted or VLBA formatted Mk5 data

 A mode for MkIV, VLBA, or Mark5B formatted Mk5 data.  All types of band setup should be supported.
 @author Adam Deller
 */
class Mk5Mode : public Mode
{
  public:
 /**
   * Constructor: calls Mode constructor then creates a mark5_format struct, using Walter Brisken's mark5access library
   * @param conf The configuration object, containing all information about the duration and setup of this correlation
   * @param confindex The index of the configuration this Mode is for
   * @param dsindex The index of the datastream this Mode is for
   * @param recordedbandchan The number of channels for each recorded subband
   * @param bpersend The number of FFT blocks to be processed in a message
   * @param gsamples The number of additional guard samples at the end of a message
   * @param nrecordedfreqs The number of recorded frequencies for this Mode
   * @param recordedbw The bandwidth of each of these IFs
   * @param recordedfreqclkoffsets The time offsets in microseconds to be applied post-F for each of the frequencies
   * @param nrecordedbands The total number of subbands recorded
   * @param nzoombands The number of subbands to be taken from withing the recorded bands - can be zero
   * @param nbits The number of bits per sample
   * @param fbank Whether to use a polyphase filterbank to channelise (instead of FFT)
   * @param postffringe Whether fringe rotation takes place after channelisation
   * @param quaddelayinterp Whether delay interpolataion from FFT start to FFT end is quadratic (if false, linear is used)
   * @param cacorrs Whether cross-polarisation autocorrelations are to be calculated
   * @param fsamples The number of samples in a frame per channel
  */
    Mk5Mode(Configuration * conf, int confindex, int dsindex, int recordedbandchan, int bpersend, int gsamples, int nrecordedfreqs, double recordedbw, double * recordedfreqclkoffsets, int nrecordedbands, int nzoombands, int nbits, bool fbank, bool postffringe, bool quaddelayinterp, bool cacorrs, int framebytes, int framesamples, Configuration::dataformat format);
    virtual ~Mk5Mode();

  protected:
 /** 
   * Uses mark5access library to unpack multiplexed, quantised data into the separate float arrays
   * @return The fraction of samples returned
   * @param sampleoffset The offset in number of time samples into the data array
  */
    virtual float unpack(int sampleoffset);

    int framesamples, framebytes, samplestounpack, fanout;
    struct mark5_stream *mark5stream;
};
