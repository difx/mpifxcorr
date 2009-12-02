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
#include <string.h>
#include <climits>
#include "mk5mode.h"
#include "configuration.h"
#include "mode.h"
#include "alert.h"

int Configuration::MONITOR_TCP_WINDOWBYTES;

Configuration::Configuration(const char * configfile, int id)
  : mpiid(id), consistencyok(true)
{
  string configfilestring = configfile;
  int basestart = configfilestring.find_last_of('/');
  if(basestart == string::npos)
    basestart = 0;
  else
    basestart = basestart+1;
  jobname = configfilestring.substr(basestart, string(configfile).find_last_of('.')-basestart);

  sectionheader currentheader = INPUT_EOF;
  commonread = false;
  datastreamread = false;
  configread = false;
  freqread = false;
  ruleread = false;
  baselineread = false;
  maxnumchannels = 0;
  estimatedbytes = 0;
  model = NULL;

  //open the file
  ifstream * input = new ifstream(configfile);
  if(input->fail() || !input->is_open())
  {
    if(mpiid == 0) //only write one copy of this error message
      cfatal << startl << "Error opening file " << configfile << " - aborting!!!" << endl;
    consistencyok = false;
  }
  else
    currentheader = getSectionHeader(input);

  //go through all the sections and tables in the input file
  while(consistencyok && currentheader != INPUT_EOF)
  {
    switch(currentheader)
    {
      case COMMON:
        processCommon(input);
        break;
      case CONFIG:
        if(!commonread)
        {
          if(mpiid == 0) //only write one copy of this error message
            cfatal << startl << "Input file out of order!  Attempted to read configuration details without knowledge of common settings - aborting!!!" << endl;
          consistencyok = false;
        }
        else
          consistencyok = processConfig(input);
        break;
      case RULE:
        if(!configread) {
          if(mpiid == 0) //only write one copy of this error message
            cfatal << startl << "Error - input file out of order!  Attempted to read rule details without knowledge of configurations - aborting!!!" << endl;
          consistencyok = false;
        }
        else
          consistencyok = processRuleTable(input);
        break;
      case FREQ:
        consistencyok = processFreqTable(input);
        break;
      case TELESCOPE:
        processTelescopeTable(input);
        break;
      case DATASTREAM:
        if(!configread || ! freqread)
        {
          if(mpiid == 0) //only write one copy of this error message
            cfatal << startl << "Input file out of order!  Attempted to read datastreams without knowledge of one or both of configs/freqs - aborting!!!" << endl;
          consistencyok = false;
        }
        else
          consistencyok = processDatastreamTable(input);
        break;
      case BASELINE:
        if(!configread || !freqread)
        {
          if(mpiid == 0) //only write one copy of this error message
            cfatal << startl << "Error - input file out of order! Attempted to read baselines without knowledge of freqs - aborting!!!" << endl;
          consistencyok = false;
        }
        consistencyok = processBaselineTable(input);
        break;
      case DATA:
        if(!datastreamread)
        {
          if(mpiid == 0) //only write one copy of this error message
            cfatal << startl << "Input file out of order!  Attempted to read datastream data files without knowledge of datastreams - aborting!!!" << endl;
          consistencyok = false;
        }
        else
          processDataTable(input);
        break;
      case NETWORK:
        if(!datastreamread)
        {
          if(mpiid == 0) //only write one copy of this error message
            cfatal << startl << "Input file out of order!  Attempted to read datastream network details without knowledge of datastreams - aborting!!!" << endl;
          consistencyok = false;
        }
        else
          processNetworkTable(input);
        break;
      default:
        break;
    }
    currentheader = getSectionHeader(input);
  }
  if(!configread || !ruleread || !commonread || !datastreamread || !freqread)
  {
    if(mpiid == 0) //only write one copy of this error message
      cfatal << startl << "Error - no one or more sections missing from input file - aborting!!!" << endl;
    consistencyok = false;
  }
  input->close();
  delete input;
  //work out which frequencies are used in each config, and the minimum #channels
  freqdata freq;
  for(int i=0;i<numconfigs;i++)
  {
    freq = freqtable[getBFreqIndex(i,0,0)];
    configs[i].minpostavfreqchannels = freq.numchannels/freq.channelstoaverage;
    configs[i].frequsedbybaseline = new bool[freqtablelength];
    for(int j=0;j<freqtablelength;j++)
      configs[i].frequsedbybaseline[j] = false;
    for(int j=0;j<numbaselines;j++)
    {
      for(int k=0;k<baselinetable[configs[i].baselineindices[j]].numfreqs;k++)
      {
        //cout << "Setting frequency " << getBFreqIndex(i,j,k) << " used to true, from baseline " << j << ", baseline frequency " << k << endl; 
        freq = freqtable[getBFreqIndex(i,j,k)];
        configs[i].frequsedbybaseline[getBFreqIndex(i,j,k)] = true;
        if(freq.numchannels/freq.channelstoaverage < configs[i].minpostavfreqchannels)
          configs[i].minpostavfreqchannels = freq.numchannels/freq.channelstoaverage;
      }
    }
  }
  //process the pulsar configuration files
  for(int i=0;i<numconfigs;i++)
  {
    if(configs[i].pulsarbin)
    {
      if (consistencyok)
        consistencyok = processPulsarConfig(configs[i].pulsarconfigfilename, i);
      if (consistencyok)
        consistencyok = setPolycoFreqInfo(i);
    }
  }
  //cout << "About to open the Model, consistencyok is " << consistencyok << endl;
  if(consistencyok) {
    model = new Model(this, calcfilename);
    consistencyok = model->openSuccess();
  }
  for(int i=0;i<telescopetablelength;i++) {
    if(consistencyok)
      consistencyok = model->addClockTerms(telescopetable[i].name, telescopetable[i].clockrefmjd, telescopetable[i].clockorder, telescopetable[i].clockpoly);
  }
  estimatedbytes += model->getEstimatedBytes();
  //cout << "About to populateScanConfigList(), consistencyok is " << consistencyok << endl;
  if(consistencyok)
    consistencyok = populateScanConfigList();
  if(consistencyok)
    consistencyok = populateModelDatastreamMap();
  if(consistencyok)
    consistencyok = populateResultLengths();
  if(consistencyok)
    consistencyok = consistencyCheck();
  commandthreadinitialised = false;
  dumpsta = false;
  dumplta = false;
  stadumpchannels = DEFAULT_MONITOR_NUMCHANNELS;
  ltadumpchannels = DEFAULT_MONITOR_NUMCHANNELS;

  char *monitor_tcpwin = getenv("DIFX_MONITOR_TCPWINDOW");
  if (monitor_tcpwin!=0) {
    Configuration::MONITOR_TCP_WINDOWBYTES = atoi(monitor_tcpwin)*1024;
    cinfo << startl << "DIFX_MONITOR_TCPWINDOW set to" << Configuration::MONITOR_TCP_WINDOWBYTES/1024 << "kB" << endl;
    } else {
      Configuration::MONITOR_TCP_WINDOWBYTES = 262144;
    }
}


Configuration::~Configuration()
{
  if(configread)
  {
    for(int i=0;i<numconfigs;i++)
    {
      delete [] configs[i].datastreamindices;
      delete [] configs[i].baselineindices;
      delete [] configs[i].ordereddatastreamindices;
      delete [] configs[i].frequsedbybaseline;
    }
    delete [] configs;
  }
  if(datastreamread)
  {
    for(int i=0;i<datastreamtablelength;i++)
    {
      delete [] datastreamtable[i].recordedfreqtableindices;
      delete [] datastreamtable[i].recordedfreqpols;
      delete [] datastreamtable[i].recordedfreqclockoffsets;
      delete [] datastreamtable[i].recordedfreqlooffsets;
      delete [] datastreamtable[i].zoomfreqtableindices;
      delete [] datastreamtable[i].zoomfreqpols;
      delete [] datastreamtable[i].zoomfreqparentdfreqindices;
      delete [] datastreamtable[i].zoomfreqchanneloffset;
      delete [] datastreamtable[i].recordedbandpols;
      delete [] datastreamtable[i].recordedbandlocalfreqindices;
      delete [] datastreamtable[i].zoombandpols;
      delete [] datastreamtable[i].zoombandlocalfreqindices;
      delete [] datastreamtable[i].datafilenames;
    }
    delete [] datastreamtable;
  }
  if(model)
    delete model;
  delete [] freqtable;
  delete [] telescopetable;
  for(int i=0;i<baselinetablelength;i++)
  {
    for(int j=0;j<baselinetable[i].numfreqs;j++)
    {
      for(int k=0;k<baselinetable[i].numpolproducts[j];k++)
        delete [] baselinetable[i].polpairs[j][k];
      delete [] baselinetable[i].polpairs[j];
      delete [] baselinetable[i].datastream1bandindex[j];
      delete [] baselinetable[i].datastream2bandindex[j];
    }
    delete [] baselinetable[i].datastream1bandindex;
    delete [] baselinetable[i].datastream2bandindex;
    delete [] baselinetable[i].numpolproducts;
    delete [] baselinetable[i].freqtableindices;
    delete [] baselinetable[i].polpairs;
  }
  delete [] baselinetable;
  delete [] numprocessthreads;
}

int Configuration::genMk5FormatName(dataformat format, int nchan, double bw, int nbits, int framebytes, int decimationfactor, char *formatname)
{
  int fanout=1, mbps;

  mbps = int(2*nchan*bw*nbits + 0.5);

  switch(format)
  {
    case MKIV:
      fanout = framebytes*8/(20000*nbits*nchan);
      if(fanout*20000*nbits*nchan != framebytes*8)
      {
        cfatal << startl << "genMk5FormatName : MKIV format : framebytes = " << framebytes << " is not allowed" << endl;
        return -1;
      }
      if(decimationfactor > 1)	// Note, this conditional is to ensure compatibility with older mark5access versions
        sprintf(formatname, "MKIV1_%d-%d-%d-%d/%d", fanout, mbps, nchan, nbits, decimationfactor);
      else
        sprintf(formatname, "MKIV1_%d-%d-%d-%d", fanout, mbps, nchan, nbits);
      break;
    case VLBA:
      fanout = framebytes*8/(20160*nbits*nchan);
      if(fanout*20160*nbits*nchan != framebytes*8)
      {
        cfatal << startl << "genMk5FormatName : VLBA format : framebytes = " << framebytes << " is not allowed" << endl;
        return -1;
      }
      if(decimationfactor > 1)
        sprintf(formatname, "VLBA1_%d-%d-%d-%d/%d", fanout, mbps, nchan, nbits, decimationfactor);
      else
        sprintf(formatname, "VLBA1_%d-%d-%d-%d", fanout, mbps, nchan, nbits);
      break;
    case VLBN:
      fanout = framebytes*8/(20160*nbits*nchan);
      if(fanout*20160*nbits*nchan != framebytes*8)
      {
        cfatal << startl << "genMk5FormatName : VLBN format : framebytes = " << framebytes << " is not allowed" << endl;
        return -1;
      }
      if(decimationfactor > 1)
        sprintf(formatname, "VLBN1_%d-%d-%d-%d/%d", fanout, mbps, nchan, nbits, decimationfactor);
      else
        sprintf(formatname, "VLBN1_%d-%d-%d-%d", fanout, mbps, nchan, nbits);
      break;
    case MARK5B:
      if(decimationfactor > 1)
        sprintf(formatname, "Mark5B-%d-%d-%d/%d", mbps, nchan, nbits, decimationfactor);
      else
        sprintf(formatname, "Mark5B-%d-%d-%d", mbps, nchan, nbits);
      break;
    case VDIF:
      if(decimationfactor > 1)
        sprintf(formatname, "VDIF_%d-%d-%d-%d/%d", framebytes-32, mbps, nchan, nbits, decimationfactor);
      else
        sprintf(formatname, "VDIF_%d-%d-%d-%d", framebytes-32, mbps, nchan, nbits);
      break;
    default:
      cfatal << startl << "genMk5FormatName : unsupported format encountered" << endl;
      return -1;
  }

  return fanout;
}

int Configuration::getFramePayloadBytes(int configindex, int configdatastreamindex)
{
  int payloadsize;
  int framebytes = getFrameBytes(configindex, configdatastreamindex);
  dataformat format = getDataFormat(configindex, configdatastreamindex);
  
  switch(format)
  {
    case VLBA:
    case VLBN:
      payloadsize = (framebytes/2520)*2500;
      break;
    case MARK5B:
      payloadsize = framebytes - 16;
      break;
    case VDIF:
      payloadsize = framebytes - 32;
      break;
    default:
      payloadsize = framebytes;
  }

  return payloadsize;
}

