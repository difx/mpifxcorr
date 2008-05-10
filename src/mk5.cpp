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
#include <mpi.h>
#include "mk5.h"


static int genFormatName(Configuration::dataformat format, int nchan, double bw, int nbits, int framebytes, int oversamplefactor char *formatname)
{
  int fanout=1, mbps;

  mbps = int(2*nchan*bw*nbits + 0.5);

  switch(format)
  {
    case Configuration::MKIV:
      fanout = framebytes*8/(20000*nbits*nchan);
      if(fanout*20000*nbits*nchan != framebytes*8)
      {
        cerr << "genFormatName : MKIV format : framebytes = " << framebytes << " is not allowed\n";
        exit(1);
      }
      sprintf(formatname, "MKIV%d_%d-%d-%d-%d", oversamplefactor, fanout, mbps, nchan, nbits);
      break;
    case Configuration::VLBA:
      fanout = framebytes*8/(20160*nbits*nchan);
      if(fanout*20160*nbits*nchan != framebytes*8)
      {
        cerr << "genFormatName : VLBA format : framebytes = " << framebytes << " is not allowed\n";
        exit(1);
      }
      sprintf(formatname, "VLBA%d_%d-%d-%d-%d", oversamplefactor, fanout, mbps, nchan, nbits);
      break;
    case Configuration::MARK5B:
      sprintf(formatname, "Mark5B-%d-%d-%d", mbps, nchan, nbits);
      break;
    default:
      cerr << "genFormatName : unsupported format encountered\n" << endl;
      exit(1);
  }

  return fanout;
}

/// Mk5DataMode ---------------------------------------------------------


Mk5Mode::Mk5Mode(Configuration * conf, int confindex, int dsindex, int nchan, int bpersend, int gblocks, int nfreqs, double bw, double * freqclkoffsets, int ninputbands, int noutputbands, int nbits, bool fbank, bool postffringe, bool quaddelayinterp, bool cacorrs, int framebytes, int framesamples, Configuration::dataformat format)
 : Mode(conf, confindex, dsindex, nchan, bpersend, gblocks, nfreqs, bw, freqclkoffsets, ninputbands, noutputbands, nbits, nchan*2+4, fbank, postffringe, quaddelayinterp, cacorrs, bw*2)
{
  char formatname[64];

  fanout = genFormatName(format, ninputbands, bw, nbits, framebytes, conf->oversamplefactor, formatname);

  // since we allocated the max amount of space needed above, we need to change
  // this to the number actually needed.
  unpacksamples = nchan*2;

  samplestounpack = nchan*2;
  if(fanout > 1)
    samplestounpack += fanout;

  //create the mark5_stream used for unpacking
  mark5stream = new_mark5_stream(
      new_mark5_stream_unpacker(0),
      new_mark5_format_generic_from_string(formatname) );

  if(mark5stream == 0)
  {
    cerr << "Mk5Mode::Mk5Mode : mark5stream is null " << endl;
  }

  if(framesamples != mark5stream->framesamples)
  {
    cerr << "Mk5Mode::Mk5Mode : framesamples inconsistent (" << framesamples 
	 << "/" << mark5stream->framesamples << ")" << endl;
    exit(1);
  }

}

Mk5Mode::~Mk5Mode()
{
  delete_mark5_stream(mark5stream);
}

float Mk5Mode::unpack(int sampleoffset)
{
  int framesin, goodsamples;

  // FIXME -- I think we can use mark5stream->samplegranularity instead of fanout below and fewer samples will be lost in those rare cases.  --WFB

  //work out where to start from
  framesin = (sampleoffset/framesamples);
  unpackstartsamples = sampleoffset - (sampleoffset % fanout);

  //unpack one frame plus one FFT size worth of samples
  goodsamples = mark5_unpack_with_offset(mark5stream, data, unpackstartsamples, unpackedarrays, samplestounpack);
  if(fanout > 1)
  {
    for(int i = 0; i < sampleoffset % fanout; i++)
      if(unpackedarrays[0][i] != 0.0)
        goodsamples--;
    for(int i = unpacksamples + sampleoffset % fanout; i < samplestounpack; i++)
      if(unpackedarrays[0][i] != 0.0)
        goodsamples--;
  }
    
  if(goodsamples < 0)
  {
    cerr << "Error trying to unpack Mark5 format data at sampleoffset " << sampleoffset << " from buffer seconds " << bufferseconds << " plus " << buffermicroseconds << " microseconds!!!" << endl;
    goodsamples = 0;
  }

  return goodsamples/(float)unpacksamples;
}



/// Mk5DataStream -------------------------------------------------------


Mk5DataStream::Mk5DataStream(Configuration * conf, int snum, int id, int ncores, int * cids, int bufferfactor, int numsegments)
 : DataStream(conf, snum, id, ncores, cids, bufferfactor, numsegments)
{
  //each data buffer segment contains an integer number of frames, because thats the way config determines max bytes
}

Mk5DataStream::~Mk5DataStream()
{}

