/***************************************************************************
 *   Copyright (C) 2007 by Walter Brisken and Adam Deller                  *
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
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include "config.h"
#include "nativemk5.h"
#ifdef HAVE_DIFXMESSAGE
#include <difxmessage.h>
#endif

#define FILL_PATTERN 0x11223344

#define u32 uint32_t

static void dirCallback(int scan, int nscan, int status, void *data)
{
	char progressSymbols[] = "@?!.";
#ifdef HAVE_DIFXMESSAGE
	DifxMessageMk5Status *mk5status;

	mk5status = (DifxMessageMk5Status *)data;
	mk5status->scanNumber = scan;
	mk5status->position = nscan;
	sprintf(mk5status->scanName, "%s", Mark5DirDescription[status]);
	difxMessageSendMark5Status(mk5status);
#endif

	if(status == 3)
	{
		printf(".");
	}
	else
	{
		printf("%c[%d]", progressSymbols[status], scan);
	}
}

NativeMk5DataStream::NativeMk5DataStream(Configuration * conf, int snum, 
	int id, int ncores, int * cids, int bufferfactor, int numsegments) :
		Mk5DataStream(conf, snum, id, ncores, cids, bufferfactor, 
	numsegments)
{
	XLR_RETURN_CODE xlrRC;

	/* each data buffer segment contains an integer number of frames, 
	 * because thats the way config determines max bytes
	 */

	executeseconds = conf->getExecuteSeconds();

#ifdef HAVE_DIFXMESSAGE
	sendMark5Status(MARK5_STATE_OPENING, 0, 0, 0.0, 0.0);
#endif

	cout << "Opening Streamstor [" << mpiid << "]" << endl;
	xlrRC = XLROpen(1, &xlrDevice);
  
  	if(xlrRC == XLR_FAIL)
	{
		XLRClose(xlrDevice);
		cerr << "Error opening Streamstor device [" << mpiid << "]" << endl;
		cerr << "Do you have permission +rw for /dev/windrvr6?" << endl;
#ifdef HAVE_DIFXMESSAGE
		difxMessageSendDifxError("Failure opening Streamstor device", 0);
#endif
		exit(1);
	}
	else
	{
		cerr << "Success opening Streamstor device [" << mpiid << "]" <<  endl;
	}

	xlrRC = XLRSetOption(xlrDevice, SS_OPT_SKIPCHECKDIR);
	if(xlrRC == XLR_SUCCESS)
	{
		xlrRC = XLRSetOption(xlrDevice, SS_OPT_REALTIMEPLAYBACK);
	}
	if(xlrRC == XLR_SUCCESS)
	{
		xlrRC = XLRSetFillData(xlrDevice, FILL_PATTERN);
	}
	if(xlrRC != XLR_SUCCESS)
	{
		cerr << "Error setting data replacement mode / fill pattern" << endl;
	}

	module.nscans = -1;
	readpointer = -1;
	scan = 0;
	lastval = 0xFFFFFFFF;
	mark5stream = 0;
	invalidtime = 0;
	invalidstart = 0;
	newscan = 0;
#ifdef HAVE_DIFXMESSAGE
	sendMark5Status(MARK5_STATE_OPEN, 0, 0, 0.0, 0.0);
#endif
}

NativeMk5DataStream::~NativeMk5DataStream()
{
	if(mark5stream)
	{
		delete_mark5_stream(mark5stream);
	}
#ifdef HAVE_DIFXMESSAGE
	sendMark5Status(MARK5_STATE_CLOSE, 0, 0, 0.0, 0.0);
#endif
	XLRClose(xlrDevice);
}

