/***************************************************************************
 *   Copyright (C) 2011-2012 by Adam Deller & Walter Brisken               *
 *                                                                         *
 *   This program is free for non-commercial use: see the license file     *
 *   at http://astronomy.swin.edu.au/~adeller/software/difx/ for more      *
 *   details.                                                              *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                  *
 ***************************************************************************/
//===========================================================================
// SVN properties (DO NOT CHANGE)
//
// $Id: datamuxer.cpp 2273 2010-07-01 00:22:30Z AdamDeller $
// $HeadURL: https://svn.atnf.csiro.au/difx/mpifxcorr/trunk/src/datamuxer.cpp $
// $LastChangedRevision: 2273 $
// $Author: AdamDeller $
// $LastChangedDate: 2010-06-30 18:22:30 -0600 (Wed, 30 Jun 2010) $
//
//============================================================================
#include <cstdio>
#include <cstring>
#include "datamuxer.h"
#include "vdifio.h"
#include "alert.h"
#include "config.h"

DataMuxer::DataMuxer(const Configuration * conf, int dsindex, int id, int nthreads, int sbytes)
  : config(conf), datastreamindex(dsindex), mpiid(id), numthreads(nthreads), segmentbytes(sbytes)
{
  demuxbuffer = vectorAlloc_u8(segmentbytes*DEMUX_BUFFER_FACTOR);
  estimatedbytes += segmentbytes*DEMUX_BUFFER_FACTOR;
  threadbuffers = new u8*[numthreads];
  for(int i=0;i<numthreads;i++)
  {
    threadbuffers[i] = vectorAlloc_u8(segmentbytes*DEMUX_BUFFER_FACTOR/numthreads);
    estimatedbytes += segmentbytes*DEMUX_BUFFER_FACTOR/numthreads;
  }
  resetcounters();
}

DataMuxer::~DataMuxer()
{
  for(int i=0;i<numthreads;i++)
  {
    vectorFree(threadbuffers[i]);
  }
  delete [] threadbuffers;
  vectorFree(demuxbuffer);
}

void DataMuxer::resetcounters()
{
  readcount = 0;
  muxcount  = 0;
  deinterlacecount = 0;
  estimatedbytes = 0;
  skipframes = 0;
  lastskipframes = 0;
}

VDIFMuxer::VDIFMuxer(const Configuration * conf, int dsindex, int id, int nthreads, int iframebytes, int rframes, int fpersec, int bitspersamp, int * tmap)
  : DataMuxer(conf, dsindex, id, nthreads, iframebytes*rframes), inputframebytes(iframebytes), readframes(rframes), framespersecond(fpersec), bitspersample(bitspersamp)
{
  cverbose << startl << "VDIFMuxer: framespersecond is " << framespersecond << ", iframebytes is " << inputframebytes << endl;
  cinfo << startl << "VDIFMuxer: readframes is " << readframes << endl;
  outputframebytes = (inputframebytes-VDIF_HEADER_BYTES)*numthreads + VDIF_HEADER_BYTES;
  processframenumber = 0;
  numthreadbufframes = readframes * DEMUX_BUFFER_FACTOR / numthreads;
  activemask = (1<<bitspersample) - 1;
  threadindexmap = new int[numthreads];
  bufferframefull = new bool*[numthreads];
  threadwords = new unsigned int[numthreads];
  for(int i=0;i<numthreads;i++) {
    threadindexmap[i] = tmap[i];
    bufferframefull[i] = new bool[numthreadbufframes];
    for(int j=0;j<numthreadbufframes;j++)
      bufferframefull[i][j] = false;
  }  
}

VDIFMuxer::~VDIFMuxer()
{
  for(int i=0;i<numthreads;i++) {
    delete [] bufferframefull[i];
  }
  delete [] bufferframefull;
  delete [] threadwords;
  delete [] threadindexmap;
}

void VDIFMuxer::resetcounters()
{
  DataMuxer::resetcounters();
  processframenumber = 0;
  for(int i=0;i<numthreads;i++) {
    for(int j=0;j<numthreadbufframes;j++)
      bufferframefull[i][j] = false;
  }
}