void Configuration::getFrameInc(int configindex, int configdatastreamindex, int &sec, int &ns)
{
  int nchan, qb, decimationfactor;
  int payloadsize;
  double samplerate; /* in Hz */
  double seconds;

  nchan = getDNumRecordedBands(configindex, configdatastreamindex);
  samplerate = 2.0e6*getDRecordedBandwidth(configindex, configdatastreamindex, 0);
  qb = getDNumBits(configindex, configdatastreamindex);
  decimationfactor = getDDecimationFactor(configindex, configdatastreamindex);
  payloadsize = getFramePayloadBytes(configindex, configdatastreamindex);

  seconds = payloadsize*8/(samplerate*nchan*qb*decimationfactor);
  sec = int(seconds);
  ns = int(1.0e9*(seconds - sec));
}

int Configuration::getFramesPerSecond(int configindex, int configdatastreamindex)
{
  int nchan, qb, decimationfactor;
  int payloadsize;
  double samplerate; /* in Hz */

  nchan = getDNumRecordedBands(configindex, configdatastreamindex);
  samplerate = 2.0e6*getDRecordedBandwidth(configindex, configdatastreamindex, 0);
  qb = getDNumBits(configindex, configdatastreamindex);
  decimationfactor = getDDecimationFactor(configindex, configdatastreamindex);
  payloadsize = getFramePayloadBytes(configindex, configdatastreamindex);

  // This will always work out to be an integer 
  return int(samplerate*nchan*qb*decimationfactor/(8*payloadsize) + 0.5);
}

int Configuration::getMaxDataBytes()
{
  int length;
  int maxlength = getDataBytes(0,0);

  for(int i=0;i<numconfigs;i++)
  {
    for(int j=0;j<numdatastreams;j++)
    {
      length = getDataBytes(i,j);
      if(length > maxlength)
        maxlength = length;
    }
  }

  return maxlength;
}

int Configuration::getMaxDataBytes(int datastreamindex)
{
  int length;
  int maxlength = getDataBytes(0,datastreamindex);

  for(int i=1;i<numconfigs;i++)
  {
    length = getDataBytes(i,datastreamindex);
    if(length > maxlength)
      maxlength = length;
  }

  return maxlength;
}

int Configuration::getMaxBlocksPerSend()
{
  int length;
  int maxlength = configs[0].blockspersend;

  for(int i=1;i<numconfigs;i++)
  {
    length = configs[i].blockspersend;
    if(length > maxlength)
      maxlength = length;
  }

  return maxlength;
}

int Configuration::getMaxNumRecordedFreqs()
{
  int currentnumfreqs, maxnumfreqs = 0;
  
  for(int i=0;i<numconfigs;i++)
  {
    currentnumfreqs = getMaxNumRecordedFreqs(i);
    if(currentnumfreqs > maxnumfreqs)
      maxnumfreqs = currentnumfreqs;
  }
  
  return maxnumfreqs;
}

int Configuration::getMaxNumRecordedFreqs(int configindex)
{
  int maxnumfreqs = 0;
  
  for(int i=0;i<numdatastreams;i++)
  {
    if(datastreamtable[configs[configindex].datastreamindices[i]].numrecordedfreqs > maxnumfreqs)
      maxnumfreqs = datastreamtable[configs[configindex].datastreamindices[i]].numrecordedfreqs;
  }

  return maxnumfreqs;
}

int Configuration::getMaxNumFreqDatastreamIndex(int configindex)
{
  int maxindex = 0;
  int maxnumfreqs = datastreamtable[configs[configindex].datastreamindices[0]].numrecordedfreqs;
  
  for(int i=1;i<numdatastreams;i++)
  {
    if(datastreamtable[configs[configindex].datastreamindices[i]].numrecordedfreqs > maxnumfreqs)
    {
      maxnumfreqs = datastreamtable[configs[configindex].datastreamindices[i]].numrecordedfreqs;
      maxindex = i;
    }
  }
  
  return maxindex;
}

int Configuration::getMaxPhaseCentres(int configindex)
{
  int maxphasecentres = 1;
  for(int i=0;i<model->getNumScans();i++) {
    if(scanconfigindices[i] == configindex) {
      if(model->getNumPhaseCentres(i) > maxphasecentres)
        maxphasecentres = model->getNumPhaseCentres(i);
    }
  }
  return maxphasecentres;
}

int Configuration::getDataBytes(int configindex, int datastreamindex)
{
  datastreamdata currentds = datastreamtable[configs[configindex].datastreamindices[datastreamindex]];
  freqdata arecordedfreq = freqtable[currentds.recordedfreqtableindices[0]]; 
  int validlength = (arecordedfreq.decimationfactor*configs[configindex].blockspersend*currentds.numrecordedbands*2*currentds.numbits*arecordedfreq.numchannels)/8;
  if(currentds.format == MKIV || currentds.format == VLBA || currentds.format == VLBN || currentds.format == MARK5B || currentds.format == VDIF)
  {
    //must be an integer number of frames, with enough margin for overlap on either side
    validlength += (arecordedfreq.decimationfactor*(int)(configs[configindex].guardns/(1000.0/(freqtable[currentds.recordedfreqtableindices[0]].bandwidth*2.0))+0.5)*currentds.numrecordedbands*2*currentds.numbits*arecordedfreq.numchannels)/8;
    return ((validlength/currentds.framebytes)+2)*currentds.framebytes;
  }
  else
    return validlength;
}

int Configuration::getMaxProducts(int configindex)
{
  baselinedata current;
  int maxproducts = 0;
  for(int i=0;i<numbaselines;i++)
  {
    current = baselinetable[configs[configindex].baselineindices[i]];
    for(int j=0;j<current.numfreqs;j++)
    {
      if(current.numpolproducts[j] > maxproducts)
        maxproducts = current.numpolproducts[j];
    }
  }
  return maxproducts;
}

int Configuration::getMaxProducts()
{
  int maxproducts = 0;

  for(int i=0;i<numconfigs;i++)
  {
    if(getMaxProducts(i) > maxproducts)
      maxproducts = getMaxProducts(i);
  }
  
  return maxproducts;
}

int Configuration::getDMatchingBand(int configindex, int datastreamindex, int bandindex)
{
  datastreamdata ds = datastreamtable[configs[configindex].datastreamindices[datastreamindex]];
  if(bandindex >= ds.numrecordedbands) {
    for(int i=0;i<ds.numzoombands;i++)
    {
      if(ds.zoombandlocalfreqindices[bandindex] == ds.zoombandlocalfreqindices[i] && (i != bandindex))
        return i;
    }
  }
  else {
    for(int i=0;i<ds.numrecordedbands;i++)
    {
      if(ds.recordedbandlocalfreqindices[bandindex] == ds.recordedbandlocalfreqindices[i] && (i != bandindex))
        return i;
    }
  }

  return -1;
}

int Configuration::getCNumProcessThreads(int corenum)
{
  if(corenum < numcoreconfs)
    return numprocessthreads[corenum];
  return 1;
}

bool Configuration::stationUsed(int telescopeindex)
{
  bool toreturn = false;

  for(int i=0;i<numconfigs;i++)
  {
    for(int j=0;j<numdatastreams;j++)
    {
      if(datastreamtable[configs[i].datastreamindices[j]].telescopeindex == telescopeindex)
        toreturn = true;
    }
  }

  return toreturn;
}

Mode* Configuration::getMode(int configindex, int datastreamindex)
{
  configdata conf = configs[configindex];
  datastreamdata stream = datastreamtable[conf.datastreamindices[datastreamindex]];
  int framesamples, framebytes;
  int guardsamples = (int)(conf.guardns/(1000.0/(freqtable[stream.recordedfreqtableindices[0]].bandwidth*2.0)) + 0.5);
  int streamrecbandchan = freqtable[stream.recordedfreqtableindices[0]].numchannels;
  int streamdecimationfactor = freqtable[stream.recordedfreqtableindices[0]].decimationfactor;
  int streamchanstoaverage = freqtable[stream.recordedfreqtableindices[0]].channelstoaverage;
  double streamrecbandwidth = freqtable[stream.recordedfreqtableindices[0]].bandwidth;

  switch(stream.format)
  {
    case LBASTD:
      if(stream.numbits != 2)
        cerror << startl << "ERROR! All LBASTD Modes must have 2 bit sampling - overriding input specification!!!" << endl;
      return new LBAMode(this, configindex, datastreamindex, streamrecbandchan, streamchanstoaverage, conf.blockspersend, guardsamples, stream.numrecordedfreqs, streamrecbandwidth,  stream.recordedfreqclockoffsets, stream.recordedfreqlooffsets, stream.numrecordedbands, stream.numzoombands, 2/*bits*/, stream.filterbank, conf.fringerotationorder, conf.arraystridelen, conf.writeautocorrs, LBAMode::stdunpackvalues);
      break;
    case LBAVSOP:
      if(stream.numbits != 2)
        cerror << startl << "ERROR! All LBASTD Modes must have 2 bit sampling - overriding input specification!!!" << endl;
      return new LBAMode(this, configindex, datastreamindex, streamrecbandchan, streamchanstoaverage, conf.blockspersend, guardsamples, stream.numrecordedfreqs, streamrecbandwidth, stream.recordedfreqclockoffsets, stream.recordedfreqlooffsets, stream.numrecordedbands, stream.numzoombands, 2/*bits*/, stream.filterbank, conf.fringerotationorder, conf.arraystridelen, conf.writeautocorrs, LBAMode::vsopunpackvalues);
      break;
    case MKIV:
    case VLBA:
    case VLBN:
    case MARK5B:
    case VDIF:
      framesamples = getFramePayloadBytes(configindex, datastreamindex)*8/(getDNumBits(configindex, datastreamindex)*getDNumRecordedBands(configindex, datastreamindex)*streamdecimationfactor);
      framebytes = getFrameBytes(configindex, datastreamindex);
      return new Mk5Mode(this, configindex, datastreamindex, streamrecbandchan, streamchanstoaverage, conf.blockspersend, guardsamples, stream.numrecordedfreqs, streamrecbandwidth, stream.recordedfreqclockoffsets, stream.recordedfreqlooffsets, stream.numrecordedbands, stream.numzoombands, stream.numbits, stream.filterbank, conf.fringerotationorder, conf.arraystridelen, conf.writeautocorrs, framebytes, framesamples, stream.format);
      break;
    default:
      cerror << startl << "Error - unknown Mode!!!" << endl;
      return NULL;
  }
}

Configuration::sectionheader Configuration::getSectionHeader(ifstream * input)
{
  string line = "";

  while (line == "" && !input->eof())
    getline(*input, line); //skip the whitespace

  //return the type of section this is
  if(line.substr(0, 17) == "# COMMON SETTINGS")
    return COMMON;
  if(line.substr(0, 16) == "# CONFIGURATIONS")
    return CONFIG;
  if(line.substr(0, 7) == "# RULES")
    return RULE;
  if(line.substr(0, 12) == "# FREQ TABLE")
    return FREQ;
  if(line.substr(0, 17) == "# TELESCOPE TABLE")
    return TELESCOPE;
  if(line.substr(0, 18) == "# DATASTREAM TABLE")
    return DATASTREAM;
  if(line.substr(0, 16) == "# BASELINE TABLE")
    return BASELINE;
  if(line.substr(0, 12) == "# DATA TABLE")
    return DATA;
  if(line.substr(0, 15) == "# NETWORK TABLE")
    return NETWORK;

  if (input->eof())
    return INPUT_EOF;

  return UNKNOWN;
}

