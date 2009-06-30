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
#ifndef DATASTREAM_H
#define DATASTREAM_H

#include <mpi.h>
#include <string>
#include <fstream>
#include <vector>
#include <iostream>
#include <pthread.h>
#include "architecture.h"
#include "configuration.h"

using namespace std;

/**
@class DataStream 
@brief Loads data into memory from a disk or network connection, calculates geometric delays and sends data to Cores

This class manages a stream of data from a disk or memory, coarsely aligning it with the geocentre and sending segments of 
data to Core nodes for processing as directed by the FxManager.  Defaults are for LBA-style file and frame headers - the
appropriate methods are virtual so Datastream can be subclassed to give altered functionality for different data formats

@author Adam Deller
*/
class DataStream{
public:
 /**
  * Constructor: Copies the information passed to it - does not do other initialisation as it can be subclassed and different functionality is needed
  * @param conf The configuration object, containing all information about the duration and setup of this correlation
  * @param snum This Datastream's index (numbered from 0)
  * @param id This Datastream's MPI id
  * @param ncores The number of Cores in this correlation
  * @param cids Array containing the MPI ids of each Core
  * @param bufferfactor The size of the buffer, in terms of number of "max send sizes" - the biggest "blocks per send*numchannels" from the possible configurations
  * @param numsegments The number of separate segments this buffer will be divided into
  */
  DataStream(Configuration * conf, int snum, int id, int ncores, int * cids, int bufferfactor, int numsegments);

  virtual ~DataStream();

 /**
  * Creates all arrays, initialises the reading thread and loads delays from the precomputed delay file
  */
  virtual void initialise();

 /**
  * While the correlation continues, keep accepting control information from the FxManager and sending data to the appropriate
  * Cores, while maintaining fresh data in the buffer
  */
  void execute();

protected:
  /// Structure which maintains all information necessary for a segment of the databuffer, including the configuration parameters for
  /// that time interval and the MPI requests for the non-blocking communications that originated in this segment
  typedef struct {
    int scan;
    int scanseconds;
    int scanns;
    int configindex;
    int validbytes;
    int sendbytes;
    int controllength;
    int bytesbetweenintegerns;
    bool readto;
    int blockspersend;
    int bytespersamplenum;
    int bytespersampledenom;
    int nsinc;
    int numchannels;
    int numsent;
    double sampletimens;
    MPI_Request * datarequests;
    MPI_Request * controlrequests;
    s32 ** controlbuffer;
  } readinfo;

 /** 
  * Launches a new reading thread, that will read from a file on disk and populate the databuffer as fast as possible
  * @param thisstream Pointer to this Datastream object
  */
  static void * launchNewFileReadThread(void * thisstream);

 /** 
  * Launches a new reading thread, that will read from a network socket and populate the databuffer as fast as possible
  * @param thisstream Pointer to this Datastream object
  */
  static void * launchNewNetworkReadThread(void * thisstream);

 /** 
  * Updates all the parameters (numchannels, sendbytes etc) for the specified segment of the databuffer
  * @param segmentindex The index of the segment to be updated
  */
  virtual void updateConfig(int segmentindex);

 /** 
  * Reads in the header information from an (LBA-style) file and sets the current segment time information accordingly
  * Should take into account intclockseconds when setting the file time and readseconds
  * @param configindex The config index at the current time
  * @param fileindex The number of the file to be opened
  */
  virtual void initialiseFile(int configindex, int fileindex);

 /** 
  * Reads in the header information from a buffer read in from the network, and sets the current segment time information accordingly
  * Should take into account intclockseconds when setting the file time and readseconds
  * Analogous to initialiseFile
  * @param frameheader Buffer containing the frame header
  * @return 0 on success, -1 on failure
  */
  virtual int initialiseFrame(char * frameheader);

 /** 
  * Calculates the correct offset from the start of the databuffer for a given time in the correlation, 
  * and calculates valid bits for each FFT block as control information to pass to the Cores
  * @param scan The scan to calculate for
  * @param offsetsec The offset in seconds from the start of the scan
  * @param offsetns The offset in nanoseconds from the given second
  * @return The offset in bytes from the start of the databuffer that this block should start from - must be between 0 and bufferlength
  */
  virtual int calculateControlParams(int scan, int offsetsec, int offsetns);
  
  const int databufferfactor, numdatasegments;
  int streamnum, atsegment, readscan, readseconds, corrstartday, corrstartseconds, readbytes, bufferbytes, readnanoseconds, intclockseconds;
  bool dataremaining;
  readinfo * bufferinfo;
  Configuration * config;
  Model * model;
  string ** datafilenames;
  ifstream input;
  
  static const int LBA_HEADER_LENGTH = 4096;
 
 /** 
  * Attempts to open the specified file for reading
  * @param configindex The config index at the current time
  * @param fileindex The number of the file to be opened
  */
  virtual void openfile(int configindex, int fileindex);

 /** 
  * Attempts to open the specified file and peeks at what scan it belongs to
  * @param configindex The config index at the current time
  * @param fileindex The number of the file to be opened
  * @return The scan that the start of the file belongs to
  */
  virtual int peekfile(int configindex, int fileindex);

 /** 
  * Attempts to open the next frame by reading data from the open network socket
  * @return 0 on failure, otherwise the framesize in bytes
  */
  virtual int openframe();

 /** 
  * Attempts to open a network socket to read data from
  * @param portnumber The port to read from
  * @param tcpwindowsize The size of the packets to receive, in KB
  */
  void openstream(int portnumber, int tcpwindowsize);

 /** 
  * Attempts to close a network socket
  */
  void closestream();

 /** 
  * While the correlation continues and there are more files to be read, continues reading data into the databuffer as fast as possible
  */
  virtual void loopfileread();

 /** 
  * While the correlation continues and the network socket remains open, continues reading data into the databuffer as fast as possible
  */
  void loopnetworkread();

 /** 
  * Reads one segment's worth of data from the currently open file into the specified segment
  * @param buffersegment The segment of the databuffer that this read will be stored in
  */
  void diskToMemory(int buffersegment);

 /** 
  * Reads one segment's worth of data from the currently open network socket into the specified segment
  * @param buffersegment The segment of the databuffer that this read will be stored in
  * @param framebytesremaining The number of bytes left in this frame before reading a new frame is necessary
  */
  virtual void networkToMemory(int buffersegment, int & framebytesremaining);

 /** 
  * Reads the specified number of bytes from the specified socket into the provided buffer
  * @param sock The network socket being read from
  * @param ptr The buffer to store read data in
  * @param bytestoread The number of bytes to read from the socket
  * @param nread The number of bytes actually read
  */
  virtual int readnetwork(int sock, char* ptr, int bytestoread, int* nread);

 /**
  * Launches the read thread (whether from file or network) and waits until it has initialised itself
  */
  void initialiseMemoryBuffer();

 /**
  * Depending on the relative positions of the reading thread and the main (sending) thread in the databuffer, either tests to see
  * if all sends from the segment containing oldest data have completed, or actually waits until they have
  */
  void waitForSendComplete();

 /**
  * Waits until all sends from the given buffer segment have been completed, using MPI_Wait
  * @param buffersegment The segment of the databuffer to wait on
  */
  void waitForBuffer(int buffersegment);

  string stationname;
  int mpiid, delaystartday, delaystartseconds, filestartday, filestartseconds, numcores, numsent, delayincms, lastnearestindex, lastscan, lastvalidsegment, totaldelays, maxsendspersegment, waitsegment, portnumber, tcpwindowsizebytes, socketnumber;
  int * coreids;
  int * filesread;
  int * confignumfiles;
  double a, b, c, clockoffset, clockrate;
  bool readthreadstarted, keepreading, readfromfile, tcp;
  u8 * databuffer;
  pthread_t readerthread;
  pthread_cond_t readcond;
  pthread_cond_t initcond;
  pthread_mutex_t * bufferlock;
  MPI_Status * datastatuses;
  MPI_Status * controlstatuses;
};

#endif