bool VDIFMuxer::initialise()
{
  //check the size of an int
  if (sizeof(int) != 4) {
    cfatal << startl << "Int size is " << sizeof(int) << " bytes - VDIFMuxer assumes 4 byte ints - I must abort!" << endl;
    return false;
  }

  //get a reference time, calculate number of words and samples per input/output frame
  //assumes there is data at the start of the demuxbuffer already
  vdif_header *header = (vdif_header*)demuxbuffer;
  refframemjd = getVDIFFrameMJD(header);
  refframesecond = getVDIFFrameSecond(header);
  refframenumber = getVDIFFrameNumber(header);
  samplesperframe = ((inputframebytes-VDIF_HEADER_BYTES)*8)/bitspersample;
  wordsperinputframe = (inputframebytes-VDIF_HEADER_BYTES)/4;
  wordsperoutputframe = wordsperinputframe*numthreads;
  samplesperinputword = samplesperframe/wordsperinputframe;
  samplesperoutputword = samplesperinputword/numthreads;
  if(samplesperoutputword == 0) {
    cfatal << startl << "Too many threads/too high bit resolution - can't fit one complete timestep in a 32 bit word! Aborting." << endl;
    return false;
  }
#ifdef WORDS_BIGENDIAN
  // For big endian (non-intel), different, yet to be implemented, corner turners are needed
  // It is not even clear this generic one works for big endian...
  cornerturn = &VDIFMuxer::cornerturn_generic;
#else
  if (numthreads == 1) {
    cinfo << startl << "Using optimized VDIF corner turner: cornerturn_1thread" << endl;
    cornerturn = &VDIFMuxer::cornerturn_1thread;
  }
  else if (numthreads == 2 && bitspersample == 2) {
    cinfo << startl << "Using optimized VDIF corner turner: cornerturn_2thread_2bit" << endl;
    cornerturn = &VDIFMuxer::cornerturn_2thread_2bit;
  }
  else if (numthreads == 4 && bitspersample == 2) {
    cinfo << startl << "Using optimized VDIF corner turner: cornerturn_4thread_2bit" << endl;
    cornerturn = &VDIFMuxer::cornerturn_4thread_2bit;
  }
  else if (numthreads == 8 && bitspersample == 2) {
    cinfo << startl << "Using optimized VDIF corner turner: cornerturn_4thread_2bit" << endl;
    cornerturn = &VDIFMuxer::cornerturn_4thread_2bit;
  }
  else {
    cwarn << startl << "Using generic VDIF corner turner; performance may suffer" << endl;
    cornerturn = &VDIFMuxer::cornerturn_generic;
  }
#endif

  return true;
}

int VDIFMuxer::datacheck(u8 * checkbuffer, int bytestocheck, int startfrom)
{
  int consumedbytes, byteoffset, bytestoread;
  char * currentptr;
  char * lastptr;

  //loop over one read's worth of data, shifting any time we find a bad packet
  consumedbytes = (startfrom/inputframebytes)*inputframebytes;
  currentptr = (char*)checkbuffer + consumedbytes;
  bytestoread = 0;
  while(consumedbytes < bytestocheck - (inputframebytes-1)) {
    byteoffset = 0;
    if(getVDIFFrameBytes((vdif_header*)currentptr) != inputframebytes) {
      cwarn << startl << "Bad packet detected in VDIF datastream after " << consumedbytes << " bytes" << endl;
      lastptr = currentptr;
      byteoffset += 4;
      currentptr += 4;
      while(getVDIFFrameBytes((vdif_header*)currentptr) != inputframebytes && consumedbytes + byteoffset < bytestocheck) {
        byteoffset += 4;
        currentptr += 4;
      }
      if(consumedbytes + byteoffset < bytestocheck) {
        cwarn << startl << "Length of interloper packet was " << byteoffset << " bytes" << endl;
        cinfo << startl << "Newly found input frame details are bytes = " << getVDIFFrameBytes((vdif_header*)currentptr) << ", MJD = " << getVDIFFrameMJD((vdif_header*)currentptr) << ", framesecond = " << getVDIFFrameSecond((vdif_header*)currentptr) << ", framenumber = " << getVDIFFrameNumber((vdif_header*)currentptr) << endl;
      }
      else {
        cwarn << startl << "Reached end of checkbuffer before end of interloper packet - " << byteoffset << " bytes read" << endl;
      }
      bytestoread += byteoffset;
      memmove(lastptr, currentptr, bytestocheck - (consumedbytes + byteoffset));
      consumedbytes += byteoffset;
      currentptr -= byteoffset; //go back to where we should be, after having moved the memory...
    }
    consumedbytes += inputframebytes;
    currentptr += inputframebytes;
  }
  return bytestoread; 
} 