bool Configuration::processBaselineTable(ifstream * input)
{
  int tempint, dsband;
  int ** tempintptr;
  string line;
  datastreamdata dsdata;
  baselinedata bldata;

  getinputline(input, &line, "BASELINE ENTRIES");
  baselinetablelength = atoi(line.c_str());
  baselinetable = new baselinedata[baselinetablelength];
  estimatedbytes += baselinetablelength*sizeof(baselinedata);
  if(baselinetablelength < numbaselines)
  {
    if(mpiid == 0) //only write one copy of this error message
      cfatal << startl << "Not enough baselines are supplied in the baseline table (" << baselinetablelength << ") compared to the number of baselines (" << numbaselines << ")!!!" << endl;
    return false;
  }

  for(int i=0;i<baselinetablelength;i++)
  {
    //read in the info for this baseline
    baselinetable[i].localfreqindices = new int[freqtablelength];
    baselinetable[i].totalbands = 0;
    getinputline(input, &line, "D/STREAM A INDEX ", i);
    baselinetable[i].datastream1index = atoi(line.c_str());
    getinputline(input, &line, "D/STREAM B INDEX ", i);
    baselinetable[i].datastream2index = atoi(line.c_str());
    getinputline(input, &line, "NUM FREQS ", i);
    baselinetable[i].numfreqs = atoi(line.c_str());
    baselinetable[i].oddlsbfreqs = new int[baselinetable[i].numfreqs];
    baselinetable[i].numpolproducts = new int[baselinetable[i].numfreqs];
    baselinetable[i].datastream1bandindex = new int*[baselinetable[i].numfreqs];
    baselinetable[i].datastream2bandindex = new int*[baselinetable[i].numfreqs];
    baselinetable[i].freqtableindices = new int[baselinetable[i].numfreqs];
    baselinetable[i].polpairs = new char**[baselinetable[i].numfreqs];
    for(int j=0;j<baselinetable[i].numfreqs;j++)
    {
      baselinetable[i].oddlsbfreqs[j] = 0;
      getinputline(input, &line, "POL PRODUCTS ", i);
      baselinetable[i].numpolproducts[j] = atoi(line.c_str());
      baselinetable[i].datastream1bandindex[j] = new int[baselinetable[i].numpolproducts[j]];
      baselinetable[i].datastream2bandindex[j] = new int[baselinetable[i].numpolproducts[j]];
      estimatedbytes += baselinetable[i].numpolproducts[j]*2*4;
      baselinetable[i].polpairs[j] = new char*[baselinetable[i].numpolproducts[j]];
      for(int k=0;k<baselinetable[i].numpolproducts[j];k++)
      {
        baselinetable[i].totalbands++;
        getinputline(input, &line, "D/STREAM A BAND ", k);
        baselinetable[i].datastream1bandindex[j][k] = atoi(line.c_str());
        getinputline(input, &line, "D/STREAM B BAND ", k);
        baselinetable[i].datastream2bandindex[j][k] = atoi(line.c_str());
        baselinetable[i].polpairs[j][k] = new char[3];
        estimatedbytes += 3;
      }
      dsdata = datastreamtable[baselinetable[i].datastream1index];
      dsband = baselinetable[i].datastream1bandindex[j][0];
      if(dsband >= dsdata.numrecordedbands) //it is a zoom band
        baselinetable[i].freqtableindices[j] = dsdata.zoomfreqtableindices[dsdata.zoombandlocalfreqindices[dsband-dsdata.numrecordedbands]];
      else
        baselinetable[i].freqtableindices[j] = dsdata.recordedfreqtableindices[dsdata.recordedbandlocalfreqindices[dsband]];
      for(int k=0;k<baselinetable[i].numpolproducts[j];k++) {
        dsdata = datastreamtable[baselinetable[i].datastream1index];
        dsband = baselinetable[i].datastream1bandindex[j][k];
        if(dsband >= dsdata.numrecordedbands) //it is a zoom band
          baselinetable[i].polpairs[j][k][0] = dsdata.zoombandpols[dsband-dsdata.numrecordedbands];
        else
          baselinetable[i].polpairs[j][k][0] = dsdata.recordedbandpols[dsband];
        dsdata = datastreamtable[baselinetable[i].datastream2index];
        dsband = baselinetable[i].datastream2bandindex[j][k];
        if(dsband >= dsdata.numrecordedbands) //it is a zoom band
          baselinetable[i].polpairs[j][k][1] = dsdata.zoombandpols[dsband-dsdata.numrecordedbands];
        else
          baselinetable[i].polpairs[j][k][1] = dsdata.recordedbandpols[dsband];
      }
    }
    if(datastreamtable[baselinetable[i].datastream1index].telescopeindex > datastreamtable[baselinetable[i].datastream2index].telescopeindex)
    {
      if(mpiid == 0) //only write one copy of this error message
        cerror << startl << "First datastream for baseline " << i << " has a higher number than second datastream - reversing!!!" << endl;
      tempint = baselinetable[i].datastream1index;
      baselinetable[i].datastream1index = baselinetable[i].datastream2index;
      baselinetable[i].datastream2index = tempint;
      tempintptr = baselinetable[i].datastream1bandindex;
      baselinetable[i].datastream1bandindex = baselinetable[i].datastream2bandindex;
      baselinetable[i].datastream2bandindex = tempintptr;
    }
  }
  for(int f=0;f<freqtablelength;f++)
  {
    for(int i=0;i<baselinetablelength;i++)
    {
      bldata = baselinetable[i];
      bldata.localfreqindices[f] = -1;
      for(int j=0;j<bldata.numfreqs;j++)
      {
        if(bldata.freqtableindices[j] == f)
          bldata.localfreqindices[f] = j;
      }
    }
  }
  baselineread = true;
  return true;
}

void Configuration::processCommon(ifstream * input)
{
  string line;

  getinputline(input, &calcfilename, "CALC FILENAME");
  getinputline(input, &coreconffilename, "CORE CONF FILENAME");
  getinputline(input, &line, "EXECUTE TIME (SEC)");
  executeseconds = atoi(line.c_str());
  getinputline(input, &line, "START MJD");
  startmjd = atoi(line.c_str());
  getinputline(input, &line, "START SECONDS");
  startseconds = atoi(line.c_str());
  startns = (int)((atof(line.c_str()) - ((double)startseconds))*1000000000.0 + 0.5);
  getinputline(input, &line, "ACTIVE DATASTREAMS");
  numdatastreams = atoi(line.c_str());
  getinputline(input, &line, "ACTIVE BASELINES");
  numbaselines = atoi(line.c_str());
  getinputline(input, &line, "VIS BUFFER LENGTH");
  visbufferlength = atoi(line.c_str());
  getinputline(input, &line, "OUTPUT FORMAT");
  if(line == "SWIN" || line == "DIFX")
  {
    outformat = DIFX;
  }
  else if(line == "ASCII")
  {
    outformat = ASCII;
  }
  else
  {
    if(mpiid == 0) //only write one copy of this error message
      cerror << startl << "Unknown output format " << line << " (case sensitive choices are SWIN, DIFX (same thing) and ASCII), assuming SWIN/DIFX" << endl;
    outformat = DIFX;
  }
  getinputline(input, &outputfilename, "OUTPUT FILENAME");

  commonread = true;
}

bool Configuration::processConfig(ifstream * input)
{
  string line;
  bool found;

  maxnumpulsarbins = 0;
  maxnumbufferedffts = 0;

  getinputline(input, &line, "NUM CONFIGURATIONS");
  numconfigs = atoi(line.c_str());
  configs = new configdata[numconfigs];
  estimatedbytes += numconfigs*sizeof(configdata);
  for(int i=0;i<numconfigs;i++)
  {
    found = false;
    configs[i].datastreamindices = new int[numdatastreams];
    configs[i].baselineindices = new int [numbaselines];
    getinputline(input, &(configs[i].name), "CONFIG NAME");
    getinputline(input, &line, "INT TIME (SEC)");
    configs[i].inttime = atof(line.c_str());
    getinputline(input, &line, "SUBINT NANOSECONDS");
    configs[i].subintns = atoi(line.c_str());
    getinputline(input, &line, "GUARD NANOSECONDS");
    configs[i].guardns = atoi(line.c_str());
    getinputline(input, &line, "FRINGE ROTN ORDER");
    configs[i].fringerotationorder = atoi(line.c_str());
    getinputline(input, &line, "ARRAY STRIDE LEN");
    configs[i].arraystridelen = atoi(line.c_str());
    getinputline(input, &line, "XMAC STRIDE LEN");
    configs[i].xmacstridelen = atoi(line.c_str());
    getinputline(input, &line, "NUM BUFFERED FFTS");
    configs[i].numbufferedffts = atoi(line.c_str());
    if(configs[i].numbufferedffts > maxnumbufferedffts)
      maxnumbufferedffts = configs[i].numbufferedffts;
    getinputline(input, &line, "WRITE AUTOCORRS");
    configs[i].writeautocorrs = ((line == "TRUE") || (line == "T") || (line == "true") || (line == "t"))?true:false;
    getinputline(input, &line, "PULSAR BINNING");
    configs[i].pulsarbin = ((line == "TRUE") || (line == "T") || (line == "true") || (line == "t"))?true:false;
    if(configs[i].pulsarbin)
    {
      getinputline(input, &configs[i].pulsarconfigfilename, "PULSAR CONFIG FILE");
    }
    getinputline(input, &line, "PHASED ARRAY");
    configs[i].phasedarray = ((line == "TRUE") || (line == "T") || (line == "true") || (line == "t"))?true:false;
    if(configs[i].phasedarray)
    {
      if(mpiid == 0) //only write one copy of this error message
        cwarn << startl << "Error - phased array is not yet supported!!!" << endl;
      getinputline(input, &configs[i].phasedarrayconfigfilename, "PHASED ARRAY CONFIG FILE");
    }
    for(int j=0;j<numdatastreams;j++)
    {
      getinputline(input, &line, "DATASTREAM ", j);
      configs[i].datastreamindices[j] = atoi(line.c_str());
    }
    for(int j=0;j<numbaselines;j++)
    {
      getinputline(input, &line, "BASELINE ", j);
      configs[i].baselineindices[j] = atoi(line.c_str());
    }
  }

  configread = true;
  return true;
}

