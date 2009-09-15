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
#include <mpi.h>
#include "mk5.h"
#include "mode.h"
#include <iomanip>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "alert.h"

#define MAXPACKETSIZE 100000
#define MARK5FILL 0x11223344;

/// Mk5DataStream -------------------------------------------------------

Mk5DataStream::Mk5DataStream(Configuration * conf, int snum, int id, int ncores, int * cids, int bufferfactor, int numsegments)
 : DataStream(conf, snum, id, ncores, cids, bufferfactor, numsegments)
{
  //each data buffer segment contains an integer number of frames, because thats the way config determines max bytes
  lastconfig = -1;
}

Mk5DataStream::~Mk5DataStream()
{
  if(syncteststream != 0)
    delete_mark5_stream(syncteststream);
}

void Mk5DataStream::initialise()
{
  DataStream::initialise();

  syncteststream = 0;
  udp_offset = 0;
  if (!readfromfile && !tcp) {
    if (sizeof(long long)!=8) {
      cfatal << startl << "DataStream assumes long long is 8 bytes, it is " << sizeof(long long) << " bytes" << endl;
      MPI_Abort(MPI_COMM_WORLD, 1);
    }
    udpsize = abs(tcpwindowsizebytes/1024)-20-2*4-sizeof(long long); // IP header is 20 bytes, UDP is 4x2 bytes
    if (udpsize<=0) {
      cfatal << startl << "Datastream " << mpiid << ":" << " Warning implied UDP packet size is negative!! " << endl;
      MPI_Abort(MPI_COMM_WORLD, 1);
    }
    udpsize &= ~ 0x7;  // Ensures udpsize multiple of 64 bits
    packet_segmentstart = 0;
    packet_head = 0;
    udp_buf = new char[MAXPACKETSIZE];
    packets_arrived.resize(readbytes/udpsize+2);

    if (sizeof(int)!=4) {
      cfatal << startl << "DataStream assumes int is 4 bytes, it is " << sizeof(int) << " bytes" << endl;
      MPI_Abort(MPI_COMM_WORLD, 1);
    }

    invalid_buf = new char[udpsize];
    unsigned int *tmp = (unsigned int*)invalid_buf;
    for (int i=0; i<udpsize/4; i++) {
      tmp[i] = MARK5FILL;
    }

    // UDP statistics
    packet_drop = 0;
    packet_oo = 0;
    packet_duplicate = 0;
    npacket = 0;
    packet_sum = 0;
  }

}