bool VDIFMuxer::validData(int bufferframe) const
{
  for(int i = 0;i < numthreads; i++) {
    if(!bufferframefull[i][bufferframe])
      return false;
  }
  return true;
}


void VDIFMuxer::cornerturn_generic(u8 * outputbuffer, int processindex, int outputframecount)
{
  unsigned int copyword;
  unsigned int * outputwordptr;
  
  //loop over all the samples and copy them in
  copyword = 0;
  for(int i=0;i<wordsperinputframe;i++) {
    for(int j=0;j<numthreads;j++)
      threadwords[j] = *(unsigned int *)(&(threadbuffers[j][processindex*inputframebytes + VDIF_HEADER_BYTES + i*4]));
    for(int j=0;j<numthreads;j++) {
      outputwordptr = (unsigned int *)&(outputbuffer[outputframecount*outputframebytes + VDIF_HEADER_BYTES + (i*numthreads + j)*4]);
      copyword = 0;
      for(int k=0;k<samplesperoutputword;k++) {
        for(int l=0;l<numthreads;l++) {
          copyword |= ((threadwords[l] >> ((j*samplesperoutputword + k)*bitspersample)) & (activemask)) << (k*numthreads + l)*bitspersample;
        }
      }
      *outputwordptr = copyword;
    }
  }
}


void VDIFMuxer::cornerturn_1thread(u8 * outputbuffer, int processindex, int outputframecount)
{
  // Trivial case of 1 thread: just a copy

  const void * t = (const void *)(&(threadbuffers[0][processindex*inputframebytes + VDIF_HEADER_BYTES]));
  void * outputwordptr = (void *)&(outputbuffer[outputframecount*outputframebytes + VDIF_HEADER_BYTES]);

  memcpy(outputwordptr, t, wordsperinputframe*4);
}


void VDIFMuxer::cornerturn_2thread_2bit(u8 * outputbuffer, int processindex, int outputframecount)
{
  // Efficiently handle the special case of 2 threads of 2-bit data.
  //
  // Thread: ------1-------   ------0-------   ------1-------   ------0-------
  // Byte:   ------1-------   ------1-------   ------0-------   ------0-------
  // Input:  b7  b6  b5  b4   a7  a6  a5  a4   b3  b2  b1  b0   a3  a2  a1  a0
  //
  // Shift:   0  -1  -2  -3   +3  +2  +1   0    0  -1  -2  -3   +3  +2  +1   0
  //
  // Output: b7  a7  b6  a6   b5  a5  b4  a4   b3  a3  b2  a2   b1  a1  b0  a0
  // Byte:   ------3-------   ------2-------   ------1-------   ------0-------

  const unsigned int M0 = 0xC003C003;
  const unsigned int M1 = 0x30003000;
  const unsigned int M2 = 0x000C000C;
  const unsigned int M3 = 0x0C000C00;
  const unsigned int M4 = 0x00300030;
  const unsigned int M5 = 0x03000300;
  const unsigned int M6 = 0x00C000C0;

  const u8 *t0 = threadbuffers[0] + processindex*inputframebytes + VDIF_HEADER_BYTES;
  const u8 *t1 = threadbuffers[1] + processindex*inputframebytes + VDIF_HEADER_BYTES;
  unsigned int *outputwordptr = (unsigned int *)&(outputbuffer[outputframecount*outputframebytes + VDIF_HEADER_BYTES]);
 
  for(int i = 0; i < wordsperoutputframe; ++i)
  {
    // assemble
    unsigned int x = (t1[1] << 24) | (t0[1] << 16) | (t1[0] << 8) | t0[0];

    // mask and shift
    *outputwordptr = (x & M0) | ((x & M1) >> 2) | ((x & M2) << 2) | ((x & M3) >> 4) | ((x & M4) << 4) | ((x & M5) >> 6) | ((x & M6) << 6);

    // advance pointers
    t0 += 2;
    t1 += 2;
    ++outputwordptr;
  }
}