bool Configuration::processDatastreamTable(ifstream * input)
{
  datastreamdata * dsdata;
  int configindex, freqindex, decimationfactor, tonefreq;
  double lofreq, parentlowbandedge, parenthighbandedge, lowbandedge, highbandedge;
  string line;
  bool ok = true;

  getinputline(input, &line, "DATASTREAM ENTRIES");
  datastreamtablelength = atoi(line.c_str());
  datastreamtable = new datastreamdata[datastreamtablelength];
  estimatedbytes += datastreamtablelength*sizeof(datastreamdata);
  if(datastreamtablelength < numdatastreams)
  {
    if(mpiid == 0) //only write one copy of this error message
      cfatal << startl << "Error - not enough datastreams are supplied in the datastream table (" << datastreamtablelength << ") compared to the number of datastreams (" << numdatastreams << "!!!" << endl;
    return false;
  }
  //create the ordereddatastream array
  for(int i=0;i<numconfigs;i++)
    configs[i].ordereddatastreamindices = new int[datastreamtablelength];

  //get the information on the length of the internal buffer for the datastreams
  getinputline(input, &line, "DATA BUFFER FACTOR");
  databufferfactor = atoi(line.c_str());
  getinputline(input, &line, "NUM DATA SEGMENTS");
  numdatasegments = atoi(line.c_str());

  for(int i=0;i<datastreamtablelength;i++)
  {
    dsdata = &(datastreamtable[i]);
    configindex=-1;
    datastreamtable[i].numdatafiles = 0; //default in case its a network datastream
    datastreamtable[i].tcpwindowsizekb = 0; //default in case its a file datastream
    datastreamtable[i].portnumber = -1; //default in case its a file datastream

    //get configuration index for this datastream
    for(int c=0; c<numconfigs; c++)
    {
      for(int d=0; d<numdatastreams; d++)
      {
        if(configs[c].datastreamindices[d] == i)
        {
          configindex = c;
          break;
        }
      }
      if(configindex >= 0) break;
    }

    //read all the info for this datastream
    getinputline(input, &line, "TELESCOPE INDEX");
    datastreamtable[i].telescopeindex = atoi(line.c_str());
    getinputline(input, &line, "TSYS");
    datastreamtable[i].tsys = atof(line.c_str());
    getinputline(input, &line, "DATA FORMAT");
    if(line == "LBASTD")
      datastreamtable[i].format = LBASTD;
    else if(line == "LBAVSOP")
      datastreamtable[i].format = LBAVSOP;
    else if(line == "NZ")
      datastreamtable[i].format = NZ;
    else if(line == "K5")
      datastreamtable[i].format = K5;
    else if(line == "MKIV")
      datastreamtable[i].format = MKIV;
    else if(line == "VLBA")
      datastreamtable[i].format = VLBA;
    else if(line == "VLBN")
      datastreamtable[i].format = VLBN;
    else if(line == "MARK5B")
      datastreamtable[i].format = MARK5B;
    else if(line == "VDIF")
      datastreamtable[i].format = VDIF;
    else
    {
      if(mpiid == 0) //only write one copy of this error message
        cfatal << startl << "Unknown data format " << line << " (case sensitive choices are LBASTD, LBAVSOP, NZ, K5, MKIV, VLBA, VLBN, MARK5B and VDIF)" << endl;
      return false;
    }
    getinputline(input, &line, "QUANTISATION BITS");
    datastreamtable[i].numbits = atoi(line.c_str());

    getinputline(input, &line, "DATA FRAME SIZE");
    datastreamtable[i].framebytes = atoi(line.c_str());

    getinputline(input, &line, "DATA SOURCE");
    if(line == "FILE")
      datastreamtable[i].source = UNIXFILE;
    else if(line == "MODULE")
      datastreamtable[i].source = MK5MODULE;
    else if(line == "EVLBI")
      datastreamtable[i].source = EVLBI;
    else
    {
      if(mpiid == 0) //only write one copy of this error message
        cfatal << startl << "Unknown data source " << line << " (case sensitive choices are FILE, MK5MODULE and EVLBI)" << endl;
      return false;
    }
    getinputline(input, &line, "FILTERBANK USED");
    datastreamtable[i].filterbank = ((line == "TRUE") || (line == "T") || (line == "true") || (line == "t"))?true:false;
    if(datastreamtable[i].filterbank) {
      if(mpiid == 0) //only write one copy of this error message
        cwarn << startl << "Error - filterbank not yet supported!!!" << endl;
    }
    getinputline(input, &line, "PHASE CAL INT (MHZ)");
    datastreamtable[i].phasecalintervalmhz = atoi(line.c_str());
    if(datastreamtable[i].phasecalintervalmhz < 0)
    {
      if(mpiid == 0) //only write one copy of this error message
        cwarn << startl << "Error - phase cal interval <0 currently unsupported" << endl;
    }

    getinputline(input, &line, "NUM RECORDED FREQS");
    datastreamtable[i].numrecordedfreqs = atoi(line.c_str());
    datastreamtable[i].recordedfreqpols = new int[datastreamtable[i].numrecordedfreqs];
    datastreamtable[i].recordedfreqtableindices = new int[datastreamtable[i].numrecordedfreqs];
    datastreamtable[i].recordedfreqclockoffsets = new double[datastreamtable[i].numrecordedfreqs];
    datastreamtable[i].recordedfreqlooffsets = new double[datastreamtable[i].numrecordedfreqs];
    estimatedbytes += 8*datastreamtable[i].numrecordedfreqs*3;
    datastreamtable[i].numrecordedbands = 0;
    for(int j=0;j<datastreamtable[i].numrecordedfreqs;j++)
    {
      getinputline(input, &line, "REC FREQ INDEX ", j);
      datastreamtable[i].recordedfreqtableindices[j] = atoi(line.c_str());
      getinputline(input, &line, "CLK OFFSET ", j);
      datastreamtable[i].recordedfreqclockoffsets[j] = atof(line.c_str());
      if(j == 0 && datastreamtable[i].recordedfreqclockoffsets[j] != 0.0 && mpiid == 0)
        cwarn << startl << "Model accountability is compromised if the first band of a telescope has a non-zero clock offset! If this is the first/only datastream for " << telescopetable[datastreamtable[i].telescopeindex].name << ", you should adjust the telescope clock so that the offset for this band is ZERO!" << endl;
      getinputline(input, &line, "FREQ OFFSET ", j); //Freq offset is positive if recorded LO frequency was higher than the frequency in the frequency table
      datastreamtable[i].recordedfreqlooffsets[j] = atof(line.c_str());
      getinputline(input, &line, "NUM REC POLS ", j);
      datastreamtable[i].recordedfreqpols[j] = atoi(line.c_str());
      datastreamtable[i].numrecordedbands += datastreamtable[i].recordedfreqpols[j];
    }
    decimationfactor = freqtable[datastreamtable[i].recordedfreqtableindices[0]].decimationfactor;
    datastreamtable[i].bytespersamplenum = (datastreamtable[i].numrecordedbands*datastreamtable[i].numbits*decimationfactor)/8;
    if(datastreamtable[i].bytespersamplenum == 0)
    {
      datastreamtable[i].bytespersamplenum = 1;
      datastreamtable[i].bytespersampledenom = 8/(datastreamtable[i].numrecordedbands*datastreamtable[i].numbits*decimationfactor);
    }
    else
      datastreamtable[i].bytespersampledenom = 1;
    datastreamtable[i].recordedbandpols = new char[datastreamtable[i].numrecordedbands];
    datastreamtable[i].recordedbandlocalfreqindices = new int[datastreamtable[i].numrecordedbands];
    estimatedbytes += datastreamtable[i].numrecordedbands*5;
    for(int j=0;j<datastreamtable[i].numrecordedbands;j++)
    {
      getinputline(input, &line, "REC BAND ", j);
      datastreamtable[i].recordedbandpols[j] = *(line.data());
      getinputline(input, &line, "REC BAND ", j);
      datastreamtable[i].recordedbandlocalfreqindices[j] = atoi(line.c_str());
      if(datastreamtable[i].recordedbandlocalfreqindices[j] >= datastreamtable[i].numrecordedfreqs) {
        if(mpiid == 0) //only write one copy of this error message
          cerror << startl << "Error - attempting to refer to freq outside local table!!!" << endl;
        return false;
      }
    }
    getinputline(input, &line, "NUM ZOOM FREQS");
    datastreamtable[i].numzoomfreqs = atoi(line.c_str());
    datastreamtable[i].zoomfreqtableindices = new int[datastreamtable[i].numzoomfreqs];
    datastreamtable[i].zoomfreqpols = new int[datastreamtable[i].numzoomfreqs];
    datastreamtable[i].zoomfreqparentdfreqindices = new int[datastreamtable[i].numzoomfreqs];
    datastreamtable[i].zoomfreqchanneloffset = new int[datastreamtable[i].numzoomfreqs];
    estimatedbytes += datastreamtable[i].numzoomfreqs*16;
    datastreamtable[i].numzoombands = 0;
    for(int j=0;j<datastreamtable[i].numzoomfreqs;j++)
    {
      getinputline(input, &line, "ZOOM FREQ INDEX ");
      datastreamtable[i].zoomfreqtableindices[j] = atoi(line.c_str());
      getinputline(input, &line, "NUM ZOOM POLS ", j);
      datastreamtable[i].zoomfreqpols[j] = atoi(line.c_str());
      datastreamtable[i].numzoombands += datastreamtable[i].zoomfreqpols[j];
      datastreamtable[i].zoomfreqparentdfreqindices[j] = -1;
      for (int k=0;k<datastreamtable[i].numrecordedfreqs;k++) {
        parentlowbandedge = freqtable[datastreamtable[i].recordedfreqtableindices[k]].bandedgefreq;
        parenthighbandedge = freqtable[datastreamtable[i].recordedfreqtableindices[k]].bandedgefreq + freqtable[datastreamtable[i].recordedfreqtableindices[k]].bandwidth;
        if(freqtable[datastreamtable[i].recordedfreqtableindices[k]].lowersideband) {
          parentlowbandedge -= freqtable[datastreamtable[i].recordedfreqtableindices[k]].bandwidth;
          parenthighbandedge -= freqtable[datastreamtable[i].recordedfreqtableindices[k]].bandwidth;
        }
        lowbandedge = freqtable[datastreamtable[i].zoomfreqtableindices[k]].bandedgefreq;
        highbandedge = freqtable[datastreamtable[i].zoomfreqtableindices[k]].bandedgefreq + freqtable[datastreamtable[i].zoomfreqtableindices[k]].bandwidth;
        if(freqtable[datastreamtable[i].zoomfreqtableindices[k]].lowersideband) {
          parentlowbandedge -= freqtable[datastreamtable[i].zoomfreqtableindices[k]].bandwidth;
          parenthighbandedge -= freqtable[datastreamtable[i].zoomfreqtableindices[k]].bandwidth;
        }
        if (highbandedge < parenthighbandedge && lowbandedge > parentlowbandedge) {
          datastreamtable[i].zoomfreqparentdfreqindices[j] = k;
          datastreamtable[i].zoomfreqchanneloffset[j] = (int)(((lowbandedge - parentlowbandedge)/freqtable[datastreamtable[i].recordedfreqtableindices[0]].bandwidth)*freqtable[datastreamtable[i].recordedfreqtableindices[0]].numchannels);
          if (freqtable[datastreamtable[i].zoomfreqtableindices[j]].lowersideband)
            datastreamtable[i].zoomfreqchanneloffset[j] += freqtable[datastreamtable[i].zoomfreqtableindices[j]].numchannels;
        }
      }
    }
    datastreamtable[i].zoombandpols = new char[datastreamtable[i].numzoombands];
    datastreamtable[i].zoombandlocalfreqindices = new int[datastreamtable[i].numzoombands];
    estimatedbytes += 5*datastreamtable[i].numzoombands;
    for(int j=0;j<datastreamtable[i].numzoombands;j++)
    {
      getinputline(input, &line, "ZOOM BAND ", j);
      datastreamtable[i].zoombandpols[j] = *(line.data());
      getinputline(input, &line, "ZOOM BAND ", j);
      datastreamtable[i].zoombandlocalfreqindices[j] = atoi(line.c_str());
      if(datastreamtable[i].zoombandlocalfreqindices[j] >= datastreamtable[i].numzoomfreqs) {
        if(mpiid == 0) //only write one copy of this error message
          cerror << startl << "Error - attempting to refer to freq outside local table!!!" << endl;
        return false;
      }
    }
    if(dsdata->phasecalintervalmhz > 0)
    {
      dsdata->numrecordedfreqpcaltones = new int[dsdata->numrecordedfreqs];
      dsdata->recordedfreqpcaltonefreqs = new int*[dsdata->numrecordedfreqs];
      dsdata->recordedfreqpcaloffsetsmhz = new double[dsdata->numrecordedfreqs];
      dsdata->maxrecordedpcaltones = 0;
      estimatedbytes += sizeof(int)*(dsdata->numrecordedfreqs);
      for(int j=0;j<dsdata->numrecordedfreqs;j++)
      {
        datastreamtable[i].numrecordedfreqpcaltones[j] = 0;
        freqindex = dsdata->recordedfreqtableindices[j];
        lofreq = freqtable[freqindex].bandedgefreq;
        if(freqtable[freqindex].lowersideband)
          lofreq -= freqtable[freqindex].bandwidth;
        tonefreq = (int(lofreq)/dsdata->phasecalintervalmhz)*dsdata->phasecalintervalmhz;
        if(tonefreq < lofreq)
          tonefreq += dsdata->phasecalintervalmhz;
        while(tonefreq + dsdata->numrecordedfreqpcaltones[j]*dsdata->phasecalintervalmhz < lofreq + freqtable[freqindex].bandwidth)
          dsdata->numrecordedfreqpcaltones[j]++;
        if(dsdata->numrecordedfreqpcaltones[j] > dsdata->maxrecordedpcaltones)
          dsdata->maxrecordedpcaltones = dsdata->numrecordedfreqpcaltones[j];
        datastreamtable[i].recordedfreqpcaltonefreqs[j] = new int[datastreamtable[i].numrecordedfreqpcaltones[j]];
        estimatedbytes += sizeof(int)*datastreamtable[i].numrecordedfreqpcaltones[j];
        tonefreq = (int(lofreq)/dsdata->phasecalintervalmhz)*dsdata->phasecalintervalmhz;
        if(tonefreq < lofreq)
          tonefreq += dsdata->phasecalintervalmhz;
        for(int k=0;k<datastreamtable[i].numrecordedfreqpcaltones[j];k++)
          datastreamtable[i].recordedfreqpcaltonefreqs[j][k] = tonefreq + k*dsdata->phasecalintervalmhz;
        dsdata->recordedfreqpcaloffsetsmhz[j] = (double)datastreamtable[i].recordedfreqpcaltonefreqs[j][0] - lofreq;
      }
    }
    datastreamtable[i].tcpwindowsizekb = 0;
    datastreamtable[i].portnumber = 0;
  }

  for(int i=0;i<numconfigs;i++)
  {
    //work out blockspersend
    freqdata f = freqtable[datastreamtable[configs[i].datastreamindices[0]].recordedfreqtableindices[0]];
    double ffttime = 1000.0*f.numchannels/f.bandwidth;
    double bpersenddouble = configs[i].subintns/ffttime;
    configs[i].blockspersend = int(bpersenddouble + 0.5);
    if (fabs(bpersenddouble - configs[i].blockspersend) > Mode::TINY) {
      ok = false;
      if(mpiid == 0) //only write one copy of this error message
        cfatal << startl << "The supplied value of subint nanoseconds (" << configs[i].subintns << ") for config " << i << " does not yield an integer number of FFTs! (FFT time is " << ffttime << "). Aborting!" << endl;
    }
  }
  if(!ok)
    return false;

  //read in the core numthreads info
  ifstream coreinput(coreconffilename.c_str());
  numcoreconfs = 0;
  if(!coreinput.is_open() || coreinput.bad())
  {
    cerror << startl << "Could not open " << coreconffilename << " - will set all numthreads to 1!!" << endl;
  }
  else
  {
    getinputline(&coreinput, &line, "NUMBER OF CORES");
    int maxlines = atoi(line.c_str());
    numprocessthreads = new int[maxlines];
    getline(coreinput, line);
    for(int i=0;i<maxlines;i++)
    {
      if(coreinput.eof())
      {
        cerror << startl << "Hit the end of the file! Setting the numthread for Core " << i << " to 1" << endl;
        numprocessthreads[numcoreconfs++] = 1;
      }
      else
      {
        numprocessthreads[numcoreconfs++] = atoi(line.c_str());
        getline(coreinput, line);
      }
    }
  }
  coreinput.close();

  datastreamread = true;
  return true;
}