/* Here "File" is VSN */
void NativeMk5DataStream::initialiseFile(int configindex, int fileindex)
{
	double startmjd;
	double scanstart, scanend;
	int v, i;
	int scanns;
	long long n;
	int doUpdate = 0;
	char *mk5dirpath;
	char message[64];
	char formatname[64];
	int nbits, ninputbands, framebytes;
	Configuration::dataformat format;
	double bw;

	format = config->getDataFormat(configindex, streamnum);
	nbits = config->getDNumBits(configindex, streamnum);
	ninputbands = config->getDNumInputBands(configindex, streamnum);
	framebytes = config->getFrameBytes(configindex, streamnum);
	bw = config->getConfigBandwidth(configindex);
	genFormatName(format, ninputbands, bw, nbits, framebytes, config->getDecimationFactor(configindex), formatname);

	if(mark5stream)
	{
		delete_mark5_stream(mark5stream);
	}
	mark5stream = new_mark5_stream(
	  new_mark5_stream_unpacker(0),
	  new_mark5_format_generic_from_string(formatname) );

	mk5dirpath = getenv("MARK5_DIR_PATH");
	if(mk5dirpath == 0)
	{
		mk5dirpath = ".";
	}

	startmjd = corrstartday + corrstartseconds/86400.0;

#ifdef HAVE_DIFXMESSAGE
	sendMark5Status(MARK5_STATE_GETDIR, 0, 0, startmjd, 0.0);
#endif

	if(module.nscans < 0)
	{
		doUpdate = 1;
		cout << "getting module info" << endl;
		v = getCachedMark5Module(&module, xlrDevice, corrstartday, 
			datafilenames[configindex][fileindex].c_str(), 
			mk5dirpath, &dirCallback, &mk5status);
		cout << endl;

		if(v < 0)
		{
			cerr << "Module " << 
				datafilenames[configindex][fileindex] << 
				" not found in unit - aborting!!!" << endl;
#ifdef HAVE_DIFXMESSAGE
			sprintf(message, "Module %s not found in unit", datafilenames[configindex][fileindex].c_str());
			difxMessageSendDifxError(message, 0);
#endif
			dataremaining = false;
			return;
		}
	}

#ifdef HAVE_DIFXMESSAGE
	sendMark5Status(MARK5_STATE_GOTDIR, 0, 0, startmjd, 0.0);
#endif

	// find starting position
  
	if(scan != 0)  /* just continue by reading next valid scan */
	{
		cout << "NM5 : continuing read in next scan" << endl;
		do
		{
			scan++;
		} while(scan-module.scans < module.nscans && scan->duration < 0.1);
		if(scan-module.scans >= module.nscans)
		{
			cerr << "No more data on module [" << mpiid << "]" << endl;
			scan = 0;
			dataremaining = false;
			keepreading = false;
#ifdef HAVE_DIFXMESSAGE
			sendMark5Status(MARK5_STATE_NOMOREDATA, 0, 0, 0.0, 0.0);
#endif
			return;
		}
		scanns = int(1000000000.0*scan->framenuminsecond/scan->framespersecond + 0.1);
		cout << "Before[" << mpiid << "] rs = " << readseconds << "  rns = " << readnanoseconds << endl;
		scanstart = scan->mjd + (scan->sec + scanns*1.e-9)/86400.0;
		scanend = scanstart + scan->duration/86400.0;
		readpointer = scan->start + scan->frameoffset;
		readseconds = (scan->mjd-corrstartday)*86400 + scan->sec - corrstartseconds;
		readnanoseconds = scanns;

		cout << "After[" << mpiid << "]  rs = " << readseconds << "  rns = " << readnanoseconds << endl;

		if(readseconds > executeseconds)
		{
			cerr << "No more data for project on module [" << mpiid << "]" << endl;
			scan = 0;
			dataremaining = false;
			keepreading = false;
#ifdef HAVE_DIFXMESSAGE
			sendMark5Status(MARK5_STATE_NOMOREDATA, 0, 0, 0.0, 0.0);
#endif
			return;
		}
	}
	else	/* first time this project */
	{
		n = 0;
		for(i = 0; i < module.nscans; i++)
		{
			double scanstart, scanend;
			scan = module.scans + i;
			scanns = int(1000000000.0*scan->framenuminsecond/scan->framespersecond + 0.1);
			scanstart = scan->mjd + (scan->sec + scanns*1.e-9)/86400.0;
			scanend = scanstart + scan->duration/86400.0;

			if(startmjd < scanstart)  /* obs starts before data */
			{
				cout << "NM5 : scan found(1) : " << i << endl;
				readpointer = scan->start + scan->frameoffset;
				readseconds = (scan->mjd-corrstartday)*86400 
					+ scan->sec - corrstartseconds;
				readnanoseconds = scanns;
				break;
			}
			else if(startmjd < scanend) /* obs starts within data */
			{
				cout << "NM5 : scan found(2) : " << i << endl;
				readpointer = scan->start + scan->frameoffset;
				n = (long long)((((corrstartday - scan->mjd)*86400 
			+ (corrstartseconds - (scan->sec + scanns*1.e-9)))
					*framespersecond) + 0.5);
				readpointer += n*scan->framebytes;
				readseconds = 0;
				readnanoseconds = 0;
				break;
			}
		}
		cout << "NativeMk5DataStream " << mpiid << 
			" positioned at byte " << readpointer << 
			" seconds = " << readseconds <<" ns = " << 
			readnanoseconds << " n = " << n << endl;

		if(i >= module.nscans || scan == 0)
		{
			cerr << "No valid data found - aborting\n";
			dataremaining = false;
#ifdef HAVE_DIFXMESSAGE
			sendMark5Status(MARK5_STATE_NODATA, 0, 0, 0.0, 0.0);
#endif
			return;
		}

		cout << "Scan info. start = " << scan->start << " off = " << scan->frameoffset << " size = " << scan->framebytes << endl;
	}

#ifdef HAVE_DIFXMESSAGE
	sendMark5Status(MARK5_STATE_GOTDIR, scan-module.scans+1, readpointer, scan->mjd+(scan->sec+scanns*1.e-9)/86400.0, 0.0);
#endif

	cout << "The frame start day is " << scan->mjd << 
		", the frame start seconds is " << (scan->sec+scanns*1.e-9)
		<< ", readseconds is " << readseconds << 
		", readnanoseconds is " << readnanoseconds << endl;

	/* update all the configs - to ensure that the nsincs and 
	 * headerbytes are correct
	 */
	if(doUpdate)
	{
		cout << "Updating all configs [" << mpiid << "]" << endl;
		for(i = 0; i < numdatasegments; i++)
		{
			updateConfig(i);
		}
	}
	else
	{
		cout << "NOT updating all configs [" << mpiid << "]" << endl;
	}

	newscan = 1;

	cout << "Scan " << (scan-module.scans) <<" initialised[" << mpiid << "]" << endl;
}