void VDIFMuxer::cornerturn_4thread_2bit(u8 * outputbuffer, int processindex, int outputframecount)
{
  // Efficiently handle the special case of 4 threads of 2-bit data.  because nthread = samples/byte
  // this is effectively a matrix transpose.  With this comes some symmetries that make this case
  // unexpectedly simple.
  //
  // The trick is to first assemble a 32-bit word containing one 8 bit chunk of each thread
  // and then to reorder the bits using masking and shifts.  Only 7 unique shifts are needed.
  // Note: can be extended to do twice as many samples in a 64 bit word with about the same
  // number of instructions.  This results in a speed-down on 32-bit machines!
  //
  // This algorithm is approximately 9 times faster than the generic cornerturner for this case
  // and about 9 times harder to understand!  The table below (and others) indicates the sample motion
  //
  // Thread: ------3-------   ------2-------   ------1-------   ------0-------
  // Byte:   ------0-------   ------0-------   ------0-------   ------0-------
  // Input:  d3  d2  d1  d0   c3  c2  c1  c0   b3  b2  b1  b0   a3  a2  a1  a0
  //
  // Shift:   0  -3  -6  -9   +3   0  -3  -6   +6  +3   0  -3   +9  +6  +3   0
  //
  // Output: d3  c3  b3  a3   d2  c2  b2  a2   d1  c1  b1  a1   d0  c0  b0  a0
  // Byte:   ------3-------   ------2-------   ------1-------   ------0-------
  //
  // -WFB

  const unsigned int M0 = 0xC0300C03;
  const unsigned int M1 = 0x300C0300;
  const unsigned int M2 = 0x00C0300C;
  const unsigned int M3 = 0x0C030000;
  const unsigned int M4 = 0x0000C030;
  const unsigned int M5 = 0x03000000;
  const unsigned int M6 = 0x000000C0;

  const u8 *t0 = threadbuffers[0] + processindex*inputframebytes + VDIF_HEADER_BYTES;
  const u8 *t1 = threadbuffers[1] + processindex*inputframebytes + VDIF_HEADER_BYTES;
  const u8 *t2 = threadbuffers[2] + processindex*inputframebytes + VDIF_HEADER_BYTES;
  const u8 *t3 = threadbuffers[3] + processindex*inputframebytes + VDIF_HEADER_BYTES;
  unsigned int *outputwordptr = (unsigned int *)&(outputbuffer[outputframecount*outputframebytes + VDIF_HEADER_BYTES]);

  for(int i = 0; i < wordsperoutputframe; ++i)
  {
    // assemble
    unsigned int x = (*t3 << 24) | (*t2 << 16) | (*t1 << 8) | *t0;

    // mask and shift
    *outputwordptr = (x & M0) | ((x & M1) >> 6) | ((x & M2) << 6) | ((x & M3) >> 12) | ((x & M4) << 12) | ((x & M5) >> 18) | ((x & M6) << 18);
  
    // advance pointers
    ++t0;
    ++t1;
    ++t2;
    ++t3;
    ++outputwordptr;
  }
}