bool Configuration::processRuleTable(ifstream * input)
{
  int count=0;
  string key, val;
  getinputline(input, &key, "NUM RULES");
  numrules = atoi(key.c_str());
  rules = new ruledata[numrules];
  estimatedbytes += numrules*sizeof(ruledata);
  for(int i=0;i<numrules;i++) {
    rules[i].configindex = -1;
    rules[i].sourcename = "";
    rules[i].scanId = "";
    rules[i].calcode = "";
    rules[i].qual = -1;
    rules[i].mjdStart = -999.9;
    rules[i].mjdStop = -999.9;
  }

  while(count<numrules && !input->eof())
  {
    getinputkeyval(input, &key, &val);
    if(strstr(key.c_str(), "CONFIG NAME")) {
      rules[count].configname = val;
      count++;
    }
    else if(strstr(key.c_str(), "SOURCE")) {
      rules[count].sourcename = val;
    }
    else if(strstr(key.c_str(), "SCAN ID")) {
      rules[count].scanId = val;
    }
    else if(strstr(key.c_str(), "CALCODE")) {
      rules[count].calcode = val;
    }
    else if(strstr(key.c_str(), "QUAL")) {
      rules[count].qual = atoi(val.c_str());
    }
    else if(strstr(key.c_str(), "MJD START")) {
      rules[count].mjdStart = atof(val.c_str());
    }
    else if(strstr(key.c_str(), "MJD STOP")) {
      rules[count].mjdStop = atof(val.c_str());
    }
    else {
      if(mpiid == 0) //only write one copy of this error message
        cwarn << startl << "Received unknown key " << key << " with val " << val << " in rule table - ignoring!" << endl;
    }
  }

  for(int i=0;i<numrules;i++) {
    for(int j=0;j<numconfigs;j++) {
      if(rules[i].configname.compare(configs[j].name) == 0) {
        rules[i].configindex = j;
      }
    }
    if(rules[i].configindex < 0) {
      if(mpiid == 0) //only write one copy of this error message
        cfatal << startl << "Found a rule with config name " << rules[i].configname << " that doesn't match any configs - aborting!" << endl;
      return false;
    }
  }
  ruleread = true;
  return true;
}

void Configuration::processDataTable(ifstream * input)
{
  string line;

  for(int i=0;i<datastreamtablelength;i++)
  {
    getinputline(input, &line, "D/STREAM ", i);
    datastreamtable[i].numdatafiles = atoi(line.c_str());
    datastreamtable[i].datafilenames = new string[datastreamtable[i].numdatafiles];
    for(int j=0;j<datastreamtable[i].numdatafiles;j++)
      getinputline(input, &(datastreamtable[i].datafilenames[j]), "FILE ", i);
  }
}

bool Configuration::processFreqTable(ifstream * input)
{
  string line;

  getinputline(input, &line, "FREQ ENTRIES");
  freqtablelength = atoi(line.c_str());
  freqtable = new freqdata[freqtablelength];
  estimatedbytes += freqtablelength*sizeof(freqdata);
  for(int i=0;i<freqtablelength;i++)
  {
    getinputline(input, &line, "FREQ (MHZ) ", i);
    freqtable[i].bandedgefreq = atof(line.c_str());
    getinputline(input, &line, "BW (MHZ) ", i);
    freqtable[i].bandwidth = atof(line.c_str());
    getinputline(input, &line, "SIDEBAND ", i);
    freqtable[i].lowersideband = ((line == "L") || (line == "l") || (line == "LOWER") || (line == "lower"))?true:false;
    getinputline(input, &line, "NUM CHANNELS ");
    freqtable[i].numchannels = atoi(line.c_str());
    if(freqtable[i].numchannels > maxnumchannels)
      maxnumchannels = freqtable[i].numchannels;
    getinputline(input, &line, "CHANS TO AVG ");
    freqtable[i].channelstoaverage = atoi(line.c_str());
    if(freqtable[i].channelstoaverage <= 0 || (freqtable[i].channelstoaverage > 1 && freqtable[i].channelstoaverage%2 != 0)) {
      if(mpiid == 0) //only write one copy of this error message
        cerror << startl << "Channels to average must be positive and a power of two - not the case for frequency entry " << i << "(" << freqtable[i].channelstoaverage << ") - aborting!!" << endl;
      return false;
    }
    getinputline(input, &line, "OVERSAMPLE FAC. ");
    freqtable[i].oversamplefactor = atoi(line.c_str());
    getinputline(input, &line, "DECIMATION FAC. ");
    freqtable[i].decimationfactor = atoi(line.c_str());
    freqtable[i].matchingwiderbandindex = -1;
    freqtable[i].matchingwiderbandoffset = -1;
  }
  //now look for matching wider bands
  for(int i=freqtablelength-1;i>0;i--)
  {
    double f1chanwidth = freqtable[i].bandwidth/freqtable[i].numchannels;
    double f1loweredge = freqtable[i].bandedgefreq;
    if (freqtable[i].lowersideband)
      f1loweredge -= freqtable[i].bandwidth;
    for(int j=i-1;j>=0;j--)
    {
      double f2chanwidth = freqtable[j].bandwidth/freqtable[j].numchannels;
      double f2loweredge = freqtable[j].bandedgefreq;
      if (freqtable[j].lowersideband)
        f2loweredge -= freqtable[j].bandwidth;
      if((i != j) && (f1chanwidth == f2chanwidth) && (f1loweredge < f2loweredge) &&
          (f1loweredge + freqtable[i].bandwidth > f2loweredge + freqtable[j].bandwidth))
      {
        freqtable[j].matchingwiderbandindex = i;
        freqtable[j].matchingwiderbandoffset = int(((f2loweredge-f1loweredge)/freqtable[i].bandwidth)*freqtable[i].numchannels + 0.5);
      }
    }
  }
  freqread = true;
  return true;
}

void Configuration::processTelescopeTable(ifstream * input)
{
  string line;

  getinputline(input, &line, "TELESCOPE ENTRIES");
  telescopetablelength = atoi(line.c_str());
  telescopetable = new telescopedata[telescopetablelength];
  estimatedbytes += telescopetablelength*sizeof(telescopedata);
  for(int i=0;i<telescopetablelength;i++)
  {
    getinputline(input, &(telescopetable[i].name), "TELESCOPE NAME ", i);
    getinputline(input, &line, "CLOCK REF MJD ", i);
    telescopetable[i].clockrefmjd = atof(line.c_str());
    getinputline(input, &line, "CLOCK POLY ORDER ", i);
    telescopetable[i].clockorder = atoi(line.c_str());
    telescopetable[i].clockpoly = new double[telescopetable[i].clockorder+1];
    for(int j=0;j<telescopetable[i].clockorder+1;j++) {
      getinputline(input, &line, "CLOCK COEFF ", i);
      telescopetable[i].clockpoly[j] = atof(line.c_str());
    }
  }
}

void Configuration::processNetworkTable(ifstream * input)
{
  string line;

  for(int i=0;i<datastreamtablelength;i++)
  {
    getinputline(input, &line, "PORT NUM ", i);
    datastreamtable[i].portnumber = atoi(line.c_str());
    getinputline(input, &line, "TCP WINDOW (KB) ", i);
    datastreamtable[i].tcpwindowsizekb = atoi(line.c_str());
  }
}

bool Configuration::populateScanConfigList()
{
  bool applies, srcnameapplies, calcodeapplies, qualapplies;
  Model::source * src;
  ruledata r;

  scanconfigindices = new int[model->getNumScans()];
  estimatedbytes += 4*model->getNumScans();
  for(int i=0;i<model->getNumScans();i++) {
    scanconfigindices[i] = -1;
    for(int j=0;j<numrules;j++) {
      applies = true;
      r = rules[j];
      if((r.scanId.compare("") != 0) && (r.scanId.compare(model->getScanIdentifier(i)) != 0))
        applies = false;
      if(r.mjdStart > 0 && r.mjdStart > model->getScanStartMJD(i))
        applies = false;
      if(r.mjdStop > 0 && r.mjdStop > model->getScanEndMJD(i))
        applies = false;
      //cout << "Looking at scan " << i+1 << "/" << model->getNumScans() << endl;
      srcnameapplies = false;
      calcodeapplies = false;
      qualapplies = false;
      if(r.sourcename.compare("") == 0)
        srcnameapplies = true;
      if(r.calcode.compare("") == 0)
        calcodeapplies = true;
      if(r.qual < 0)
        qualapplies = true;
      for(int k=0;k<model->getNumPhaseCentres(i);k++) {
        //cout << "Looking at source " << k << " of scan " << i << endl;
        src = model->getScanPhaseCentreSource(i, k);
        if(r.sourcename.compare(src->name) == 0)
          srcnameapplies = true;
        if(r.calcode.compare(src->calcode) == 0)
          calcodeapplies = true;
        if(r.qual == src->qual)
          calcodeapplies = true;
      }
      if(applies && srcnameapplies && calcodeapplies && qualapplies) {
        if(scanconfigindices[i] < 0 || scanconfigindices[i] == r.configindex) {
          scanconfigindices[i] = r.configindex;
        }
        else {
          if(mpiid == 0) //only write one copy of this error message
            cfatal << startl << "Conflicting rules apply to scan " << i << " - aborting!!!" << endl;
          return false;
        }
      }
    }
  }

  return true;
}

bool Configuration::populateModelDatastreamMap()
{
  Model::station s;
  string tname;

  for(int i=0;i<datastreamtablelength;i++) {
    datastreamtable[i].modelfileindex = -1;
    tname = telescopetable[datastreamtable[i].telescopeindex].name;
    for(int j=0;j<model->getNumStations();j++) {
      s = model->getStation(j);
      if(tname.compare(s.name) == 0)
        datastreamtable[i].modelfileindex = j;
    }
  }

  for(int i=0;i<numconfigs;i++) {
    for(int j=0;j<numdatastreams;j++) {
      if(datastreamtable[configs[i].datastreamindices[j]].modelfileindex < 0) {
        cfatal << startl << "Couldn't find datastream " << telescopetable[datastreamtable[configs[i].datastreamindices[j]].telescopeindex].name << " in the model file - aborting!!!" << endl;
        return false;
      }
    }
  }

  return true;
}