void NativeMk5DataStream::openfile(int configindex, int fileindex)
{
	cout << "NativeMk5DataStream " << mpiid << 
		" is about to look at a scan" << endl;

	/* fileindex should never increase for native mark5, but
	 * check just in case. 
	 */
	if(fileindex >= confignumfiles[configindex])
	{
		dataremaining = false;
		keepreading = false;
		cout << "NativeMk5DataStream " << mpiid << 
			" is exiting because fileindex is " << fileindex << 
			", while confignumconfigfiles is " << 
			confignumfiles[configindex] << endl;
		return;
	}

	dataremaining = true;

	initialiseFile(configindex, fileindex);
}

void NativeMk5DataStream::moduleToMemory(int buffersegment)
{
	long long start;
	unsigned long *buf, *data;
	unsigned long a, b;
	int i, t;
	S_READDESC      xlrRD;
	XLR_RETURN_CODE xlrRC;
	XLR_ERROR_CODE  xlrEC;
	XLR_READ_STATUS xlrRS;
	int bytes;
	char errStr[XLR_ERROR_LENGTH];
	static int now = 0;
	static long long lastpos = 0;
	struct timeval tv;
	char message[1000];
	int mjd, sec, sec2;
	double ns, ms;

	/* All reads of a module must be 64 bit aligned */
	bytes = readbytes;
	start = readpointer;
	data = buf = (unsigned long *)&databuffer[(buffersegment*bufferbytes)/
		numdatasegments];
	if(start & 4)
	{
		start += 4;
		*buf = lastval;
		buf++;
	}

	// we're starting after the end of the scan.  Just set flags and return
	if(start >= scan->start + scan->length)
	{
		bufferinfo[buffersegment].validbytes = 0;
		dataremaining = false;
		return;
	}

	if(start + bytes > scan->start + scan->length)
	{
		bytes = scan->start + scan->length - start;
		cout << "[" << mpiid << "] shortening read to only " << bytes << " bytes ";
		cout << "(was " << readbytes << ")" << endl;
	}

	/* always read multiples of 8 bytes */
	bytes &= ~7;

	a = start >> 32;
	b = start & 0xFFFFFFFF; 

	waitForBuffer(buffersegment);

	xlrRD.AddrHi = a;
	xlrRD.AddrLo = b;
	xlrRD.XferLength = bytes;
	xlrRD.BufferAddr = buf;

	for(t = 0; t < 2; t++)
	{
		xlrRC = XLRReadImmed(xlrDevice, &xlrRD);

		if(xlrRC != XLR_SUCCESS)
		{
			xlrEC = XLRGetLastError();
			XLRGetErrorMessage(errStr, xlrEC);
			cout << "XLRReadImmed returns FAIL.  Error msg: " << errStr << endl;
#ifdef HAVE_DIFXMESSAGE
			sprintf(message, "Read error at position=%lld, length=%d, error=%s", readpointer, bytes, errStr);
			difxMessageSendDifxError(message, 0);
#endif
			break;
		}

		/* Wait up to 10 seconds for a return */
		for(i = 1; i < 100000; i++)
		{
			xlrRS = XLRReadStatus(0);
			if(xlrRS == XLR_READ_COMPLETE)
			{
				break;
			}
			else if(xlrRS == XLR_READ_ERROR)
			{
				cerr << "XXX:" << a << ":" << b << "  rp = " << readpointer << endl;
				xlrEC = XLRGetLastError();
				XLRGetErrorMessage(errStr, xlrEC);
				cerr << "NativeMk5DataStream " << mpiid << 
					" XLRReadData error: " << errStr << endl; 
#ifdef HAVE_DIFXMESSAGE
				sprintf(message, "Read error at position=%lld, length=%d, error=%s", readpointer, bytes, errStr);
				difxMessageSendDifxError(message, 0);
#endif

				dataremaining = false;
				keepreading = false;
				bufferinfo[buffersegment].validbytes = 0;
				return;
			}
			if(i % 10000 == 0)
			{
				cout << "[" << mpiid << "] Waited " << i << " microsec  state = "; 
				if(xlrRS == XLR_READ_WAITING)
				{
					cout << "XLR_READ_WAITING" << endl;
				}
				if(xlrRS == XLR_READ_RUNNING)
				{
					cout << "XLR_READ_RUNNING" << endl;
				}
				else
				{
					cout << "XLR_READ_OTHER " << endl;
				}
			}
			usleep(100);
		}
		if(xlrRS == XLR_READ_COMPLETE)
		{
			break;
		}
		else if(t == 0)
		{
			cout << "[" << mpiid << "]  XLRClose() being called!" << endl;
			XLRClose(xlrDevice);
			
			cout << "[" << mpiid << "]  XLRCardReset() being called!" << endl;
			xlrRC = XLRCardReset(1);
			cout << "[" << mpiid << "]  XLRCardReset() called! " << xlrRC << endl;

			cout << "[" << mpiid << "]  XLROpen() being called!" << endl;
			xlrRC = XLROpen(1, &xlrDevice);
			cout << "[" << mpiid << "]  XLROpen() called! " << xlrRC << endl;
		}
	}

	if(xlrRS != XLR_READ_COMPLETE)
	{
		cerr << "Native Mark5 Error at:" << a << ":" << b << "  rp = " << readpointer << endl;
		cerr << "[" << mpiid << "] Waited 10 seconds for a read and gave up" << endl;
#ifdef HAVE_DIFXMESSAGE
		sprintf(message, "Read error at position=%lld, length=%d.  Dropping out.", readpointer, bytes);
		difxMessageSendDifxError(message, 0);
#endif
		dataremaining = false;
		keepreading = false;
		bufferinfo[buffersegment].validbytes = 0;
		return;
	}

	bufferinfo[buffersegment].validbytes = bytes;
	lastval = buf[bytes/4-1];

	// Check for validity
	mark5stream->frame = (uint8_t *)data;
	mark5_stream_get_frame_time(mark5stream, &mjd, &sec, &ns);
	mark5stream->frame = 0;
	sec2 = (readseconds + corrstartseconds) % 86400;

	if(sec != sec2 || fabs(ns - readnanoseconds) > 0.5)
	{
		invalidtime++;
		invalidstart = readpointer;
		bufferinfo[buffersegment].validbytes = 0;
		cerr << "[" << mpiid << "] Sync error: ("<<sec<<","<<ns<<") != ("<<sec2<<","<<readnanoseconds<<")"<<endl;
		printf("sync error: %d %f %d\n", mpiid, ns, readnanoseconds);
#ifdef HAVE_DIFXMESSAGE
		if(invalidtime % 16 == 0)
		{
			sprintf(message, "%d consecutive sync errors starting at readpos %lld.", invalidtime, invalidstart);
			difxMessageSendDifxError(message, 0);
		}
#endif
		// FIXME -- if invalidtime > threshhold, look for sync again
	}
	else
	{
		invalidtime = 0;
	}


#ifdef HAVE_DIFXMESSAGE
	gettimeofday(&tv, 0);
	if(tv.tv_sec > now)
	{
		now = tv.tv_sec;
		if(lastpos > 0)
		{
			double rate;
			double fmjd, fmjd2;
			enum Mk5State state;

			fmjd = corrstartday + (corrstartseconds + readseconds + (double)readnanoseconds/1000000000.0)/86400.0;
			if(newscan > 0)
			{
				newscan = 0;
				state = MARK5_STATE_START;
				rate = 0.0;
				fmjd2 = scan->mjd + (scan->sec + (float)scan->framenuminsecond/scan->framespersecond)/86400.0;
				if(fmjd2 > fmjd)
				{
					fmjd = fmjd2;
				}
			}
			else if(invalidtime == 0)
			{
				state = MARK5_STATE_PLAY;
				rate = (double)(readpointer + bytes - lastpos)*8.0/1000000.0;
			}
			else
			{
				state = MARK5_STATE_PLAYINVALID;
				rate = invalidtime;
			}

			sendMark5Status(state, scan-module.scans+1, readpointer, fmjd, rate);
		}
		lastpos = readpointer + bytes;
	}
#endif

	// Update various counters
	readnanoseconds += bufferinfo[buffersegment].nsinc;
	readseconds += readnanoseconds/1000000000;
	readnanoseconds %= 1000000000;
	if(bytes < readbytes)
	{
		dataremaining = false;
	}
	else
	{
		readpointer += bytes;
	}
	if(readpointer >= scan->start + scan->length)
	{
		dataremaining = false;
	}
}