void VDIFMuxer::cornerturn_8thread_2bit(u8 * outputbuffer, int processindex, int outputframecount)
{
  // Efficiently handle the special case of 8 threads of 2-bit data.
  //
  // Thread: ------7-------   ------6-------   ------5-------   ------4-------   ------3-------   ------2-------   ------1-------   ------0-------
  // Byte:   ------0-------   ------0-------   ------0-------   ------0-------   ------0-------   ------0-------   ------0-------   ------0-------
  // Input:  h3  h2  h1  h0   g3  g2  g1  g0   f3  f2  f1  f0   e3  e2  e1  e0   d3  d2  d1  d0   c3  c2  c1  c0   b3  b2  b1  b0   a3  a2  a1  a0
  //
  // Shift:   0  -7 -14 -21   +3  -4 -11 -18   +6  -1  -8 -15   +9  +2  -5 -12  +12  +5  -2  -9  +15  +8  +1  -6  +18 +11  +4  -3  +21 +14  +7   0
  //
  // Output: h3  g3  f3  e3   d3  c3  b3  a3   h2  g2  f2  e2   d2  c2  b2  a2   h1  g1  f1  e1   d1  c1  b1  a1   h0  g0  f0  e0   d0  c0  b0  a0
  // Byte:   ------7-------   ------6-------   ------5-------   ------4-------   ------3-------   ------2-------   ------1-------   ------0-------
  //
  // This one is a bit complicated.  A resonable way to proceed seems to be to perform two separate 4-thread corner turns and then 
  // do a final suffle of byte sized chunks.  There may be a better way...
  //
  // FIXME: This is thought to work but has yet to be fully verified.

  const unsigned int M0 = 0xC0300C03;
  const unsigned int M1 = 0x300C0300;
  const unsigned int M2 = 0x00C0300C;
  const unsigned int M3 = 0x0C030000;
  const unsigned int M4 = 0x0000C030;
  const unsigned int M5 = 0x03000000;
  const unsigned int M6 = 0x000000C0;

  const u8 *t0 = threadbuffers[0] + processindex*inputframebytes + VDIF_HEADER_BYTES;
  const u8 *t1 = threadbuffers[1] + processindex*inputframebytes + VDIF_HEADER_BYTES;
  const u8 *t2 = threadbuffers[2] + processindex*inputframebytes + VDIF_HEADER_BYTES;
  const u8 *t3 = threadbuffers[3] + processindex*inputframebytes + VDIF_HEADER_BYTES;
  const u8 *t4 = threadbuffers[4] + processindex*inputframebytes + VDIF_HEADER_BYTES;
  const u8 *t5 = threadbuffers[5] + processindex*inputframebytes + VDIF_HEADER_BYTES;
  const u8 *t6 = threadbuffers[6] + processindex*inputframebytes + VDIF_HEADER_BYTES;
  const u8 *t7 = threadbuffers[7] + processindex*inputframebytes + VDIF_HEADER_BYTES;
  unsigned int *outputwordptr = (unsigned int *)&(outputbuffer[outputframecount*outputframebytes + VDIF_HEADER_BYTES]);

  for(int i = 0; i < wordsperoutputframe; i += 2)
  {
    unsigned int x1;
    unsigned int x2;
    union { unsigned int y1; u8 b1[4]; };
    union { unsigned int y2; u8 b2[4]; };
    
    // assemble 32-bit chunks
    x1 = (*t3 << 24) | (*t2 << 16) | (*t1 << 8) | *t0;
    x2 = (*t7 << 24) | (*t6 << 16) | (*t5 << 8) | *t4;

    // mask and shift 32-bit chunks
    y1 = (x1 & M0) | ((x1 & M1) >> 6) | ((x1 & M2) << 6) | ((x1 & M3) >> 12) | ((x1 & M4) << 12) | ((x1 & M5) >> 18) | ((x1 & M6) << 18);
    y2 = (x2 & M0) | ((x2 & M1) >> 6) | ((x2 & M2) << 6) | ((x2 & M3) >> 12) | ((x2 & M4) << 12) | ((x2 & M5) >> 18) | ((x2 & M6) << 18);

    // shuffle 8-bit chunks
    outputwordptr[0] = (b2[1] << 24) | (b1[1] << 16) | (b2[0] << 8) | b1[0];
    outputwordptr[1] = (b2[3] << 24) | (b1[3] << 16) | (b2[2] << 8) | b1[2];

    ++t0;
    ++t1;
    ++t2;
    ++t3;
    ++t4;
    ++t5;
    ++t6;
    ++t7;
    outputwordptr += 2;
  }
}