bool Configuration::populateResultLengths()
{
  datastreamdata dsdata;
  baselinedata bldata;
  bool found;
  int threadfindex, threadbindex, coreresultindex, toadd;
  int bandsperautocorr, freqindex, freqchans, chanstoaverage, maxconfigphasecentres, xmacstridelen, binloop;

  maxthreadresultlength = 0;
  maxcoreresultlength = 0;
  for(int c=0;c<numconfigs;c++)
  {
    xmacstridelen = configs[c].xmacstridelen;
    binloop = 1;
    if(configs[c].pulsarbin && !configs[c].scrunchoutput)
      binloop = configs[c].numbins;
    //find a scan that matches this config
    found = false;
    maxconfigphasecentres = 1;
    for(int i=0;i<model->getNumScans();i++) {
      if(scanconfigindices[i] == c) {
        if(model->getNumPhaseCentres(i) > maxconfigphasecentres)
          maxconfigphasecentres = model->getNumPhaseCentres(i);
        found = true;
      }
    }
    if(!found) {
      if(mpiid == 0) //only write one copy of this error message
        cwarn << startl << "Did not find a scan matching config index " << c << endl;
    }
    if(getMaxProducts(c) > 2)
      bandsperautocorr = 2;
    else
      bandsperautocorr = 1;


    //work out the offsets for threadresult, and the total length too
    configs[c].completestridelength = new int[freqtablelength];
    configs[c].numxmacstrides = new int[freqtablelength];
    configs[c].threadresultfreqoffset = new int[freqtablelength];
    configs[c].threadresultbaselineoffset = new int*[freqtablelength];
    threadfindex = 0;
    for(int i=0;i<freqtablelength;i++)
    {
      if(configs[c].frequsedbybaseline[i])
      {
        configs[c].threadresultfreqoffset[i] = threadfindex;
        freqchans = freqtable[i].numchannels;
        configs[c].numxmacstrides[i] = freqtable[i].numchannels/xmacstridelen;
        configs[c].threadresultbaselineoffset[i] = new int[numbaselines];
        threadbindex = 0;
        for(int j=0;j<numbaselines;j++)
        {
          configs[c].threadresultbaselineoffset[i][j] = threadbindex;
          bldata = baselinetable[configs[c].baselineindices[j]];
          if(bldata.localfreqindices[i] >= 0)
          {
            configs[c].threadresultbaselineoffset[i][j] = threadbindex;
            threadbindex += binloop*bldata.numpolproducts[bldata.localfreqindices[i]]*xmacstridelen;
          }
        }
        configs[c].completestridelength[i] = threadbindex;
        threadfindex += configs[c].numxmacstrides[i]*configs[c].completestridelength[i];
      }
    }
    configs[c].threadresultlength = threadfindex;
    if(configs[c].threadresultlength > maxthreadresultlength)
      maxthreadresultlength = configs[c].threadresultlength;

    //work out the offsets for coreresult, and the total length too
    configs[c].coreresultbaselineoffset = new int*[freqtablelength];
    configs[c].coreresultbweightoffset  = new int*[freqtablelength];
    configs[c].coreresultautocorroffset = new int[numdatastreams];
    configs[c].coreresultacweightoffset = new int[numdatastreams];
    configs[c].coreresultpcaloffset     = new int[numdatastreams];
    coreresultindex = 0;
    for(int i=0;i<freqtablelength;i++)
    {
      if(configs[c].frequsedbybaseline[i])
      {
        freqchans = freqtable[i].numchannels;
        chanstoaverage = freqtable[i].channelstoaverage;
        configs[c].coreresultbaselineoffset[i] = new int[numbaselines];
        for(int j=0;j<numbaselines;j++)
        {
          bldata = baselinetable[configs[c].baselineindices[j]];
          if(bldata.localfreqindices[i] >= 0)
          {
            configs[c].coreresultbaselineoffset[i][j] = coreresultindex;
            coreresultindex += maxconfigphasecentres*binloop*bldata.numpolproducts[bldata.localfreqindices[i]]*freqchans/chanstoaverage;
          }
        }
      }
    }
    for(int i=0;i<freqtablelength;i++)
    {
      if(configs[c].frequsedbybaseline[i])
      {
        configs[c].coreresultbweightoffset[i] = new int[numbaselines];
        for(int j=0;j<numbaselines;j++)
        {
          bldata = baselinetable[configs[c].baselineindices[j]];
          if(bldata.localfreqindices[i] >= 0)
          {
            configs[c].coreresultbweightoffset[i][j] = coreresultindex;
            //baselineweights are only floats so need to divide by 2...
            toadd = binloop*bldata.numpolproducts[bldata.localfreqindices[i]]/2;
            if(toadd == 0)
              toadd = 1; 
            coreresultindex += toadd;
          }
        }
      }
    }
    for(int i=0;i<numdatastreams;i++)
    {
      dsdata = datastreamtable[configs[c].datastreamindices[i]];
      configs[c].coreresultautocorroffset[i] = coreresultindex;
      for(int j=0;j<getDNumRecordedBands(c, i);j++) {
        if(isFrequencyUsed(c, getDRecordedFreqIndex(c, i, j))) {
          freqindex = getDRecordedFreqIndex(c, i, j);
          freqchans = getFNumChannels(freqindex);
          chanstoaverage = getFChannelsToAverage(freqindex);
          coreresultindex += bandsperautocorr*freqchans/chanstoaverage;
        }
      }
      for(int j=0;j<getDNumZoomBands(c, i);j++) {
        if(isFrequencyUsed(c, getDZoomFreqIndex(c, i, j))) {
          freqindex = getDZoomFreqIndex(c, i, j);
          freqchans = getFNumChannels(freqindex);
          chanstoaverage = getFChannelsToAverage(freqindex);
          coreresultindex += bandsperautocorr*freqchans/chanstoaverage;
        }
      }
    }
    for(int i=0;i<numdatastreams;i++)
    {
      dsdata = datastreamtable[configs[c].datastreamindices[i]];
      configs[c].coreresultacweightoffset[i] = coreresultindex;
      toadd = 0;
      for(int j=0;j<getDNumRecordedBands(c, i);j++) {
        if(isFrequencyUsed(c, getDRecordedFreqIndex(c, i, j))) {
          toadd += bandsperautocorr;
        }
      }
      for(int j=0;j<getDNumZoomBands(c, i);j++) {
        if(isFrequencyUsed(c, getDZoomFreqIndex(c, i, j))) {
          toadd += bandsperautocorr;
        }
      }
      //this will also be just floats, not complex, so need to divide by 2
      toadd /= 2;
      if(toadd == 0)
        toadd = 1;
      coreresultindex += toadd;
    }
    for(int i=0;i<numdatastreams;i++)
    {
      configs[c].coreresultpcaloffset[i] = coreresultindex;
      dsdata = datastreamtable[configs[c].datastreamindices[i]];
      if(dsdata.phasecalintervalmhz > 0)
      {
        for(int j=0;j<getDNumRecordedFreqs(c, i);j++)
          coreresultindex += dsdata.recordedfreqpols[j]*getDRecordedFreqNumPCalTones(c, i, j);
      }
    }
    configs[c].coreresultlength = coreresultindex;
    if(configs[c].coreresultlength > maxcoreresultlength)
      maxcoreresultlength = configs[c].coreresultlength;
  }

  return true;
}