int Mk5DataStream::calculateControlParams(int scan, int offsetsec, int offsetns)
{
  int bufferindex, framesin, vlbaoffset;

  bufferindex = DataStream::calculateControlParams(scan, offsetsec, offsetns);

  if(bufferinfo[atsegment].controlbuffer[bufferinfo[atsegment].numsent][1] == Mode::INVALID_SUBINT)
    return 0;

  //do the necessary correction to start from a frame boundary - work out the offset from the start of this segment
  vlbaoffset = bufferindex - atsegment*readbytes;

  if(vlbaoffset < 0)
  {
    cwarn << startl << "Mk5DataStream::calculateControlParams : vlbaoffset=" << vlbaoffset << " bufferindex=" << bufferindex << " atsegment=" << atsegment << endl; 
    cwarn << startl << "Mk5DataStream::calculateControlParams : readbytes=" << readbytes << ", framebytes=" << framebytes << ", payloadbytes=" << payloadbytes << endl;
    bufferinfo[atsegment].controlbuffer[bufferinfo[atsegment].numsent][1] = Mode::INVALID_SUBINT;
    MPI_Abort(MPI_COMM_WORLD, 1);
    return 0;
  }

  // bufferindex was previously computed assuming no framing overhead
  framesin = vlbaoffset/payloadbytes;

  // Note here a time is needed, so we only count payloadbytes
  int segoffns = bufferinfo[atsegment].scanns + (int)((1000000000.0*framesin)/framespersecond);
  bufferinfo[atsegment].controlbuffer[bufferinfo[atsegment].numsent][1] = bufferinfo[atsegment].scanseconds + segoffns/1000000000;
  bufferinfo[atsegment].controlbuffer[bufferinfo[atsegment].numsent][2] = segoffns%1000000000;

  //go back to nearest frame -- here the total number of bytes matters
  bufferindex = atsegment*readbytes + framesin*framebytes;
  if(bufferindex >= bufferbytes)
  {
    cwarn << startl << "Mk5DataStream::calculateControlParams : bufferindex=" << bufferindex << " >= bufferbytes=" << bufferbytes << " atsegment = " << atsegment << endl;
    bufferinfo[atsegment].controlbuffer[bufferinfo[atsegment].numsent][1] = Mode::INVALID_SUBINT;
    MPI_Abort(MPI_COMM_WORLD, 1);
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
  bufferinfo[segmentindex].sendbytes = int(((((double)bufferinfo[segmentindex].sendbytes)* ((double)config->getSubintNS(bufferinfo[segmentindex].configindex)))/(config->getSubintNS(bufferinfo[segmentindex].configindex) + config->getGuardNS(bufferinfo[segmentindex].configindex)) + 0.5));
}

void Mk5DataStream::initialiseFile(int configindex, int fileindex)
{
  int offset;
  int nbits, nrecordedbands, framebytes, fanout, jumpseconds, currentdsseconds;
  Configuration::dataformat format;
  double bw, bytespersecond;
  long long dataoffset = 0;

  format = config->getDataFormat(configindex, streamnum);
  nbits = config->getDNumBits(configindex, streamnum);
  nrecordedbands = config->getDNumRecordedBands(configindex, streamnum);
  framebytes = config->getFrameBytes(configindex, streamnum);
  bw = config->getDRecordedBandwidth(configindex, streamnum, 0);

  fanout = config->genMk5FormatName(format, nrecordedbands, bw, nbits, framebytes, config->getDDecimationFactor(configindex, streamnum), formatname);
  if (fanout < 0) {
    cfatal << startl << "Fanount is " << fanout << ", which is impossible - no choice but to abort!" << endl;
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  cout << "Format name is " << formatname << ", configindex is " << configindex << ", fileindex is " << fileindex << endl;
  if(syncteststream != 0)
    delete_mark5_stream(syncteststream);
  syncteststream = new_mark5_stream(
    new_mark5_stream_file(datafilenames[configindex][fileindex].c_str(), 0),
    new_mark5_format_generic_from_string(formatname) );
  cout << "Value of syncteststream was " << syncteststream << endl;
  if(syncteststream == 0)
  {
    cwarn << startl << " could not open file " << datafilenames[configindex][fileindex] << endl;
    dataremaining = false;
    return;
  }
  if(syncteststream->nchan != config->getDNumRecordedBands(configindex, streamnum))
  {
    cerror << startl << "Error - number of recorded bands for datastream " << streamnum << " (" << nrecordedbands << ") does not match with MkV file " << datafilenames[configindex][fileindex] << " (" << syncteststream->nchan << "), will be ignored!!!" << endl;
  }
  cout << "Successfully used mark5stream" << endl;

  // resolve any day ambiguities
  mark5_stream_fix_mjd(syncteststream, corrstartday);

  mark5_stream_print(syncteststream);

  offset = syncteststream->frameoffset;

  bytespersecond = syncteststream->framebytes/syncteststream->framens * 1e9;

  readseconds = 86400*(syncteststream->mjd-corrstartday) + syncteststream->sec-corrstartseconds + intclockseconds;
  readnanoseconds = int(syncteststream->ns);
  currentdsseconds = activesec + model->getScanStartSec(activescan, config->getStartMJD(), config->getStartSeconds());

  if (currentdsseconds  > readseconds+1)
  {
    jumpseconds = currentdsseconds - readseconds;
    if (activens < readnanoseconds)
    {
      jumpseconds--;
    }

    // set byte offset to the requested time
    dataoffset = (long long)(jumpseconds * bytespersecond);
    readseconds += jumpseconds;
  }

  while(readscan < (model->getNumScans()-1) && model->getScanEndSec(readscan, corrstartday, corrstartseconds) < readseconds)
    readscan++;
  while(readscan > 0 && model->getScanStartSec(readscan, corrstartday, corrstartseconds) > readseconds)
    readscan--;
  readseconds = readseconds - model->getScanStartSec(readscan, corrstartday, corrstartseconds);
  cverbose << startl << "The frame start is day=" << syncteststream->mjd << ", seconds=" << syncteststream->sec << ", ns=" << syncteststream->ns << ", readscan=" << readscan << ", readseconds=" << readseconds << ", readns=" << readnanoseconds << endl;

  //close the stream used to get the offset, create the one we will use to test
  delete_mark5_stream(syncteststream);
  syncteststream = new_mark5_stream(new_mark5_stream_unpacker(0), new_mark5_format_generic_from_string(formatname) );
  cverbose << startl << "Value of syncteststream after reopening was " << syncteststream << endl;

  cverbose << startl << "About to seek to byte " << offset << " plus " << dataoffset << " to get to the first frame" << endl;

  input.seekg(offset + dataoffset, ios_base::cur);
  if (input.peek() == EOF) {
    dataremaining = false;
    input.clear();
  }
}

int Mk5DataStream::testForSync(int configindex, int buffersegment)
{
  int corrday, corrsec;
  int mjd, sec;
  double ns, deltatime;
  struct mark5_stream *mark5stream;
  int offset;
  char * ptr;

  offset = 0;
  corrday = config->getStartMJD();
  corrsec = config->getStartSeconds();
  syncteststream->frame = (uint8_t *)(&(databuffer[buffersegment*(bufferbytes/numdatasegments)]));
  // resolve any day ambiguities
  mark5_stream_fix_mjd(syncteststream, corrday);
  mark5_stream_get_frame_time(syncteststream, &mjd, &sec, &ns);
  syncteststream->frame = 0;
  deltatime = 86400*(corrday - mjd) + (model->getScanStartSec(bufferinfo[buffersegment].scan, corrday, corrsec) + bufferinfo[buffersegment].scanseconds + corrsec - sec) + double(bufferinfo[buffersegment].scanns-ns)/1e9;

  if(fabs(deltatime) > 1e-10) //oh oh, a problem
  {
    cerror << startl << "Lost Sync! Will attempt to resync. Deltatime was " << deltatime << endl;
    cdebug << startl << "Corrday was " << corrday << ", corrsec was " << corrsec << ". MJD was " << mjd << ", sec was " << sec << "> Readseconds was " << bufferinfo[buffersegment].scanseconds << ". readns was " << bufferinfo[buffersegment].scanns << ", ns was " << ns << endl;
    mark5stream = new_mark5_stream(
    new_mark5_stream_memory(&databuffer[buffersegment*(bufferbytes/numdatasegments)], bufferinfo[buffersegment].validbytes), new_mark5_format_generic_from_string(formatname) );

    if (mark5stream==0)
    {
      //Don't change time (will be deadreckoned from that last segment) but set validbytes to zero (crap data)
      cwarn << startl << "Could not identify Mark5 segment time (" << formatname << ") - this segment will be trash!" << endl;
      bufferinfo[buffersegment].validbytes = 0;
    }
    else
    {
      if(mark5stream->nchan != config->getDNumRecordedBands(configindex, streamnum))
      {
        cerror << startl << "Number of recorded bands for datastream " << streamnum << " (" << config->getDNumRecordedBands(configindex, streamnum) << ") does not match with MkV data " << " (" << mark5stream->nchan << "), will be ignored!!!" << endl;
      }

      // resolve any day ambiguities
      mark5_stream_fix_mjd(mark5stream, corrstartday);

      if (configindex != lastconfig) {
        cinfo << startl << "Config has changed!" << endl;
        mark5_stream_print(mark5stream);
        lastconfig = configindex;
      }

      offset = mark5stream->frameoffset;

      // If offset is non-zero we need to shuffle the data in memory to align on a frame boundary
      if (offset>0) { 
        ptr = (char*)&databuffer[buffersegment*(bufferbytes/numdatasegments)];

        if (offset>bufferinfo[buffersegment].validbytes) {
          cerror << startl << "Mark5 offset (" << offset << ") > valid bytes in current segment (" << bufferinfo[buffersegment].validbytes << "!!! Will trash this segment." << endl;
          bufferinfo[buffersegment].validbytes = 0;
        } else {
          int nread, status;
          cinfo << startl << "************: Shifting " << offset << " bytes in memory to regain sync" << endl;
          memmove(ptr, ptr+offset, bufferinfo[buffersegment].validbytes-offset);

          //No need to update validbytes - caller will attempt to fill the missing data
          //just fill in the new times
          readseconds = 86400*(mark5stream->mjd-corrstartday) + mark5stream->sec-corrstartseconds + intclockseconds;
          readnanoseconds = mark5stream->ns;
          while(readscan < (model->getNumScans()-1) && model->getScanEndSec(readscan, corrstartday, corrstartseconds) < readseconds)
            readscan++;
          while(readscan > 0 && model->getScanStartSec(readscan, corrstartday, corrstartseconds) > readseconds)
            readscan--;
          readseconds = readseconds - model->getScanStartSec(readscan, corrstartday, corrstartseconds);

          cinfo << startl << "After regaining sync, the frame start day is " << mark5stream->mjd << ", the frame start seconds is " << mark5stream->sec << ", the frame start ns is " << mark5stream->ns << ", readscan is " << readscan << ", readseconds is " << readseconds << ", readnanoseconds is " << readnanoseconds << endl;
        }
      }
      delete_mark5_stream(mark5stream);
    }
  }

  return offset;
}

void Mk5DataStream::networkToMemory(int buffersegment, int & framebytesremaining)
{
  int offset;
  int nbits, nrecordedbands, framebytes, fanout;
  Configuration::dataformat format;
  double bw;

  if (udp_offset>readbytes) {
    cinfo << startl << "DataStream " << mpiid << ": Skipping over " << udp_offset-(udp_offset%readbytes) << " bytes" << endl;
    udp_offset %= readbytes;
  }
  if(bufferinfo[buffersegment].configindex != lastconfig)
  {
    //regenerate formatname (only ever likely in eVLBI, and probably not even then)
    delete_mark5_stream(syncteststream);
    format = config->getDataFormat(bufferinfo[buffersegment].configindex, streamnum);
    nbits = config->getDNumBits(bufferinfo[buffersegment].configindex, streamnum);
    nrecordedbands = config->getDNumRecordedBands(bufferinfo[buffersegment].configindex, streamnum);
    framebytes = config->getFrameBytes(bufferinfo[buffersegment].configindex, streamnum);
    bw = config->getDRecordedBandwidth(bufferinfo[buffersegment].configindex, streamnum, 0);

    fanout = config->genMk5FormatName(format, nrecordedbands, bw, nbits, framebytes, config->getDDecimationFactor(bufferinfo[buffersegment].configindex, streamnum), formatname);
    if (fanout < 0) {
      cfatal << startl << "Fanount is " << fanout << ", which is impossible - no choice but to abort!" << endl;
      MPI_Abort(MPI_COMM_WORLD, 1);
    }

    syncteststream = new_mark5_stream(new_mark5_stream_unpacker(0), new_mark5_format_generic_from_string(formatname) );
  }

  DataStream::networkToMemory(buffersegment, framebytesremaining);
}


/****
 ASSUMPTIONS

 udp_offset is left containing number of bytes still to be consumed from last UDP packet. If
 first packet from next frame is missing then udp_offset points to END of initial data. It is
 assumed only udpsize bytes are available though. udp_buf actually contains the left over bytes 
 so that must be dealt with before any more data is read.

*****/


int Mk5DataStream::readnetwork(int sock, char* ptr, int bytestoread, int* nread)
{
  ssize_t nr;

  if (tcp) {
    DataStream::readnetwork(sock, ptr, bytestoread, nread);
  } else { // UDP
    bool done, first;
    unsigned int segmentsize;
    long long packet_index, packet_segmentend, next_segmentstart=0, next_udpoffset=0;
    unsigned long long sequence;
    struct msghdr msg;
    struct iovec iov[2];

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov        = &iov[0];
    msg.msg_iovlen     = 2;
    iov[0].iov_base = &sequence;
    iov[0].iov_len = sizeof(sequence);
    iov[1].iov_base = udp_buf;
    iov[1].iov_len = MAXPACKETSIZE;

    packet_segmentend = packet_segmentstart+(bytestoread-(udp_offset%udpsize)-1)/udpsize;
    if (udp_offset>0) packet_segmentend++;
    segmentsize = packet_segmentend-packet_segmentstart+1;

    //cinfo << startl << "****** udpsize " << udpsize << "  bytestoread " << bytestoread << endl;
    //cinfo << startl << "****** udp_offset = " << udp_offset << endl;
    //cinfo << startl << "****** readnetwork will read packets " << packet_segmentstart << " to " << packet_segmentend << "(" << segmentsize << ")" << endl;
    
    if (segmentsize>packets_arrived.size()) {
      cfatal << startl << "Mk5DataStream::readnetwork bytestoread too large (" << bytestoread << "/" << segmentsize << ")" << endl;
      cinfo << startl << "packet_segmentstart = " << packet_segmentstart << endl;
      cinfo << startl << "packet_segmentend = " << packet_segmentend << endl;
      cinfo << startl << "segmentsize = " << segmentsize << endl;
      cinfo << startl << "udp_offset = " << udp_offset << endl;
      cinfo << startl << "udpsize = " << udpsize << endl;
      cinfo << startl << "packets_arrived.size() = " << packets_arrived.size() << endl;

      MPI_Abort(MPI_COMM_WORLD, 1);
    } 
    
    for (unsigned int i=0; i<segmentsize; i++) {
      packets_arrived[i] = false;
    }
    *nread = -1;
    done = 0;

    // First copy left over bytes from last packet
    if (udp_offset>0) {
      packet_index = (udp_offset-1)/udpsize;
      udp_offset = (udp_offset-1)%udpsize+1;
      if (packet_index<segmentsize) // Check not out of range
	packets_arrived[packet_index] = true;

      if (bytestoread<udp_offset+packet_index*udpsize) {
	if (packet_index>=segmentsize) {

	  if (udp_offset==udpsize) 
	    next_segmentstart = packet_segmentend+1;
	  else
	    next_segmentstart = packet_segmentend;

	  next_udpoffset = udp_offset + packet_index*udpsize-bytestoread;
	  packet_drop+=segmentsize;

	} else if (packet_index==0) {
	  memcpy(ptr, udp_buf, bytestoread);
	  packet_sum += bytestoread;
	  npacket++;
	  udp_offset -= bytestoread;
	  if (udp_offset>0)
	    memmove(udp_buf, udp_buf+bytestoread, udp_offset);

	} else {
	  int bytes = (bytestoread-udp_offset)%udpsize;
	  memcpy(ptr+udp_offset+(packet_index-1)*udpsize, udp_buf, bytes);  
	  packet_sum += bytes;
	  npacket++;
	  memmove(udp_buf,udp_buf+bytes, udpsize-bytes);
	  udp_offset = udpsize-bytes;
	  next_segmentstart = packet_head;  // CHECK THIS IS CORRECT
	}
	done = 1;
	*nread = bytestoread;

      } else {
	if (packet_index==0) { // Partial packet
	  memcpy(ptr, udp_buf, udp_offset);
	  packet_sum += udp_offset;
	  npacket++;
	} else {
	  memcpy(ptr+udp_offset+(packet_index-1)*udpsize, udp_buf, udpsize);
	  npacket++;
	  packet_sum += udpsize;
	}
      } 
    } else {
      udp_offset = udpsize;
    }

    first = 1;
    while (!done) {
      int expectedsize = udpsize+sizeof(long long);
      nr = recvmsg(sock,&msg,MSG_WAITALL);
      if (nr==-1) { // Error reading socket
	if (errno == EINTR) continue;
	return(nr);
      } else if (nr==0) {  // Socket closed remotely
	cinfo << startl << "Datastream " << mpiid << ": Remote socket closed" << endl;
	return(nr);
      } else if (nr!=expectedsize) {
	cfatal << startl << "DataStream " << mpiid << ": Expected " << expectedsize << " bytes, got " << nr << "bytes. Quiting" << endl;
        MPI_Abort(MPI_COMM_WORLD, 1);
      } else {
	if (packet_head==0 && sequence!=0) { // First packet!
	  packet_head = sequence;
	  cinfo << startl <<  "DataStream " << mpiid << ": First packet has sequence " << sequence << endl;

	  packet_segmentstart = sequence;
	  packet_segmentend = packet_segmentstart+(bytestoread-1)/udpsize;
	}

	// Check this is a sensible packet
	packet_index = sequence-packet_segmentstart;
	if (packet_index<0) {
	  // If this was much smaller we really should assume the sequence has got screwed up and 
	  // Resync
	  packet_oo++;  // This could be duplicate but we cannot tell
	  // Probably should decrease packet dropped count, maybe (it was not counted after all)
	} else if (sequence==packet_segmentend) { 
	  //cinfo << startl << "**Segmentend " << packet_index << " (" << packet_segmentend << ")" << endl;
	  int bytes = (bytestoread-udp_offset-1)%udpsize+1;
	  // Consistence check
	  if (bytes<0) {
	    cfatal << startl << "Datastream " << mpiid << ": Error read too many UDP packets!!" << endl;
	    MPI_Abort(MPI_COMM_WORLD, 1);
	  } else if (bytes > udpsize) {
	    cfatal << startl << "Datastream " << mpiid << ": Error have not read enough UDP packets!!" << endl;
	    MPI_Abort(MPI_COMM_WORLD, 1);
	  }
	  memcpy(ptr+udp_offset+(packet_index-1)*udpsize,udp_buf,bytes);
	  packet_sum += bytes;
	  memmove(udp_buf,udp_buf+bytes, udpsize-bytes);
	  next_udpoffset = udpsize-bytes;
	  if (next_udpoffset==0) {
	    next_segmentstart = sequence+1; 
	    npacket++;
	  } else
	    next_segmentstart = sequence; 
	  packet_head = sequence;
	  *nread = bytestoread;
	  packets_arrived[packet_index] = true;
	  done = 1;
	} else if (packet_index>=segmentsize) { 
	  //cinfo << startl << "*********Past Segmentend " << sequence << endl;

	  if (udp_offset==udpsize) 
	    next_segmentstart = packet_segmentend+1;
	  else
	    next_segmentstart = packet_segmentend;

	  next_udpoffset = udpsize-(bytestoread-udp_offset)%udpsize+udpsize*(sequence-packet_segmentend);
	  //cinfo << startl << "**********udp_offset= " << next_udpoffset << endl;
	  packet_head = sequence;
	  *nread = bytestoread;
	  done = 1;
	} else if (packets_arrived[packet_index]) {
	  //cinfo << startl << "**** Duplicate " << packet_index << endl;
	  packet_duplicate++;
	} else {
	  // This must be good data finally!
	  if (first) {
	    first = false;
	    //cinfo << startl << "**** First packet " << packet_index << endl;
	  }
	  packets_arrived[packet_index] = true;
	  memcpy(ptr+udp_offset+(packet_index-1)*udpsize,udp_buf,udpsize);
	  packet_sum += udpsize;
	  *nread += udpsize;
	  
	  // Do packet statistics
	  if (sequence<packet_head) { // Got a packet earlier than the head
	    packet_oo++;
	    } else {
	    packet_head = sequence;
	    }
	  npacket++;
	}
      }
    }

    // Replace missing packets with fill data
    int nmiss = 0;
    for (unsigned int i=0; i<segmentsize; i++) {
      if (!packets_arrived[i]) {
	//cinfo << startl << "***** Dropped " << packet_segmentstart+i << "(" << i << ")" << endl;
	packet_drop++; // Will generally count dropped first packet twice
	nmiss++;
	if (i==0) {
	  memcpy(ptr, invalid_buf, udp_offset); // CHECK INITIAL FULL OR EMPTY BUFFER
	} else if (i==packet_segmentend-packet_segmentstart) {
	  memcpy(ptr+udp_offset+(i-1)*udpsize,invalid_buf,(bytestoread-udp_offset)%udpsize);	  
	} else {
	  memcpy(ptr+udp_offset+(i-1)*udpsize,invalid_buf,udpsize);	  
	}
      }
    }
    //cinfo << startl << "**** Datastream " << mpiid << " missing " << nmiss << "/" << segmentsize << " packets" << endl;

    packet_segmentstart = next_segmentstart;
    udp_offset = next_udpoffset;

  }
  return(1);
}

// This is almost identical to initialiseFile. The two should maybe be combined
void Mk5DataStream::initialiseNetwork(int configindex, int buffersegment)
{
  int offset;
  char *ptr;
  struct mark5_stream *mark5stream;
  int nbits, nrecordedbands, framebytes, fanout;
  Configuration::dataformat format;
  double bw;

  format = config->getDataFormat(configindex, streamnum);
  nbits = config->getDNumBits(configindex, streamnum);
  nrecordedbands = config->getDNumRecordedBands(configindex, streamnum);
  framebytes = config->getFrameBytes(configindex, streamnum);
  bw = config->getDRecordedBandwidth(configindex, streamnum, 0);

  fanout = config->genMk5FormatName(format, nrecordedbands, bw, nbits, framebytes, config->getDDecimationFactor(configindex, streamnum), formatname);
  //cinfo << startl << "******* validbytes " << bufferinfo[buffersegment].validbytes << endl;

  //cinfo << startl << "DataStream " << mpiid << ": Create a new Mark5 stream " << formatname << endl;

  mark5stream = new_mark5_stream(
    new_mark5_stream_memory(&databuffer[buffersegment*(bufferbytes/numdatasegments)], 
			    bufferinfo[buffersegment].validbytes),
    new_mark5_format_generic_from_string(formatname) );

  if (mark5stream==0) {
    cwarn << startl << "DataStream " << mpiid << ": Warning - Could not identify Mark5 segment time (" << formatname << ")" << endl;
    // We don't need to actually set the time - it ill just be deadreckoned from that last segment. As there is no good data this does not matter
  } else {

    if(mark5stream->nchan != config->getDNumRecordedBands(configindex, streamnum))
    {
      cerror << startl << "Number of recorded bands for datastream " << streamnum << " (" << nrecordedbands << ") does not match with MkV data " << " (" << mark5stream->nchan << "), will be ignored!!!" << endl;
    }

    // resolve any day ambiguities
    mark5_stream_fix_mjd(mark5stream, corrstartday);

    if (configindex != lastconfig) {
      mark5_stream_print(mark5stream);
      lastconfig = configindex;
    }

    offset = mark5stream->frameoffset;

    // If offset is non-zero we need to shuffle the data in memory to align on a frame
    // boundary. This is the equivalent of a seek in the file case.
    if (offset>0) { 
      ptr = (char*)&databuffer[buffersegment*(bufferbytes/numdatasegments)];

      if (offset>bufferinfo[buffersegment].validbytes) {
	cerror << startl << "Datastream " << mpiid << ": ERROR Mark5 offset (" << offset << ") > segment size (" << bufferinfo[buffersegment].validbytes << "!!!" << endl;
	keepreading=false;
      } else {
	int nread, status;
	cinfo << startl << "************Datastream " << mpiid << ": Seeking " << offset << " bytes into stream. " << endl;
	memmove(ptr, ptr+offset, bufferinfo[buffersegment].validbytes-offset);
	status = readnetwork(socketnumber, ptr+bufferinfo[buffersegment].validbytes-offset, offset, &nread);
	
	// We *should not* update framebytes remaining (or validbyes). 

	if (status==-1) { // Error reading socket
	  cerror << startl << "Datastream " << mpiid << ": Error reading socket" << endl;
	  keepreading=false;
	} else if (status==0) {  // Socket closed remotely
	  keepreading=false;
	} else if (nread!=offset) {
	  cerror << startl << "Datastream " << mpiid << ": Did not read enough bytes" << endl;
	  keepreading=false;
	}
      }
    }

    readseconds = 86400*(mark5stream->mjd-corrstartday) + mark5stream->sec-corrstartseconds + intclockseconds;
    readnanoseconds = mark5stream->ns;
    while(readscan < (model->getNumScans()-1) && model->getScanEndSec(readscan, corrstartday, corrstartseconds) < readseconds)
      readscan++;
    while(readscan > 0 && model->getScanStartSec(readscan, corrstartday, corrstartseconds) > readseconds)
      readscan--;
    readseconds = readseconds - model->getScanStartSec(readscan, corrstartday, corrstartseconds);
    cinfo << startl << "DataStream " << mpiid << ": The frame start day is " << mark5stream->mjd << ", the frame start seconds is " << mark5stream->sec << ", the frame start ns is " << mark5stream->ns << ", readseconds is " << readseconds << ", readnanoseconds is " << readnanoseconds << endl;

    delete_mark5_stream(mark5stream);
  }

}

double tim(void) {
  struct timeval tv;
  double t;

  gettimeofday(&tv, NULL);
  t = (double)tv.tv_sec + (double)tv.tv_usec/1000000.0;

  return t;
}

int Mk5DataStream::openframe()
{
  // The number of segments per "frame" is arbitrary. Just set it to ~ 1sec
  int nsegment;
  nsegment = (int)(1.0e9/bufferinfo[0].nsinc+0.1);

  if (!tcp) {
    // Print statistics. 
    double delta, thistime;
    float dropped, oo, dup, rate;

    thistime = tim();
    delta = thistime-lasttime;
    lasttime = thistime;

    if (npacket==0) {
      dropped = 0;
      oo = 0;
      dup = 0;
    } else {
      dropped = (float)packet_drop*100.0/(float)npacket;
      oo = (float)packet_oo*100.0/(float)npacket;
      dup = (float)packet_duplicate*100.0/(float)npacket;
    }
    rate = packet_sum/delta*8/1e6;
    
    if (packet_head!=0) {
      cinfo << fixed << setprecision(2);
      cinfo << startl << "Datastream " << mpiid << ": Packets=" << npacket << "  Dropped=" << dropped
	   << "  Duplicated=" << dup << "  Out-of-order=" << oo << "  Rate=" << rate << " Mbps" << endl;
      // Should reset precision
    }
    packet_drop = 0;
    packet_oo = 0;
    packet_duplicate = 0;
    npacket = 0;
    packet_sum = 0;
  }


  return readbytes*nsegment;
}