int VDIFMuxer::multiplex(u8 * outputbuffer)
{
  vdif_header * header;
  vdif_header * copyheader;
  int outputframecount, processindex;
  bool foundframe;

  outputframecount = 0;
  lastskipframes = skipframes;

  //loop over one read's worth of data
  for(int f=0;f<readframes/numthreads;f++) {
    //rearrange one frame
    processindex = processframenumber % (readframes*DEMUX_BUFFER_FACTOR/numthreads);
    if(validData(processindex)) {
      //copy in and tweak up the VDIF header
      header = (vdif_header *)(outputbuffer + outputframecount*outputframebytes);
      memcpy(header, (char *)(threadbuffers[0] + processindex*inputframebytes), VDIF_HEADER_BYTES);
      setVDIFFrameInvalid(header, 0);
      setVDIFNumChannels(header, numthreads);
      setVDIFFrameBytes(header, outputframebytes);
      setVDIFThreadID(header, 0);

      // call the corner turning function.  gotta love this syntax!
      (this->*cornerturn)(outputbuffer, processindex, outputframecount);
      
      outputframecount++;
    }
    else {
      cdebug << startl << "Not all threads had valid data for frame " << processframenumber << endl;
      //just copy in a header, tweak it up for the right time/threadid and set it to invalid
      foundframe = false;
      for(int i = 0;i < numthreads; i++) {
        if(bufferframefull[i][processindex]) {
          header = (vdif_header *)(outputbuffer + outputframecount*outputframebytes); 
          memcpy(header, (char *)(threadbuffers[i] + processindex*inputframebytes), VDIF_HEADER_BYTES);
          setVDIFFrameInvalid(header, 1);
          setVDIFFrameBytes(header, outputframebytes);
          setVDIFThreadID(header, 0);
          foundframe = true;
          break;
        }
      }
      if(foundframe) {
        outputframecount++;
      }
      else {
        if(outputframecount>0) { //take the preceding output frame instead
          header = (vdif_header *)(outputbuffer + outputframecount*outputframebytes);
          copyheader = (vdif_header *)(outputbuffer + (outputframecount-1)*outputframebytes);
          memcpy(header, copyheader, VDIF_HEADER_BYTES);
          setVDIFFrameInvalid(header, 1);
          setVDIFFrameNumber(header, getVDIFFrameNumber(header)+1);
          if(getVDIFFrameNumber(header) == framespersecond) {
            setVDIFFrameNumber(header, 0);
            setVDIFFrameSecond(header, getVDIFFrameSecond(header)+1);
          }
          outputframecount++;
        }
        else {
          //if NONE of the input thread frames are valid, *and* this is the first output frame, we have to make sure this is ignored
          cwarn << startl << "No valid input frames for frame " << processframenumber << "; the rest of this data segment will be lost" << endl;
          //don't increment outputframecount
        }
      }
    }

    //clear the data we just used
    for(int i=0;i<numthreads;i++)
      bufferframefull[i][processindex] = false;
    processframenumber++;
  }

  return outputframecount*outputframebytes;
}

bool VDIFMuxer::deinterlace(int validbytes)
{
  int framethread, framebytes, framemjd, framesecond, framenumber;
  int frameoffset, frameindex, threadindex;
  long long currentframenumber;
  bool found;
  vdif_header * inputptr;

  //cout << "Deinterlacing: deinterlacecount is " << deinterlacecount << endl;
  //cout << "Will start from " << (deinterlacecount%DEMUX_BUFFER_FACTOR)*readframes << " frames in" << endl;
  for(int i=0;i<validbytes/inputframebytes;i++) {
    inputptr = (vdif_header*)(demuxbuffer + i*inputframebytes + (deinterlacecount%DEMUX_BUFFER_FACTOR)*readframes*inputframebytes);
    framethread = getVDIFThreadID(inputptr);
    framebytes = getVDIFFrameBytes(inputptr);
    framemjd = getVDIFFrameMJD(inputptr);
    framesecond = getVDIFFrameSecond(inputptr);
    framenumber = getVDIFFrameNumber(inputptr);
    if(framebytes != inputframebytes) {
      cfatal << startl << "Framebytes has changed, from " << inputframebytes << " to " << framebytes << " - aborting!" << endl;
      return false;
    }
    //check that this thread is wanted
    found = false;
    for(int j=0;j<numthreads;j++) {
      if(threadindexmap[j] == framethread) {
        found = true;
        threadindex = j;
        break;
      }
    }
    if(!found) {
      cdebug << startl << "Skipping packet from threadId " << framethread << ", numthreads is " << numthreads << ", mapping is " << endl;
      for(int j=0;j<numthreads;j++)
        cdebug << startl << threadindexmap[j] << endl;
      continue;
    }

    //put this frame where it belongs
    currentframenumber = ((long long)((framemjd-refframemjd)*86400 + framesecond - refframesecond))*framespersecond + framenumber - refframenumber;
    if (currentframenumber < 0) {
      cinfo << startl << "Discarding a frame from thread " << framethread << " which is timestamped " << -currentframenumber << " frames earlier than the first frame in the file" << endl;
      cinfo << startl << "Currentframenumber is " << currentframenumber << ", processframenumber is " << processframenumber << ", framesecond is " << framesecond << ", refframesecond is " << refframesecond << ", framenumber is " << framenumber << ", refframenumber is " << refframenumber << ", framespersecond is " << framespersecond << endl;
      continue;
    }
    frameoffset  = (int) (currentframenumber - (processframenumber+skipframes));
    if(i == 0) {
      cinfo << startl << "Current first frame to deinterlace is from " <<  framethread << " which is timestamped " << frameoffset << " frames after the current frame being processed" << endl;
      cinfo << startl << "Currentframenumber is " << currentframenumber << ", processframenumber is " << processframenumber << ", framesecond is " << framesecond << ", refframesecond is " << refframesecond << ", framenumber is " << framenumber << ", refframenumber is " << refframenumber << ", framespersecond is " << framespersecond << ", lastskipframes is " << lastskipframes << endl;
      cinfo << startl << "Frames to process is " << (validbytes/inputframebytes) << " because validbytes = " << validbytes << " and inputframebytes = " << inputframebytes << endl;
    }
    if(frameoffset < 0) {
      cwarn << startl << "Discarding a frame from thread " << framethread << " which is timestamped " << -frameoffset << " frames earlier than the current frame being processed" << endl;
      cwarn << startl << "Currentframenumber is " << currentframenumber << ", processframenumber is " << processframenumber << ", framesecond is " << framesecond << ", refframesecond is " << refframesecond << ", framenumber is " << framenumber << ", refframenumber is " << refframenumber << ", framespersecond is " << framespersecond << endl;
      continue;
    }
    if(frameoffset > readframes*DEMUX_BUFFER_FACTOR/numthreads) {
      cfatal << startl << "Discarding a frame from thread " << framethread << " which is timestamped " << frameoffset << " frames after the current frame being processed - must be a large offset in the file, which is not supported" << endl;
      cfatal << startl << "Currentframenumber is " << currentframenumber << ", processframenumber is " << processframenumber << ", framesecond is " << framesecond << ", refframesecond is " << refframesecond << ", framenumber is " << framenumber << ", refframenumber is " << refframenumber << ", framespersecond is " << framespersecond << ", i=" << i << ", skipframes is " << skipframes << ", lastskipframes is " << lastskipframes << endl;
      cfatal << startl << "The following frame would have had information: frame number = " << getVDIFFrameNumber(inputptr+inputframebytes) << ", framesecond = " << getVDIFFrameSecond(inputptr+inputframebytes) << endl;
      return false;
    }
    frameindex = (int)(currentframenumber % numthreadbufframes);
    if (bufferframefull[threadindex][frameindex]) {
      cwarn << startl << "Frame at index " << frameindex << " (which was count " << currentframenumber << ") was already full for thread " << threadindex << " - probably a major time gap in the file, which is not supported" << endl;
    }
    memcpy(threadbuffers[threadindex] + frameindex*framebytes, inputptr, framebytes);
    bufferframefull[threadindex][frameindex] = true;
  }
  deinterlacecount++;
  return true;
}

// vim: shiftwidth=2:softtabstop=2:expandtab