bool Configuration::consistencyCheck()
{
  int tindex, count, freqindex, freq1index, freq2index;
  double bandwidth, sampletimens, ffttime, numffts, f1, f2;
  datastreamdata ds1, ds2;
  baselinedata bl;

  //check entries in the datastream table
  for(int i=0;i<datastreamtablelength;i++)
  {
    //check the telescope index is acceptable
    if(datastreamtable[i].telescopeindex < 0 || datastreamtable[i].telescopeindex >= telescopetablelength)
    {
      if(mpiid == 0) //only write one copy of this error message
        cfatal << startl << "Error!!! Datastream table entry " << i << " has a telescope index (" << datastreamtable[i].telescopeindex << ") that refers outside the telescope table range (table length " << telescopetablelength << ") - aborting!!!" << endl;
      return false;
    }

    //check the recorded bands all refer to valid local freqs
    for(int j=0;j<datastreamtable[i].numrecordedbands;j++)
    {
      if(datastreamtable[i].recordedbandlocalfreqindices[j] < 0 || datastreamtable[i].recordedbandlocalfreqindices[j] >= datastreamtable[i].numrecordedfreqs)
      {
        if(mpiid == 0) //only write one copy of this error message
          cfatal << startl << "Error!!! Datastream table entry " << i << " has an recorded band local frequency index (band " << j << ") which is equal to " << datastreamtable[i].recordedbandlocalfreqindices[j] << " that refers outside the local frequency table range (" << datastreamtable[i].numrecordedfreqs << ") - aborting!!!" << endl;
        return false;
      }
    }

    //check that the zoom mode bands also refer to valid local freqs
    for(int j=0;j<datastreamtable[i].numzoombands;j++)
    {
      if(datastreamtable[i].zoombandlocalfreqindices[j] < 0 || datastreamtable[i].zoombandlocalfreqindices[j] >= datastreamtable[i].numzoomfreqs)
      {
        if(mpiid == 0) //only write one copy of this error message
          cfatal << startl << "Error!!! Datastream table entry " << i << " has an zoom band local frequency index (band " << j << ") which is equal to " << datastreamtable[i].zoombandlocalfreqindices[j] << " that refers outside the local frequency table range (" << datastreamtable[i].numzoomfreqs << ") - aborting!!!" << endl;
        return false;
      }
    }

    //check that all zoom freqs come later in the freq table than regular freqs
    for(int j=0;j<datastreamtable[i].numrecordedfreqs;j++)
    {
      int rfreqtableindex = datastreamtable[i].recordedfreqtableindices[j];
      for(int k=0;k<datastreamtable[i].numzoomfreqs;k++)
      {
        if(datastreamtable[i].zoomfreqtableindices[k] < rfreqtableindex)
        {
          if(mpiid == 0) //only write one copy of this error message
            cfatal << startl << "Error!!! Datastream table entry " << i << " has a zoom band (index " << k << ") which comes earlier in the freq table than a recorded band (index " << j << ") - aborting!!!" << endl;
          return false;
        }
      }
    }

    //check the frequency table indices are ok and all the bandwidths, number of channels, oversampling etc match for the recorded freqs
    bandwidth = freqtable[datastreamtable[i].recordedfreqtableindices[0]].bandwidth;
    int oversamp = freqtable[datastreamtable[i].recordedfreqtableindices[0]].oversamplefactor;
    int decim = freqtable[datastreamtable[i].recordedfreqtableindices[0]].decimationfactor;
    int toaver = freqtable[datastreamtable[i].recordedfreqtableindices[0]].channelstoaverage;
    if(oversamp < decim)
    {
      if(mpiid == 0) //only write one copy of this error message
        cfatal << startl << "Error - oversamplefactor (" << oversamp << ") is less than decimation factor (" << decim << ") - aborting!!!" << endl;
      return false;
    }
    for(int j=0;j<datastreamtable[i].numrecordedfreqs;j++)
    {
      if(datastreamtable[i].recordedfreqtableindices[j] < 0 || datastreamtable[i].recordedfreqtableindices[j] >= freqtablelength)
      {
        if(mpiid == 0) //only write one copy of this error message
          cfatal << startl << "Error!!! Datastream table entry " << i << " has a recorded frequency index (freq " << j << ") which is equal to " << datastreamtable[i].recordedfreqtableindices[j] << " that refers outside the frequency table range (" << freqtablelength << ") - aborting!!!" << endl;
        return false;
      }
      if(bandwidth != freqtable[datastreamtable[i].recordedfreqtableindices[j]].bandwidth)
      {
        if(mpiid == 0) //only write one copy of this error message
          cfatal << startl << "Error - all recorded bandwidths for a given datastream must be equal - Aborting!!!!" << endl;
        return false;
      }
      if(oversamp != freqtable[datastreamtable[i].recordedfreqtableindices[j]].oversamplefactor)
      {
        if(mpiid == 0) //only write one copy of this error message
          cfatal << startl << "Error - all recorded oversample factors for a given datastream must be equal - Aborting!!!!" << endl;
        return false;
      }
      if(decim != freqtable[datastreamtable[i].recordedfreqtableindices[j]].decimationfactor)
      {
        if(mpiid == 0) //only write one copy of this error message
          cfatal << startl << "Error - all recorded decimations for a given datastream must be equal - Aborting!!!!" << endl;
        return false;
      }
      if(toaver != freqtable[datastreamtable[i].recordedfreqtableindices[j]].channelstoaverage)
      {
        if(mpiid == 0) //only write one copy of this error message
          cfatal << startl << "Error - all recorded channels to average for a given datastream must be equal - Aborting!!!!" << endl;
        return false;
      }
    }

    //repeat for the zoom freqs, also check that they fit into a recorded freq and the channel widths match, and the polarisations match
    for(int j=0;j<datastreamtable[i].numzoomfreqs;j++)
    {
      if(datastreamtable[i].zoomfreqtableindices[j] < 0 || datastreamtable[i].zoomfreqtableindices[j] >= freqtablelength)
      {
        if(mpiid == 0) //only write one copy of this error message
          cfatal << startl << "Error!!! Datastream table entry " << i << " has a zoom frequency index (freq " << j << ") which is equal to " << datastreamtable[i].zoomfreqtableindices[j] << " that refers outside the frequency table range (" << freqtablelength << ")- aborting!!!" << endl;
        return false;
      }
      if(datastreamtable[i].zoomfreqparentdfreqindices[j] < 0) {
        if(mpiid == 0) //only write one copy of this error message
          cfatal << startl << "Error!!! Datastream table entry " << i << " has a zoom frequency index (freq " << j << ") which does not fit into any of the recorded bands - aborting!!!" << endl;
        return false;
      }
      double zoomfreqchannelwidth = freqtable[datastreamtable[i].zoomfreqtableindices[j]].bandwidth/freqtable[datastreamtable[i].zoomfreqtableindices[j]].numchannels;
      double parentfreqchannelwidth = freqtable[datastreamtable[i].recordedfreqtableindices[datastreamtable[i].zoomfreqparentdfreqindices[j]]].bandwidth/freqtable[datastreamtable[i].recordedfreqtableindices[datastreamtable[i].zoomfreqparentdfreqindices[j]]].numchannels;
      if(fabs(zoomfreqchannelwidth - parentfreqchannelwidth) > Mode::TINY) {
        if(mpiid == 0) //only write one copy of this error message
          cfatal << startl << "Error!!! Datastream table entry " << i << " has a zoom frequency index (freq " << j << ") whose channel width (" << zoomfreqchannelwidth << ") does not match its parents channel width (" << parentfreqchannelwidth << ") - aborting!!!" << endl;
        return false;
      }
    }

    //check that each zoom band has actually been recorded in the same polarisation
    for(int j=0;j<datastreamtable[i].numzoombands;j++) {
      bool matchingpol = false;
      for(int k=0;k<datastreamtable[i].numrecordedbands;k++) {
        if(datastreamtable[i].zoomfreqparentdfreqindices[datastreamtable[i].zoombandlocalfreqindices[j]] == datastreamtable[i].recordedbandlocalfreqindices[k]) {
          if (datastreamtable[i].zoombandpols[j] == datastreamtable[i].recordedbandpols[k])
            matchingpol = true;
        }
      }
      if(!matchingpol) {
        if(mpiid == 0) //only write one copy of this error message
          cfatal << startl << "Error!!! Datastream table entry " << i << " has a zoom band (band " << j << ") which does have have a parent band of the same polarisation (" << datastreamtable[i].zoombandpols[j] << ") - aborting!" << endl;
        return false;
      }
    }
  }

  //check that for all configs, the datastreams refer to the same telescope
  for(int i=0;i<numdatastreams;i++)
  {
    tindex = datastreamtable[configs[0].datastreamindices[i]].telescopeindex;
    for(int j=1;j<numconfigs;j++)
    {
      if(tindex != datastreamtable[configs[0].datastreamindices[i]].telescopeindex)
      {
        if(mpiid == 0) //only write one copy of this error message
          cfatal << startl << "All configs must have the same telescopes!  Config " << j << " datastream " << i << " refers to different telescopes - aborting!!!" << endl;
        return false;
      }
    }
  }

  //check entries in the config table, check that number of channels * sample time yields a whole number of nanoseconds and that the nanosecond increment is not too large for an int, and generate the ordered datastream indices array
  //also check that guardns is large enough
  int nchan, chantoav;
  double samplens;
  for(int i=0;i<numconfigs;i++)
  {
    //check the fringe rotation settings
    if(configs[i].fringerotationorder < 0 || configs[i].fringerotationorder > 2) {
      if(mpiid == 0) //only write one copy of this error message
        cfatal << startl << "Error - fringe rotation order must be 0, 1 or 2 for all configurations - aborting!" << endl;
      return false;
    }
    //check that arraystridelen is ok, and guardns is ok
    for(int j=0;j<numdatastreams;j++) {
      for(int k=0;k<datastreamtable[configs[i].datastreamindices[j]].numrecordedfreqs;k++) {
        nchan = freqtable[datastreamtable[configs[i].datastreamindices[j]].recordedfreqtableindices[k]].numchannels;
        if(nchan % configs[i].arraystridelen != 0) {
    //for(int j=0;j<freqtablelength;j++) {
      //if(freqtable[j].numchannels % configs[i].arraystridelen != 0) {
          if(mpiid == 0) //only write one copy of this error message
            cfatal << startl << "Error - config[" << i << "] has a stride length of " << configs[i].arraystridelen << " which is not an integral divisor of the number of channels in frequency[" << k << "] of datastream " << j << " (which is " << nchan << ") - aborting!" << endl;
          return false;
        }
      }
      samplens = 1000.0/freqtable[datastreamtable[configs[i].datastreamindices[j]].recordedfreqtableindices[0]].bandwidth;
      if(configs[i].guardns < 4.0*samplens*datastreamtable[configs[i].datastreamindices[j]].bytespersampledenom) {
        if(mpiid == 0) //only write one copy of this error message
          cfatal << startl << "Error - config[" << i << "] has a guard ns which is potentially too short (" << configs[i].guardns << ").  To be safe (against backwards shuffling of the start of a Datastream send) guardns should be at least " << 4.0*samplens*datastreamtable[configs[i].datastreamindices[j]].bytespersampledenom << endl;
        return false;
      }
    }

    //work out the ordereddatastreamindices
    count = 0;
    for(int j=0;j<datastreamtablelength;j++)
    {
      configs[i].ordereddatastreamindices[j] = -1;
      for(int k=0;k<numdatastreams;k++)
      {
        if(configs[i].datastreamindices[k] == j)
          configs[i].ordereddatastreamindices[j] = count++;
      }
    }
    if(count != numdatastreams)
    {
      if(mpiid == 0) //only write one copy of this error message
        cfatal << startl << "Not all datastreams accounted for in the datastream table for config " << i << endl;
      return false;
    }

    //check that the subint time results in a whole number of FFTs for each datastream
    //also that the blockspersend is the same for all datastreams
    for(int j=0;j<numdatastreams;j++)
    {
      sampletimens = 1000.0/(2.0*freqtable[datastreamtable[configs[i].datastreamindices[j]].recordedfreqtableindices[0]].bandwidth);
      ffttime = sampletimens*freqtable[datastreamtable[configs[i].datastreamindices[j]].recordedfreqtableindices[0]].numchannels*2;
      numffts = configs[i].subintns/ffttime;
      if(ffttime - (int)(ffttime+0.5) > 0.00000001 || ffttime - (int)(ffttime+0.5) < -0.000000001)
      {
        if(mpiid == 0) //only write one copy of this error message
          cfatal << startl << "Error - FFT chunk time for config " << i << ", datastream " << j << " is not a whole number of nanoseconds (" << ffttime << ") - aborting!!!" << endl;
        return false;
      }
      if(fabs(numffts - int(numffts+0.5)) > Mode::TINY) {
        if(mpiid == 0) //only write one copy of this error message
          cfatal << startl << "Error - Send of size " << configs[i].subintns << " does not yield an integer number of FFTs for datastream " << j << " in config " << i << " - ABORTING" << endl;
        return false;
      }
      if(((double)configs[i].subintns)*(databufferfactor/numdatasegments) > ((1 << (sizeof(int)*8 - 1)) - 1))
      {
        if(mpiid == 0) //only write one copy of this error message
          cfatal << startl << "Error - increment per read in nanoseconds is " << ((double)configs[i].subintns)*(databufferfactor/numdatasegments) << " - too large to fit in an int.  ABORTING" << endl;
        return false;
      }
      for (int k=1;k<getDNumRecordedFreqs(i,j);k++) {
        freqdata f = freqtable[datastreamtable[configs[i].datastreamindices[j]].recordedfreqtableindices[k]];
        if (fabs((1000.0*f.numchannels)/f.bandwidth) - ffttime > Mode::TINY) {
          if(mpiid == 0) //only write one copy of this error message
            cfatal << startl << "Error - frequency " << k << " of datastream " << j << " of config " << i << " has a different bandwidth or num channels to the other freqs of this datastream - aborting!!!" << endl;
          return false;
        }
      }
    }

    //check that all baseline indices refer inside the table, and go in ascending order
    int b, lastt1 = 0, lastt2 = 0;
    for(int j=0;j<numbaselines;j++)
    {
      b = configs[i].baselineindices[j];
      if(b < 0 || b >= baselinetablelength) //bad index
      {
        if(mpiid == 0) //only write one copy of this error message
          cfatal << startl << "Error - config " << i << " baseline index " << j << " refers to baseline " << b << " which is outside the range of the baseline table - aborting!!!" << endl;
        return false;
      }
      if(datastreamtable[baselinetable[b].datastream2index].telescopeindex < lastt2 && datastreamtable[baselinetable[b].datastream1index].telescopeindex <= lastt1)
      {
        if(mpiid == 0) //only write one copy of this error message
          cfatal << startl << "Error - config " << i << " baseline index " << j << " refers to baseline " << datastreamtable[baselinetable[b].datastream2index].telescopeindex << "-" << datastreamtable[baselinetable[b].datastream1index].telescopeindex << " which is out of order with the previous baseline " << lastt1 << "-" << lastt2 << " - aborting!!!" << endl;
        return false;
      }
      lastt1 = datastreamtable[baselinetable[b].datastream1index].telescopeindex;
      lastt2 = datastreamtable[baselinetable[b].datastream2index].telescopeindex;
    }

    for(int j=0;j<numbaselines;j++)
    {
      bl = baselinetable[configs[i].baselineindices[j]];
      for(int k=0;k<bl.numfreqs;k++)
      {
        chantoav = freqtable[bl.freqtableindices[k]].channelstoaverage;
        nchan = freqtable[bl.freqtableindices[k]].numchannels;
        if(configs[i].xmacstridelen%chantoav != 0 && chantoav%configs[i].xmacstridelen != 0)
        {
          if(mpiid == 0) //only write one copy of this error message
            cfatal << startl << "Error - config[" << i << "] has an xmac stride length of " << configs[i].xmacstridelen << " which is not 2^N x the channels to average in frequency [" << k << "] of baseline " << j << " (which is " << chantoav << ") - aborting!" << endl;
          return false;
        }
        if(nchan%configs[i].xmacstridelen != 0)
        {
          if(mpiid == 0) //only write one copy of this error message
            cfatal << startl << "Error - config[" << i << "] has an xmac stride length of " << configs[i].xmacstridelen << " which is not an integer divisor of the number of channels in frequency[" << k << "] of baseline " << j << " (which is " << nchan << ") - aborting!" << endl;
          return false;
        }
      }
    }
  }


  //check the baseline table entries
  for(int i=0;i<baselinetablelength;i++)
  {
    //check the datastream indices
    if(baselinetable[i].datastream1index < 0 || baselinetable[i].datastream2index < 0 || baselinetable[i].datastream1index >= datastreamtablelength || baselinetable[i].datastream2index >= datastreamtablelength)
    {
      if(mpiid == 0) //only write one copy of this error message
        cfatal << startl << "Error - baseline table entry " << i << " has a datastream index outside the datastream table range! Its two indices are " << baselinetable[i].datastream1index << ", " << baselinetable[i].datastream2index << ".  ABORTING" << endl;
      return false;
    }

    ds1 = datastreamtable[baselinetable[i].datastream1index];
    ds2 = datastreamtable[baselinetable[i].datastream2index];
    for(int j=0;j<baselinetable[i].numfreqs;j++)
    {
      freq1index = baselinetable[i].freqtableindices[j];
      if(baselinetable[i].datastream2bandindex[j][0] >= ds2.numrecordedbands) //zoom band
        freq2index = ds2.zoomfreqtableindices[ds2.zoombandlocalfreqindices[baselinetable[i].datastream2bandindex[j][0]]-ds2.numrecordedbands];
      else
        freq2index = ds2.recordedfreqtableindices[ds2.recordedbandlocalfreqindices[baselinetable[i].datastream2bandindex[j][0]]];
      if(freq1index != freq2index)
      {
        //these had better be compatible, otherwise bail
        f1 = freqtable[freq1index].bandedgefreq;
        f2 = freqtable[freq2index].bandedgefreq;
        if(freqtable[freq1index].lowersideband)
          f1 -= freqtable[freq1index].bandwidth;
        if(freqtable[freq2index].lowersideband)
          f2 -= freqtable[freq2index].bandwidth;
        if(freqtable[freq1index].bandedgefreq == freqtable[freq2index].bandedgefreq &&  freqtable[freq1index].bandedgefreq == freqtable[freq2index].bandedgefreq)
        {
          //different freqs, same value??
          if(mpiid == 0) //only write one copy of this error message
            cwarn << startl << "Baseline " << i << " frequency " << j << " points at two different frequencies that are apparently identical - this is not wrong, but very strange.  Check the input file" << endl;
        }
        else if(f1 == f2 && freqtable[freq1index].bandwidth == freqtable[freq2index].bandwidth)
        {
          //correlating a USB with an LSB
          if(mpiid == 0) //only write one copy of this error message
            cinfo << startl << "Baseline " << i << " frequency " << j << " is correlating an USB frequency with a LSB frequency" << endl;
          if(freqtable[freq1index].lowersideband)
            baselinetable[i].oddlsbfreqs[j] = 1; //datastream1 has the LSB (2 is USB)
          else
            baselinetable[i].oddlsbfreqs[j] = 2; //datastream2 has the LSB (1 is USB)
        }
        else
        {
          if(mpiid == 0) //only write one copy of this error message
            cwarn << startl << "Warning! Baseline table entry " << i << ", frequency " << j << " is trying to correlate two different frequencies!  Correlation will go on, but the results for these bands will probably be garbage!" << endl;
          if(freqtable[freq1index].lowersideband && !freqtable[freq2index].lowersideband)
            baselinetable[i].oddlsbfreqs[j] = 1;
          else if(freqtable[freq2index].lowersideband && !freqtable[freq1index].lowersideband)
            baselinetable[i].oddlsbfreqs[j] = 2;
        }
      }
      for(int k=0;k<baselinetable[i].numpolproducts[j];k++)
      {
        //check the band indices
        if((baselinetable[i].datastream1bandindex[j][k] < 0) || (baselinetable[i].datastream1bandindex[j][k] >= (ds1.numrecordedbands + ds1.numzoombands)))
        {
          if(mpiid == 0) //only write one copy of this error message
            cfatal << startl << "Error! Baseline table entry " << i << ", frequency " << j << ", polarisation product " << k << " for datastream 1 refers to a band outside datastream 1's range (" << baselinetable[i].datastream1bandindex[j][k] << ") - aborting!!!" << endl;
          return false;
        }
        ds1 = datastreamtable[baselinetable[i].datastream2index];
        if((baselinetable[i].datastream2bandindex[j][k] < 0) || (baselinetable[i].datastream2bandindex[j][k] >= (ds1.numrecordedbands + ds1.numzoombands)))
        {
          if(mpiid == 0) //only write one copy of this error message
            cfatal << startl << "Error! Baseline table entry " << i << ", frequency " << j << ", polarisation product " << k << " for datastream 2 refers to a band outside datastream 2's range (" << baselinetable[i].datastream2bandindex[j][k] << ") - aborting!!!" << endl;
          return false;
        }

        //check that the freqs pointed at match
        if(baselinetable[i].datastream1bandindex[j][k] >= ds1.numrecordedbands) //zoom band
          freqindex = ds1.zoomfreqtableindices[ds1.zoombandlocalfreqindices[baselinetable[i].datastream1bandindex[j][k]]-ds1.numrecordedbands];
        else
          freqindex = ds1.recordedfreqtableindices[ds1.recordedbandlocalfreqindices[baselinetable[i].datastream1bandindex[j][k]]];
        if(freqindex != freq1index)
        {
          if(mpiid == 0) //only write one copy of this error message
            cfatal << startl << "Error! Baseline table entry " << i << ", frequency " << j << ", polarisation product " << k << " for datastream 1 does not match the frequency of the first polarisation product! Aborting." << endl;
          return false;
        }
        if(baselinetable[i].datastream2bandindex[j][k] >= ds2.numrecordedbands) //zoom band
          freqindex = ds2.zoomfreqtableindices[ds2.zoombandlocalfreqindices[baselinetable[i].datastream2bandindex[j][0]]-ds2.numrecordedbands];
        else
          freqindex = ds2.recordedfreqtableindices[ds2.recordedbandlocalfreqindices[baselinetable[i].datastream2bandindex[j][k]]];
        if(freqindex != freq2index)
        {
          if(mpiid == 0) //only write one copy of this error message
            cfatal << startl << "Error! Baseline table entry " << i << ", frequency " << j << ", polarisation product " << k << " for datastream 2 does not match the frequency of the first polarisation product! Aborting." << endl;
          return false;
        }
      }
    }
  }

  //for each config, check if there are any USB x LSB correlations 
  for(int i=0;i<numconfigs;i++)
  {
    configs[i].anyusbxlsb = false;
    for(int j=0;j<numbaselines;j++)
    {
      for(int k=0;k<baselinetable[j].numfreqs;k++)
      {
        if(baselinetable[j].oddlsbfreqs[k] > 0)
          configs[i].anyusbxlsb = true;
      }
    }
  }

  if(databufferfactor % numdatasegments != 0)
  {
    if(mpiid == 0) //only write one copy of this error message
      cfatal << startl << "There must be an integer number of sends per datasegment.  Presently databufferfactor is " << databufferfactor << ", and numdatasegments is " << numdatasegments << ".  ABORTING" << endl;
    return false;
  }

  return true;
}