int Mk5DataStream::calculateControlParams(int offsetsec, int offsetsamples)
{
  int bufferindex, framesin, vlbaoffset;
  
  bufferindex = DataStream::calculateControlParams(offsetsec, offsetsamples);

  if(bufferinfo[atsegment].controlbuffer[bufferinfo[atsegment].numsent][0] < 0.0)
    return 0;

  //do the necessary correction to start from a frame boundary - work out the offset from the start of this segment
  vlbaoffset = bufferindex - atsegment*readbytes;

  if(vlbaoffset < 0)
  {
    cout << "ERROR Mk5DataStream::calculateControlParams : vlbaoffset=" << vlbaoffset << endl;
    bufferinfo[atsegment].controlbuffer[bufferinfo[atsegment].numsent][0] = -1.0;
    return 0;
  }

  // bufferindex was previously computed assuming no framing overhead
  framesin = vlbaoffset/payloadbytes;

  // Note here a time is needed, so we only count payloadbytes
  bufferinfo[atsegment].controlbuffer[bufferinfo[atsegment].numsent][0] = bufferinfo[atsegment].seconds + double(bufferinfo[atsegment].nanoseconds)*1.0e-9 + (double)framesin/framespersecond;

  //go back to nearest frame -- here the total number of bytes matters
  bufferindex = atsegment*readbytes + framesin*framebytes;
  if(bufferindex >= bufferbytes)
  {
    cout << "Mk5DataStream::calculateControlParams : bufferindex=" << bufferindex << " >= bufferbytes=" << bufferbytes << endl;
    bufferinfo[atsegment].controlbuffer[bufferinfo[atsegment].numsent][0] = -1.0;
    return 0;
  }
  return bufferindex;
}

void Mk5DataStream::updateConfig(int segmentindex)
{
  //run the default update config, then add additional information specific to Mk5
  DataStream::updateConfig(segmentindex);
  if(bufferinfo[segmentindex].configindex < 0) //If the config < 0 we can skip this scan
    return;

  framebytes = config->getFrameBytes(bufferinfo[segmentindex].configindex, streamnum);
  payloadbytes = config->getFramePayloadBytes(bufferinfo[segmentindex].configindex, streamnum);

  framespersecond = config->getFramesPerSecond(bufferinfo[segmentindex].configindex, streamnum);

  //correct the nsinc - should be number of frames*frame time
  bufferinfo[segmentindex].nsinc = int(((bufferbytes/numdatasegments)/framebytes)*(1000000000.0/double(framespersecond)) + 0.5);

  //take care of the case where an integral number of frames is not an integral number of blockspersend - ensure sendbytes is long enough

  //note below, the math should produce a pure integer, but add 0.5 to make sure that the fuzziness of floats doesn't cause an off-by-one error
  bufferinfo[segmentindex].sendbytes = int(((((double)bufferinfo[segmentindex].sendbytes)* ((double)config->getBlocksPerSend(bufferinfo[segmentindex].configindex)))/(config->getBlocksPerSend(bufferinfo[segmentindex].configindex) + config->getGuardBlocks(bufferinfo[segmentindex].configindex)) + 0.5));
}

void Mk5DataStream::initialiseFile(int configindex, int fileindex)
{
  int offset;
  char formatname[64];
  struct mark5_stream *mark5stream;
  int nbits, ninputbands, framebytes;
  Configuration::dataformat format;
  double bw;

  format = config->getDataFormat(configindex, streamnum);
  nbits = config->getDNumBits(configindex, streamnum);
  ninputbands = config->getDNumInputBands(configindex, streamnum);
  framebytes = config->getFrameBytes(configindex, streamnum);
  bw = config->getConfigBandwidth(configindex);

  genFormatName(format, ninputbands, bw, nbits, framebytes, formatname);

  mark5stream = new_mark5_stream(
    new_mark5_stream_file(datafilenames[configindex][fileindex].c_str(), 0),
    new_mark5_format_generic_from_string(formatname) );
  if(mark5stream->nchan != config->getDNumInputBands(configindex, streamnum))
  {
    cerr << "Error - number of input bands for datastream " << streamnum << " (" << ninputbands << ") does not match with MkV file " << datafilenames[configindex][fileindex] << " (" << mark5stream->nchan << "), will be ignored!!!" << endl;
  }

  // resolve any day ambiguities
  mark5_stream_fix_mjd(mark5stream, corrstartday);

  mark5_stream_print(mark5stream);

  offset = mark5stream->frameoffset;

  readseconds = 86400*(mark5stream->mjd-corrstartday) + mark5stream->sec-corrstartseconds + intclockseconds;
  readnanoseconds = int(mark5stream->ns);
  cout << "The frame start day is " << mark5stream->mjd << ", the frame start seconds is " << mark5stream->sec << ", the frame start ns is " << mark5stream->ns << ", readseconds is " << readseconds << ", readnanoseconds is " << readnanoseconds << endl;

  //close mark5stream
  delete_mark5_stream(mark5stream);

  cout << "About to seek to byte " << offset << " to get to the first frame" << endl;

  input.seekg(offset);
}