void NativeMk5DataStream::loopfileread()
{
  int perr;
  int numread = 0;
  char message[1024];

  cout << "NM5 : loopfileread starting" << endl;

  //lock the first section to start reading
  openfile(bufferinfo[0].configindex, 0);
  moduleToMemory(numread++);
  moduleToMemory(numread++);
  perr = pthread_mutex_lock(&(bufferlock[numread]));
  if(perr != 0)
    cerr << "Error in initial telescope readthread lock of first buffer section!!!" << endl;
  readthreadstarted = true;
  perr = pthread_cond_signal(&initcond);
  if(perr != 0)
    cerr << "NativeMk5DataStream readthread " << mpiid << " error trying to signal main thread to wake up!!!" << endl;
  moduleToMemory(numread++);

  lastvalidsegment = (numread-1)%numdatasegments;
  while((bufferinfo[lastvalidsegment].configindex < 0 || filesread[bufferinfo[lastvalidsegment].configindex] <= confignumfiles[bufferinfo[lastvalidsegment].configindex]) && keepreading)
  {
    while(dataremaining && keepreading)
    {
      lastvalidsegment = (lastvalidsegment + 1)%numdatasegments;
      
      //lock the next section
      perr = pthread_mutex_lock(&(bufferlock[lastvalidsegment]));
      if(perr != 0)
        cerr << "Error in telescope readthread lock of buffer section!!!" << lastvalidsegment << endl;

      //unlock the previous section
      perr = pthread_mutex_unlock(&(bufferlock[(lastvalidsegment-1+numdatasegments)% numdatasegments]));
      if(perr != 0)
        cerr << "Error in telescope readthread unlock of buffer section!!!" << (lastvalidsegment-1+numdatasegments)%numdatasegments << endl;

      //do the read
      moduleToMemory(lastvalidsegment);
      numread++;
    }
    if(keepreading)
    {
      //if we need to, change the config
      int nextconfigindex = config->getConfigIndex(readseconds);
      cout << "old config[" << mpiid << "] = " << nextconfigindex << endl;
      while(nextconfigindex < 0 && readseconds < config->getExecuteSeconds())
        nextconfigindex = config->getConfigIndex(++readseconds);
      if(readseconds >= config->getExecuteSeconds())
      {
        keepreading = false;
      }
      else
      {
        if(config->getConfigIndex(readseconds) != bufferinfo[(lastvalidsegment + 1)%numdatasegments].configindex)
          updateConfig((lastvalidsegment + 1)%numdatasegments);
	//if the datastreams for two or more configs are common, they'll all have the same 
        //files.  Therefore work with the lowest one
        int lowestconfigindex = bufferinfo[(lastvalidsegment+1)%numdatasegments].configindex;
        for(int i=config->getNumConfigs()-1;i>=0;i--)
        {
          if(config->getDDataFileNames(i, streamnum) == config->getDDataFileNames(lowestconfigindex, streamnum))
            lowestconfigindex = i;
        }
        openfile(lowestconfigindex, filesread[lowestconfigindex]);
      }
      if(keepreading == false)
      {
        bufferinfo[(lastvalidsegment+1)%numdatasegments].seconds = config->getExecuteSeconds();
        bufferinfo[(lastvalidsegment+1)%numdatasegments].nanoseconds = 0;
      }
    }
  }
  perr = pthread_mutex_unlock(&(bufferlock[lastvalidsegment]));
  if(perr != 0)
    cerr << "Error in telescope readthread unlock of buffer section!!!" << lastvalidsegment << endl;

  cout << "DATASTREAM " << mpiid << "'s readthread is exiting!!! Filecount was " << filesread[bufferinfo[lastvalidsegment].configindex] << ", confignumfiles was " << confignumfiles[bufferinfo[lastvalidsegment].configindex] << ", dataremaining was " << dataremaining << ", keepreading was " << keepreading << endl;
}

#ifdef HAVE_DIFXMESSAGE
int NativeMk5DataStream::sendMark5Status(enum Mk5State state, int scanNum, long long position, double dataMJD, float rate)
{
	int v = 0;
	char message[1000];
	S_BANKSTATUS A, B;
	XLR_RETURN_CODE xlrRC;

	mk5status.state = state;
	mk5status.status = 0;
	mk5status.activeBank = ' ';
	mk5status.position = position;
	mk5status.rate = rate;
	mk5status.dataMJD = dataMJD;
	mk5status.scanNumber = scanNum;
	if(scan)
	{
		strcpy(mk5status.scanName, scan->name);
	}
	else
	{
		strcpy(mk5status.scanName, "none");
	}
	if(state != MARK5_STATE_OPENING && state != MARK5_STATE_ERROR)
	{
		xlrRC = XLRGetBankStatus(xlrDevice, BANK_A, &A);
		if(xlrRC == XLR_SUCCESS)
		{
			xlrRC = XLRGetBankStatus(xlrDevice, BANK_B, &B);
		}
		if(xlrRC == XLR_SUCCESS)
		{
			strncpy(mk5status.vsnA, A.Label, 8);
			mk5status.vsnA[8] = 0;
			if(strncmp(mk5status.vsnA, "LABEL NO", 8) == 0)
			{
				strcpy(mk5status.vsnA, "none");
			}
			strncpy(mk5status.vsnB, B.Label, 8);
			mk5status.vsnB[8] = 0;
			if(strncmp(mk5status.vsnB, "LABEL NO", 8) == 0)
			{
				strcpy(mk5status.vsnB, "none");
			}
			if(A.Selected)
			{
				mk5status.activeBank = 'A';
				mk5status.status |= 0x100000;
			}
			if(A.State == STATE_READY)
			{
				mk5status.status |= 0x200000;
			}
			if(A.MediaStatus == MEDIASTATUS_FAULTED)
			{
				mk5status.status |= 0x400000;
			}
			if(A.WriteProtected)
			{
				mk5status.status |= 0x800000;
			}
			if(B.Selected)
			{
				mk5status.activeBank = 'B';
				mk5status.status |= 0x1000000;
			}
			if(B.State == STATE_READY)
			{
				mk5status.status |= 0x2000000;
			}
			if(B.MediaStatus == MEDIASTATUS_FAULTED)
			{
				mk5status.status |= 0x4000000;
			}
			if(B.WriteProtected)
			{
				mk5status.status |= 0x8000000;
			}
		}
		if(xlrRC != XLR_SUCCESS)
		{
			mk5status.state = MARK5_STATE_ERROR;
		}
	}
	else
	{
		sprintf(mk5status.vsnA, "???");
		sprintf(mk5status.vsnB, "???");
	}
	switch(mk5status.state)
	{
	case MARK5_STATE_PLAY:
		mk5status.status |= 0x0100;
		break;
	case MARK5_STATE_ERROR:
		mk5status.status |= 0x0002;
		break;
	case MARK5_STATE_IDLE:
		mk5status.status |= 0x0001;
		break;
	default:
		break;
	}

	v = difxMessageSendMark5Status(&mk5status);

	return v;
}
#endif