bool Configuration::processPhasedArrayConfig(string filename, int configindex)
{
  string line;

  if(mpiid == 0) //only write one copy of this info message
    cinfo << startl << "About to process phased array file " << filename << endl;
  ifstream phasedarrayinput(filename.c_str(), ios::in);
  if(!phasedarrayinput.is_open() || phasedarrayinput.bad())
  {
    if(mpiid == 0) //only write one copy of this error message
      cfatal << startl << "Could not open phased array config file " << line << " - aborting!!!" << endl;
    return false;
  }
  getinputline(&phasedarrayinput, &line, "OUTPUT TYPE");
  getinputline(&phasedarrayinput, &line, "OUTPUT FORMAT");
  getinputline(&phasedarrayinput, &line, "ACC TIME (NS)");
  getinputline(&phasedarrayinput, &line, "COMPLEX OUTPUT");
  getinputline(&phasedarrayinput, &line, "OUTPUT BITS");
  phasedarrayinput.close();
  return true;
}

bool Configuration::processPulsarConfig(string filename, int configindex)
{
  int numpolycofiles, ncoefficients, polycocount;
  string line;
  string * polycofilenames;
  double * binphaseends;
  double * binweights;
  int * numsubpolycos;
  char psrline[128];
  ifstream temppsrinput;

  if(mpiid == 0) //only write one copy of this info message
    cinfo << startl << "About to process pulsar file " << filename << endl;
  ifstream pulsarinput(filename.c_str(), ios::in);
  if(!pulsarinput.is_open() || pulsarinput.bad())
  {
    if(mpiid == 0) //only write one copy of this error message
      cfatal << startl << "Could not open pulsar config file " << line << " - aborting!!!" << endl;
    return false;
  }
  getinputline(&pulsarinput, &line, "NUM POLYCO FILES");
  numpolycofiles = atoi(line.c_str());
  polycofilenames = new string[numpolycofiles];
  numsubpolycos = new int[numpolycofiles];
  configs[configindex].numpolycos = 0;
  for(int i=0;i<numpolycofiles;i++)
  {
    getinputline(&pulsarinput, &(polycofilenames[i]), "POLYCO FILE");
    numsubpolycos[i] = 0;
    temppsrinput.open(polycofilenames[i].c_str());
    temppsrinput.getline(psrline, 128);
    temppsrinput.getline(psrline, 128);
    while(!(temppsrinput.eof() || temppsrinput.fail())) {
      psrline[54] = '\0';
      ncoefficients = atoi(&(psrline[49]));
      for(int j=0;j<ncoefficients/3 + 2;j++)
        temppsrinput.getline(psrline, 128);
      numsubpolycos[i]++;
      configs[configindex].numpolycos++;
    }
    temppsrinput.close();
  }
  getinputline(&pulsarinput, &line, "NUM PULSAR BINS");
  configs[configindex].numbins = atoi(line.c_str());
  if(configs[configindex].numbins > maxnumpulsarbins)
    maxnumpulsarbins = configs[configindex].numbins;
  binphaseends = new double[configs[configindex].numbins];
  binweights = new double[configs[configindex].numbins];
  getinputline(&pulsarinput, &line, "SCRUNCH OUTPUT");
  configs[configindex].scrunchoutput = ((line == "TRUE") || (line == "T") || (line == "true") || (line == "t"))?true:false;
  for(int i=0;i<configs[configindex].numbins;i++)
  {
    getinputline(&pulsarinput, &line, "BIN PHASE END");
    binphaseends[i] = atof(line.c_str());
    getinputline(&pulsarinput, &line, "BIN WEIGHT");
    binweights[i] = atof(line.c_str());
  }

  //create the polycos
  configs[configindex].polycos = new Polyco*[configs[configindex].numpolycos];
  polycocount = 0;
  for(int i=0;i<numpolycofiles;i++)
  {
    for(int j=0;j<numsubpolycos[i];j++)
    {
      //cinfo << startl << "About to create polyco file " << polycocount << " from filename " << polycofilenames[i] << ", subcount " << j << endl;
      configs[configindex].polycos[polycocount] = new Polyco(polycofilenames[i], j, configindex, configs[configindex].numbins, getMaxNumChannels(), binphaseends, binweights, double(configs[configindex].subintns)/60000000000.0);
      if (!configs[configindex].polycos[polycocount]->initialisedOK())
        return false;
      estimatedbytes += configs[configindex].polycos[polycocount]->getEstimatedBytes();
      polycocount++;
    } 
  }
  
  delete [] binphaseends;
  delete [] binweights;
  delete [] polycofilenames;
  delete [] numsubpolycos;
  pulsarinput.close();
  return true;
}

bool Configuration::setPolycoFreqInfo(int configindex)
{
  bool ok = true;
  datastreamdata d = datastreamtable[getMaxNumFreqDatastreamIndex(configindex)];
  double * frequencies = new double[freqtablelength];
  double * bandwidths = new double[freqtablelength];
  int * numchannels = new int[freqtablelength];
  bool * used = new bool[freqtablelength];
  for(int i=0;i<freqtablelength;i++)
  {
    frequencies[i] = freqtable[i].bandedgefreq;
    if(freqtable[i].lowersideband)
      frequencies[i] -= ((double)(freqtable[i].numchannels-1))*freqtable[i].bandwidth/((double)freqtable[i].numchannels);
    bandwidths[i] = freqtable[i].bandwidth;
    numchannels[i] = freqtable[i].numchannels;
    used[i] = configs[configindex].frequsedbybaseline[i];
  }
  for(int i=0;i<configs[configindex].numpolycos;i++)
  {
    ok = ok && configs[configindex].polycos[i]->setFrequencyValues(freqtablelength, frequencies, bandwidths, numchannels, used);
  }
  delete [] frequencies;
  delete [] bandwidths;
  delete [] numchannels;
  delete [] used;
  return ok;
}

void Configuration::makeFortranString(string line, int length, char * destination)
{
  int linelength = line.length();
  
  if(linelength <= length)
  {
    strcpy(destination, line.c_str());
    for(int i=0;i<length-linelength;i++)
      destination[i+linelength] = ' ';
  }
  else
  {
    strcpy(destination, (line.substr(0, length-1)).c_str());
    destination[length-1] = line.at(length-1);
  }
}

void Configuration::getinputkeyval(ifstream * input, std::string * key, std::string * val)
{
  if(input->eof())
    cerror << startl << "Error - trying to read past the end of file!!!" << endl;
  getline(*input, *key);
  while(key->length() > 0 && key->at(0) == COMMENT_CHAR) { // a comment
    //if(mpiid == 0) //only write one copy of this error message
    //  cverbose << startl << "Skipping comment " << key << endl;
    getline(*input, *key);
  }
  int keylength = key->find_first_of(':') + 1;
  if(keylength < DEFAULT_KEY_LENGTH)
    keylength = DEFAULT_KEY_LENGTH;
  *val = key->substr(keylength);
  *key = key->substr(0, key->find_first_of(':'));
}

void Configuration::getinputline(ifstream * input, std::string * line, std::string startofheader)
{
  if(input->eof())
    cerror << startl << "Trying to read past the end of file!!!" << endl;
  getline(*input,*line);
  while(line->length() > 0 && line->at(0) == COMMENT_CHAR) { // a comment
    //if(mpiid == 0) //only write one copy of this error message
    //  cverbose << startl << "Skipping comment " << line << endl;
    getline(*input, *line);
  }
  int keylength = line->find_first_of(':') + 1;
  if(keylength < DEFAULT_KEY_LENGTH)
    keylength = DEFAULT_KEY_LENGTH;
  if(startofheader.compare((*line).substr(0, startofheader.length())) != 0) //not what we expected
    cerror << startl << "Error - we thought we were reading something starting with '" << startofheader << "', when we actually got '" << (*line).substr(0, keylength) << "'" << endl;
  *line = line->substr(keylength);
}

void Configuration::getinputline(ifstream * input, std::string * line, std::string startofheader, int intval)
{
  char buffer[MAX_KEY_LENGTH+1];
  sprintf(buffer, "%s%i", startofheader.c_str(), intval);
  getinputline(input, line, string(buffer));
}

void Configuration::getMJD(int & d, int & s, int year, int month, int day, int hour, int minute, int second)
{
  d = year*367 - int(7*(year + int((month + 9)/12))/4) + int(275*month/9) + day - 678987;

  s = 3600*hour + 60*minute + second;
}

void Configuration::mjd2ymd(int mjd, int & year, int & month, int & day)
{
  int j = mjd + 32044 + 2400001;
  int g = j / 146097;
  int dg = j % 146097;
  int c = ((dg/36524 + 1)*3)/4;
  int dc = dg - c*36524;
  int b = dc / 1461;
  int db = dc % 1461;
  int a = ((db/365 + 1)*3)/4;
  int da = db - a*365;
  int y = g*400 + c*100 + b*4 + a;
  int m = (da*5 + 308)/153 - 2;
  int d = da - ((m + 4)*153)/5 + 122;
  
  year = y - 4800 + (m + 2)/12;
  month = (m + 2)%12 + 1;
  day = d + 1;
}

// vim: shiftwidth=2:softtabstop=2:expandtab:
