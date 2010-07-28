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
#include "config.h"
#include "visibility.h"
#include "core.h"
#include "datastream.h"
#include <dirent.h>
#include <cmath>
#include <string>
#include <string.h>
#include <stdio.h>
#include <iomanip>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <difxmessage.h>
#include "alert.h"

Visibility::Visibility(Configuration * conf, int id, int numvis, char * dbuffer, int dbufferlen, int eseconds, int scan, int scanstartsec, int startns, const string * pnames)
  : config(conf), visID(id), numvisibilities(numvis), todiskbuffer(dbuffer), todiskbufferlength(dbufferlen), currentscan(scan), executeseconds(eseconds), currentstartseconds(scanstartsec), currentstartns(startns), polnames(pnames)
{
  int status, binloop;

  //cverbose << startl << "About to create visibility " << id << "/" << numvis << endl;
  estimatedbytes = 0;
  model = config->getModel();

  maxproducts = config->getMaxProducts();
  autocorrwidth = 1;
  if (maxproducts > 2 && config->writeAutoCorrs(config->getScanConfigIndex(currentscan)))
    autocorrwidth = 2;
  first = true;
  configuredok = true;
  currentsubints = 0;
  numdatastreams = config->getNumDataStreams();
  resultlength = config->getMaxCoreResultLength();
  results = vectorAlloc_cf32(resultlength);
  floatresults = (f32*)results;
  estimatedbytes += 8*resultlength; //for the results
  estimatedbytes += 4*(config->getNumDataStreams() + config->getNumBaselines())*config->getDNumTotalBands(0,0); //a rough stab at the calibration arrays
  status = vectorZero_cf32(results, resultlength);
  if(status != vecNoErr)
    csevere << startl << "Error trying to zero when creating visibility " << visID << endl;
  numbaselines = config->getNumBaselines();
  currentconfigindex = config->getScanConfigIndex(currentscan);
  expermjd = config->getStartMJD();
  experseconds = config->getStartSeconds();
  offsetns = 0;
  changeConfig(currentconfigindex);
  maxfiles = 1;
  for(int i=0;i<model->getNumScans();i++)
  {
    binloop = 1;
    if(config->pulsarBinOn(config->getScanConfigIndex(i)) && !config->scrunchOutputOn(config->getScanConfigIndex(i)))
      binloop = config->getNumPulsarBins(config->getScanConfigIndex(i));
    if(model->getNumPhaseCentres(i)*binloop > maxfiles)
      maxfiles = model->getNumPhaseCentres(i)*binloop;
  }
  todiskmemptrs = new int[maxfiles];
  estimatedbytes += maxfiles*4;

  //set up the initial time period this Visibility will be responsible for
  offsetns = offsetns + offsetnsperintegration;
  subintsthisintegration = (int)(((long long)(config->getIntTime(currentconfigindex)*1000000000.0))/config->getSubintNS(currentconfigindex));
  if(offsetns > config->getSubintNS(currentconfigindex)/2)
  {
    offsetns -= config->getSubintNS(currentconfigindex)/2;
    subintsthisintegration++;
  }
  for(int i=0;i<visID;i++)
    updateTime();
}


Visibility::~Visibility()
{
  vectorFree(results);
  for(int i=0;i<numdatastreams;i++)
    delete [] autocorrcalibs[i];
  delete [] autocorrcalibs;

  for(int i=0;i<numbaselines;i++)
  {
    for(int j=0;j<config->getBNumFreqs(currentconfigindex, i);j++)
      delete [] baselineweights[i][j];
    delete [] baselineweights[i];
  }
  delete [] baselineweights;

  if(pulsarbinon) {
    for(int i=0;i<config->getFreqTableLength();i++) {
      for(int j=0;j<config->getFNumChannels(i)+1;j++)
        vectorFree(binweightsums[i][j]);
      for(int j=0;j<((config->scrunchOutputOn(currentconfigindex))?1:config->getNumPulsarBins(currentconfigindex));j++)
        vectorFree(binscales[i][j]);
      delete [] binweightsums[i];
      delete [] binscales[i];
    }
    delete [] binweightsums;
    delete [] binscales;
    vectorFree(binweightdivisor);
  }
}

bool Visibility::addData(cf32* subintresults)
{
  int status;

  status = vectorAdd_cf32_I(subintresults, results, resultlength);
  if(status != vecNoErr)
    csevere << startl << "Error copying results in Vis. " << visID << endl;
  currentsubints++;

  if(currentsubints>subintsthisintegration)
    cerror << startl << "Somehow Visibility " << visID << " ended up with " << currentsubints << " subintegrations - was expecting only " << subintsthisintegration << endl;

  return (currentsubints>=subintsthisintegration); //are we finished integrating?
}

string sec2time(const int& sec) {
  ostringstream oss;
  oss << setfill('0');
  oss << setw(2) << sec/3600 << ":" << setw(2) << (sec/60)%60 << ":" << setw(2) << sec%60; 
  return oss.str();
}

void Visibility::increment()
{
  int status;
  int sec = experseconds + model->getScanStartSec(currentscan, expermjd, experseconds) + currentstartseconds;

  cinfo << startl << "Vis. " << visID << " is incrementing, since currentsubints = " << currentsubints << ".  The approximate mjd/seconds is " << expermjd + sec/86400 << "/" << (sec)%86400 << endl;

  currentsubints = 0;
  for(int i=0;i<numvisibilities;i++) //adjust the start time and offset
    updateTime();

  status = vectorZero_cf32(results, resultlength);
  if(status != vecNoErr)
    csevere << startl << "Error trying to zero when incrementing visibility " << visID << endl;

  if(pulsarbinon) {
    for(int i=0;i<config->getFreqTableLength();i++) {
      for(int j=0;j<config->getFNumChannels(i)+1;j++) {
        if(config->scrunchOutputOn(currentconfigindex))
        {
          binweightsums[i][j][0] = 0.0;
        }
        else
        {
          status = vectorZero_f32(binweightsums[i][j], config->getNumPulsarBins(currentconfigindex));
          if(status != vecNoErr)
            csevere << startl << "Error trying to zero binweightsums when incrementing visibility " << visID << endl;
        }
      }
    }
  }
}

void Visibility::updateTime()
{
  int configindex;
  if (currentscan >= model->getNumScans()) {
    //already past the end, just return
    return;
  }

  offsetns = offsetns+offsetnsperintegration;
  subintsthisintegration = (int)(((long long)(config->getIntTime(currentconfigindex)*1000000000.0))/config->getSubintNS(currentconfigindex));
  if(offsetns > config->getSubintNS(currentconfigindex)/2)
  {
    offsetns -= config->getSubintNS(currentconfigindex);
    subintsthisintegration++;
  }

  currentstartseconds += (int)config->getIntTime(currentconfigindex);
  currentstartns += (int)((config->getIntTime(currentconfigindex)-(int)config->getIntTime(currentconfigindex))*1000000000 + 0.5);
  currentstartseconds += currentstartns/1000000000;
  currentstartns %= 1000000000;

  if(currentscan < model->getNumScans() && currentstartseconds >= model->getScanDuration(currentscan)) {
    currentscan++;
    currentstartseconds = 0;
    currentstartns = 0;
    offsetns = offsetnsperintegration;
    subintsthisintegration = (int)(((long long)(config->getIntTime(currentconfigindex)*1000000000.0))/config->getSubintNS(currentconfigindex));
    if(offsetns > config->getSubintNS(currentconfigindex)/2)
    {
      offsetns -= config->getSubintNS(currentconfigindex);
      subintsthisintegration++;
    }
  }
  else if((double)model->getScanDuration(currentscan) - (double)currentstartseconds - ((double)currentstartns)/1000000000.0 < config->getIntTime(currentconfigindex)) {
    //This will be an incomplete subintegration - recalculate subintsthisintegration
    subintsthisintegration -= ((long long)(1000000000.0*currentstartseconds + (double)currentstartns +  1000000000.0*config->getIntTime(currentconfigindex) - 1000000000.0*model->getScanDuration(currentscan)))/((long long)config->getSubintNS(currentconfigindex));
  }

  configindex = 0;
  if(currentscan < model->getNumScans())
    configindex = config->getScanConfigIndex(currentscan);
  while(configindex < 0 && currentscan < model->getNumScans())
    configindex = config->getScanConfigIndex(++currentscan);

  if(configindex != currentconfigindex && currentscan < model->getNumScans())
  {
    changeConfig(configindex);
  }
}


void Visibility::copyVisData(char **buf, int *bufsize, int *nbuf) {
  char *ptr;
  int ntowrite, nwrot, i;
  int32_t atsec, datasize, numchans;


  if (currentsubints==0) { // Nothing to send
    *nbuf = -1;
    return;
  }

  atsec = currentstartseconds+experseconds;
  datasize = resultlength;
  numchans = config->getFNumChannels(currentconfigindex);

  ntowrite = (4+4+4+resultlength*sizeof(cf32));

  if (*bufsize < ntowrite) {
    if (*bufsize>0) delete [] *buf;
    *buf = new char[ntowrite];
    *bufsize = ntowrite;
  }

  ptr = *buf;

  memcpy(ptr, &atsec, 4);
  ptr +=4;

  memcpy(ptr, &datasize, 4);
  ptr +=4;

  memcpy(ptr, &numchans, 4);
  ptr +=4;

  memcpy(ptr, results, resultlength*sizeof(cf32));
  *nbuf = ntowrite;

  return;
}

void Visibility::writedata()
{
  f32 scale, divisor;
  int ds1, ds2, ds1bandindex, ds2bandindex, localfreqindex, freqindex, freqchannels;
  int status, resultindex, binloop;
  int dumpmjd, intsec;
  double dumpseconds;

  cdebug << startl << "Vis. " << visID << " is starting to write out data" << endl;

  if(currentscan >= model->getNumScans() || currentstartseconds + model->getScanStartSec(currentscan, expermjd, experseconds) >= executeseconds)
  {
    //cdebug << startl << "Vis. " << visID << " is not writing out any data, since the time is past the end of the correlation" << endl;
    return; //NOTE EXIT HERE!!!
  }

  intsec = experseconds + model->getScanStartSec(currentscan, expermjd, experseconds) + currentstartseconds;
  dumpmjd = expermjd + intsec/86400;
  dumpseconds = double(intsec%86400) + ((double)currentstartns)/1000000000.0 + config->getIntTime(currentconfigindex)/2.0;
  if(dumpseconds > 86400.0) {
    dumpmjd++;
    dumpseconds -= 86400.0;
  }

  if(currentsubints == 0) //nothing to write out
  {
    return; //NOTE EXIT HERE!!!
  }

  if(config->pulsarBinOn(currentconfigindex) && !config->scrunchOutputOn(currentconfigindex))
    binloop = config->getNumPulsarBins(currentconfigindex);
  else
    binloop = 1;

  for(int i=0;i<numbaselines;i++) {
    //grab the baseline weights
    for(int j=0;j<config->getBNumFreqs(currentconfigindex,i);j++) {
      freqindex = config->getBFreqIndex(currentconfigindex, i, j);
      resultindex = config->getCoreResultBWeightOffset(currentconfigindex, freqindex, i)*2;
      for(int b=0;b<binloop;b++) {
        for(int k=0;k<config->getBNumPolProducts(currentconfigindex, i, j);k++) {
          if(binloop>1)
            baselineweights[i][j][b][k] = floatresults[resultindex]/(fftsperintegration*polyco->getBinWidth(b));
          else
            baselineweights[i][j][b][k] = floatresults[resultindex]/fftsperintegration;
          resultindex++;
        }
      }
    }
  }

  for(int i=0;i<numdatastreams;i++)
  {
    //grab the autocorrelation weights
    resultindex = config->getCoreResultACWeightOffset(currentconfigindex, i)*2;
    for(int j=0;j<autocorrwidth;j++)
    {
      for(int k=0;k<config->getDNumTotalBands(currentconfigindex, i); k++)
      {
        freqindex = config->getDTotalFreqIndex(currentconfigindex, i, k);
        if(config->isFrequencyUsed(currentconfigindex, freqindex)) {
          autocorrweights[i][j][k] = floatresults[resultindex]/fftsperintegration;
          resultindex++;
        }
        else
          autocorrweights[i][j][k] = 0.0;
      }
    }
  }

  //if needed work out the band average, for use in calibration (allows us to calculate fractional correlation)
  for(int i=0;i<numdatastreams;i++)
  {
    if(config->getDTsys(currentconfigindex, i) > 0.0)
    {
      resultindex = config->getCoreResultAutocorrOffset(currentconfigindex, i);
      for(int k=0;k<config->getDNumTotalBands(currentconfigindex, i); k++)
      {
        freqindex = config->getDTotalFreqIndex(currentconfigindex, i, k);
        if(config->isFrequencyUsed(currentconfigindex, freqindex)) {
          freqchannels = config->getFNumChannels(freqindex)/config->getFChannelsToAverage(freqindex);
          status = vectorMean_cf32(&(results[resultindex]), freqchannels, &autocorrcalibs[i][k], vecAlgHintFast);
          if(status != vecNoErr)
            csevere << startl << "Error in getting average of autocorrelation!!!" << status << endl;
          resultindex += freqchannels;
        }
      }
    }
  }

  for(int i=0;i<numbaselines;i++) //calibrate each baseline
  {
    ds1 = config->getBOrderedDataStream1Index(currentconfigindex, i);
    ds2 = config->getBOrderedDataStream2Index(currentconfigindex, i);
    for(int j=0;j<config->getBNumFreqs(currentconfigindex,i);j++) //do each frequency
    {
      freqindex = config->getBFreqIndex(currentconfigindex, i, j);
      resultindex = config->getCoreResultBaselineOffset(currentconfigindex, freqindex, i);
      freqchannels = config->getFNumChannels(freqindex)/config->getFChannelsToAverage(freqindex);
      for(int s=0;s<model->getNumPhaseCentres(currentscan);s++)
      {
        for(int b=0;b<binloop;b++)
        {
          for(int k=0;k<config->getBNumPolProducts(currentconfigindex, i, j);k++) //do each product of this frequency eg RR,LL,RL,LR
          {
            ds1bandindex = config->getBDataStream1BandIndex(currentconfigindex, i, j, k);
            ds2bandindex = config->getBDataStream2BandIndex(currentconfigindex, i, j, k);
            if(config->getDTsys(currentconfigindex, ds1) > 0.0 && config->getDTsys(currentconfigindex, ds2) > 0.0)
            {
              divisor = (Mode::getDecorrelationPercentage(config->getDNumBits(currentconfigindex, ds1)))*(Mode::getDecorrelationPercentage(config->getDNumBits(currentconfigindex, ds2)))*autocorrcalibs[ds1][ds1bandindex].re*autocorrcalibs[ds2][ds2bandindex].re;
              if(divisor > 0.0) //only do it if there is something to calibrate with
                scale = sqrt(config->getDTsys(currentconfigindex, ds1)*config->getDTsys(currentconfigindex, ds2)/divisor);
              else
                scale = 0.0;
            }
            else
            {
              //We want normalised correlation coefficients, so scale by number of contributing
              //samples rather than datastream tsys and decorrelation correction
              if(baselineweights[i][j][b][k] > 0.0)
                scale = 1.0/(baselineweights[i][j][b][k]*meansubintsperintegration*((float)(config->getBlocksPerSend(currentconfigindex)*2*freqchannels*config->getFChannelsToAverage(freqindex))));
              else
                scale = 0.0;
            }

            //amplitude calibrate the data
            if(scale > 0.0)
            {
              //cout << "Scaling baseline (found at resultindex " << resultindex << ") by " << scale << ", before scaling the 6th re and im are " << floatresults[resultindex*2 + 12] << ", " << floatresults[resultindex*2 + 13] << endl;
              status = vectorMulC_f32_I(scale, &(floatresults[resultindex*2]), 2*freqchannels);
              if(status != vecNoErr)
                csevere << startl << "Error trying to amplitude calibrate the baseline data!!!" << endl;
            }
            resultindex += freqchannels;
          }
        }
      }
    }
  }

  if(config->writeAutoCorrs(currentconfigindex)) //if we need to, calibrate the autocorrs
  {
    for(int i=0;i<numdatastreams;i++) //do each datastream
    {
      resultindex = config->getCoreResultAutocorrOffset(currentconfigindex, i)*2;
      for(int j=0;j<autocorrwidth;j++) //the parallel, (and the cross if needed) product for which this band is the first
      {
        for(int k=0;k<config->getDNumTotalBands(currentconfigindex, i); k++)
        {
          freqindex = config->getDTotalFreqIndex(currentconfigindex, i, k);
          if(config->isFrequencyUsed(currentconfigindex, freqindex)) {
            freqchannels = config->getFNumChannels(freqindex)/config->getFChannelsToAverage(freqindex);
            scale = 0.0;
            //calibrate the data
            if(config->getDTsys(currentconfigindex, i) > 0.0)
            {
              //we want to calibrate "online" with a-priori tsys and band average of autocorrelations
              divisor = sqrt(autocorrcalibs[i][k].re*autocorrcalibs[i][(j==0)?k:config->getDMatchingBand(currentconfigindex, i, k)].re);
              if(divisor > 0.0)
              {
                scale = config->getDTsys(currentconfigindex, i)/divisor;
              }
            }
            else
            {
              //We want normalised correlation coefficients, so scale by number of contributing
              //samples rather than datastream tsys and decorrelation correction
              if(autocorrweights[i][j][k] > 0.0)
              {
                scale = 1.0/(autocorrweights[i][j][k]*meansubintsperintegration*((float)(config->getBlocksPerSend(currentconfigindex)*2*freqchannels*config->getFChannelsToAverage(freqindex))));
              }
            }
            if(scale > 0.0)
            {
              status = vectorMulC_f32_I(scale, &(floatresults[resultindex]), freqchannels*2);
              if(status != vecNoErr)
                csevere << startl << "Error trying to amplitude calibrate the datastream data!!!" << endl;
            }
            resultindex += freqchannels*2;
          }
        }
      }
    }
  }

  //calibrate the pulse cal
  for(int i=0;i<numdatastreams;i++)
  {
    if(config->getDPhaseCalIntervalMHz(currentconfigindex, i) > 0)
    {
      resultindex = config->getCoreResultPCalOffset(currentconfigindex, i)*2;
      for(int j=0;j<config->getDNumRecordedBands(currentconfigindex, i); j++)
      {
        localfreqindex = config->getDLocalRecordedFreqIndex(currentconfigindex, i, j);
        freqindex = config->getDRecordedFreqIndex(currentconfigindex, i, localfreqindex);
        freqchannels = config->getFNumChannels(freqindex)/config->getFChannelsToAverage(freqindex);
        if(autocorrweights[i][0][j] > 0.0)
        {
          scale = 1.0/(autocorrweights[i][0][j]*meansubintsperintegration*((float)(config->getBlocksPerSend(currentconfigindex)*2*freqchannels*config->getFChannelsToAverage(freqindex))));
          status = vectorMulC_f32_I(scale, &(floatresults[resultindex]), config->getDRecordedFreqNumPCalTones(currentconfigindex, i, localfreqindex)*2);
          if(status != vecNoErr)
            csevere << startl << "Error trying to amplitude calibrate the pulsecal data!!" << endl;
        }
        resultindex += config->getDRecordedFreqNumPCalTones(currentconfigindex, i, localfreqindex)*2;
      }
    }
  }

  //all calibrated, now just need to write out
  if(config->getOutputFormat() == Configuration::DIFX)
    writedifx(dumpmjd, dumpseconds);
  else
    writeascii(dumpmjd, dumpseconds);

  cdebug << startl << "Vis. " << visID << " has finished writing data" << endl;

  return;
}

void Visibility::writeascii(int dumpmjd, double dumpseconds)
{
  ofstream output;
  int binloop, freqchannels, freqindex;
  char datetimestring[26];

  int resultindex, atindex;
  int mjd = dumpmjd;
  int seconds = (int)dumpseconds;
  int microseconds = ((int)((dumpseconds - (double)seconds)*1000000.0 + 0.5));
  int hours = seconds/3600;
  int minutes = (seconds-hours*3600)/60;
  seconds = seconds - (hours*3600 + minutes*60);
  while(hours >= 24)
  {
     hours -= 24;
     mjd++;
  }
  sprintf(datetimestring, "%05u_%02u%02u%02u_%06u", mjd, hours, minutes, seconds, microseconds);
  cinfo << startl << "Mjd is " << mjd << ", hours is " << hours << ", minutes is " << minutes << ", seconds is " << seconds << endl;
  
  if(config->pulsarBinOn(currentconfigindex) && !config->scrunchOutputOn(currentconfigindex))
    binloop = config->getNumPulsarBins(currentconfigindex);
  else
    binloop = 1;

  for(int i=0;i<numbaselines;i++)
  {
    for(int j=0;j<config->getBNumFreqs(currentconfigindex,i);j++)
    {
      freqindex = config->getBFreqIndex(currentconfigindex, i, j);
      resultindex = config->getCoreResultBaselineOffset(currentconfigindex, freqindex, i);
      freqchannels = config->getFNumChannels(freqindex)/config->getFChannelsToAverage(freqindex);
      for(int s=0;s<model->getNumPhaseCentres(currentscan);s++)
      {
        for(int b=0;b<binloop;b++)
        {
          for(int k=0;k<config->getBNumPolProducts(currentconfigindex, i, j);k++)
          {
            //write out to a naive filename
            output.open(string(string("baseline_")+char('0' + i)+"_freq_"+char('0' + j)+"_product_"+char('0'+k)+"_"+datetimestring+"_source_"+char('0'+s)+"_bin_"+char('0'+b)+".output").c_str(), ios::out|ios::trunc);
            for(int l=0;l<freqchannels;l++) {
              atindex = resultindex+l;
              output << l << " " << sqrt(results[atindex].re*results[atindex].re + results[atindex].im*results[atindex].im) << " " << atan2(results[atindex].im, results[atindex].re) << endl;
            }
            output.close();
            resultindex += freqchannels;
          }
        }
      }
    }
  }

  if(config->writeAutoCorrs(currentconfigindex)) //if we need to, write out the autocorrs
  {
    for(int i=0;i<numdatastreams;i++)
    {
      resultindex = config->getCoreResultAutocorrOffset(currentconfigindex, i);
      for(int j=0;j<autocorrwidth;j++)
      {
        for(int k=0;k<config->getDNumTotalBands(currentconfigindex, i); k++)
        {
          freqindex = config->getDTotalFreqIndex(currentconfigindex, i, k);
          if(config->isFrequencyUsed(currentconfigindex, freqindex)) {
            freqchannels = config->getFNumChannels(freqindex)/config->getFChannelsToAverage(freqindex);
            //write out to naive filename
            output.open(string(string("datastream_")+char('0' + i)+"_crosspolar_"+char('0' + j)+"_product_"+char('0'+k)+"_"+datetimestring+"_bin_"+char('0'+0)+".output").c_str(), ios::out|ios::trunc);
            for(int l=0;l<freqchannels;l++) {
              atindex = resultindex + l;
              output << l << " " << sqrt(results[atindex].re*results[atindex].re + results[atindex].im*results[atindex].im) << " " << atan2(results[atindex].im, results[atindex].re) << endl;
            }
            output.close();
            resultindex += freqchannels;
          }
        }
      }
    }
  }
}

void Visibility::writedifx(int dumpmjd, double dumpseconds)
{
  ofstream output;
  ofstream pcaloutput;
  char filename[256];
  char pcalfilename[256];
  char pcalstr[256];
  int binloop, freqindex, numpolproducts, resultindex, freqchannels, maxpol;
  int year, month, day, startyearmjd, dummyseconds;
  int ant1index, ant2index, sourceindex, baselinenumber, numfiles, filecount, tonefreq;
  double scanoffsetsecs, pcaldoy, cablecaldelay;
  bool modelok;
  double buvw[3]; //the u,v and w for this baseline at this time
  char polpair[3]; //the polarisation eg RR, LL

  if(currentscan >= model->getNumScans()) {
    cwarn << startl << "Visibility will not write out time " << dumpmjd << "/" << dumpseconds << " since currentscan is " << currentscan << " and numscans is " << model->getNumScans() << endl;
    return;
  }

  if(config->pulsarBinOn(currentconfigindex) && !config->scrunchOutputOn(currentconfigindex))
    binloop = config->getNumPulsarBins(currentconfigindex);
  else
    binloop = 1;

  numfiles = binloop*model->getNumPhaseCentres(currentscan);
  for(int f=0;f<numfiles;f++)
  {
    todiskmemptrs[f] = f*(todiskbufferlength/numfiles);
  }

  //work out the time of this integration
  dumpmjd = expermjd + (experseconds + model->getScanStartSec(currentscan, expermjd, experseconds) + currentstartseconds)/86400;
  dumpseconds = double((experseconds + model->getScanStartSec(currentscan, expermjd, experseconds) + currentstartseconds)%86400) + ((double)currentstartns)/1000000000.0 + config->getIntTime(currentconfigindex)/2.0;

  //work through each baseline visibility point
  for(int i=0;i<numbaselines;i++)
  {
    baselinenumber = config->getBNumber(currentconfigindex, i);
    for(int j=0;j<config->getBNumFreqs(currentconfigindex,i);j++)
    {
      freqindex = config->getBFreqIndex(currentconfigindex, i, j);
      resultindex = config->getCoreResultBaselineOffset(currentconfigindex, freqindex, i);
      freqchannels = config->getFNumChannels(freqindex)/config->getFChannelsToAverage(freqindex);
      numpolproducts = config->getBNumPolProducts(currentconfigindex, i, j);
      filecount = 0;
      for(int s=0;s<model->getNumPhaseCentres(currentscan);s++)
      {
        //get the source-specific data
        sourceindex = model->getPhaseCentreSourceIndex(currentscan, s);
        scanoffsetsecs = currentstartseconds + ((double)currentstartns)/1e9 + config->getIntTime(currentconfigindex)/2.0;
        ant1index = config->getDModelFileIndex(currentconfigindex, config->getBOrderedDataStream1Index(currentconfigindex, i));
        ant2index = config->getDModelFileIndex(currentconfigindex, config->getBOrderedDataStream2Index(currentconfigindex, i));
        modelok = model->interpolateUVW(currentscan, scanoffsetsecs, ant1index, ant2index, s+1, buvw);
        if(!modelok)
          csevere << startl << "Could not calculate the UVW for this integration!!!" << endl;
        for(int b=0;b<binloop;b++)
        {
          for(int k=0;k<numpolproducts;k++) 
          {
            config->getBPolPair(currentconfigindex, i, j, k, polpair);

            //open the file for appending in ascii and write the ascii header
            if(baselineweights[i][j][b][k] > 0.0)
            {
              //cout << "About to write out baseline[" << i << "][" << s << "][" << k << "] from resultindex " << resultindex << ", whose 6th vis is " << results[resultindex+6].re << " + " << results[resultindex+6].im << " i" << endl;
              writeDiFXHeader(&output, baselinenumber, dumpmjd, dumpseconds, currentconfigindex, sourceindex, freqindex, polpair, b, 0, baselineweights[i][j][b][k], buvw, filecount);

              //close, reopen in binary and write the binary data, then close again
              //For both USB and LSB data, the Nyquist channel has already been excised by Core. In
              //the case of correlating USB with LSB data, the first datastream defines which is the 
              //Nyquist channels.  In any case, the numchannels that are written out represent the
              //the valid part of the, and run from lowest frequency to highest frequency.  For USB
              //data, the first channel is the DC - for LSB data, the last channel is the DC
              //output.write((char*)(&(results[resultindex])), freqchannels*sizeof(cf32));
              memcpy(&(todiskbuffer[todiskmemptrs[filecount]]), &(results[resultindex]), freqchannels*sizeof(cf32));
              todiskmemptrs[filecount] += freqchannels*sizeof(cf32);
            }

            resultindex += freqchannels;
          }
          filecount++;
        }
      }
    }
  }

  //now write all the different files out to disk, one hit per file
  filecount = 0;
  for(int s=0;s<model->getNumPhaseCentres(currentscan);s++)
  {
    for(int b=0;b<binloop;b++)
    {
      sprintf(filename, "%s/DIFX_%05d_%06d.s%04d.b%04d", config->getOutputFilename().c_str(), expermjd, experseconds, s, b);
      output.open(filename, ios::app);
      output.write(&(todiskbuffer[filecount*(todiskbufferlength/numfiles)]), todiskmemptrs[filecount]-filecount*(todiskbufferlength/numfiles));
      output.close();
      filecount++;
    }
  }

  if(model->getNumPhaseCentres(currentscan) == 1)
    sourceindex = model->getPhaseCentreSourceIndex(currentscan, 0);
  else
    sourceindex = model->getPointingCentreSourceIndex(currentscan);
  todiskmemptrs[0] = 0;

  //now each autocorrelation visibility point if necessary
  if(config->writeAutoCorrs(currentconfigindex))
  {
    buvw[0] = 0.0;
    buvw[1] = 0.0;
    buvw[2] = 0.0;
    for(int i=0;i<numdatastreams;i++)
    {
      baselinenumber = 257*(config->getDTelescopeIndex(currentconfigindex, i)+1);
      resultindex = config->getCoreResultAutocorrOffset(currentconfigindex, i);
      for(int j=0;j<autocorrwidth;j++)
      {
        for(int k=0;k<config->getDNumTotalBands(currentconfigindex, i); k++)
        {
          freqindex = config->getDTotalFreqIndex(currentconfigindex, i, k);
          if(config->isFrequencyUsed(currentconfigindex, freqindex)) {
            freqchannels = config->getFNumChannels(freqindex)/config->getFChannelsToAverage(freqindex);
            if(autocorrweights[i][j][k] > 0.0)
            {
              //open, write the header and close
              if(k<config->getDNumRecordedBands(currentconfigindex, i))
                polpair[0] = config->getDRecordedBandPol(currentconfigindex, i, k);
              else
                polpair[0] = config->getDZoomBandPol(currentconfigindex, i, k-config->getDNumRecordedBands(currentconfigindex, i));
              if(j==0)
                polpair[1] = polpair[0];
              else
                polpair[1] = config->getOppositePol(polpair[0]);
              writeDiFXHeader(&output, baselinenumber, dumpmjd, dumpseconds, currentconfigindex, sourceindex, freqindex, polpair, 0, 0, autocorrweights[i][j][k], buvw, 0);

              //open, write the binary data and close
              //see baseline writing section for description of treatment of USB/LSB data and the Nyquist channel
              //output.write((char*)(&(results[resultindex])), freqchannels*sizeof(cf32));
              memcpy(&(todiskbuffer[todiskmemptrs[0]]), &(results[resultindex]), freqchannels*sizeof(cf32));
              todiskmemptrs[0] += freqchannels*sizeof(cf32);
            }
            resultindex += freqchannels;
          }
        }
      }
    }
  }

  //write out the autocorrelations, all in one hit
  sprintf(filename, "%s/DIFX_%05d_%06d.s%04d.b%04d", config->getOutputFilename().c_str(), expermjd, experseconds, 0, 0);
  output.open(filename, ios::app);
  output.write(todiskbuffer, todiskmemptrs[0]);
  output.close();

  //now each pcal (if necessary)
  cablecaldelay = 0.0;
  config->mjd2ymd(dumpmjd, year, month, day);
  config->getMJD(startyearmjd, dummyseconds, year, 1, 1, 0, 0, 0);
  pcaldoy = dumpmjd - startyearmjd + 1.0 + dumpseconds/86400.0;
  maxpol = 1;
  if(config->getMaxProducts(currentconfigindex) > 1)
    maxpol = 2;
  polpair[0] = config->getDRecordedBandPol(0, 0, 0);
  polpair[1] = config->getOppositePol(polpair[0]);
  for(int i=0;i<numdatastreams;i++)
  {
    if(config->getDPhaseCalIntervalMHz(currentconfigindex, i) > 0)
    {
      sprintf(pcalfilename, "%s/PCAL_%s", config->getOutputFilename().c_str(), config->getTelescopeName(i).c_str());
      pcaloutput.open(pcalfilename, ios::app);
      //write the header string - note state counts are not written, and cablecal is dummy
      sprintf(pcalstr, "%s %10.7f %9.7f %.2f %d %d %d %d %d",
              config->getTelescopeName(i).c_str(), pcaldoy,
              config->getIntTime(currentconfigindex)/86400.0, cablecaldelay,
              maxpol, config->getDNumRecordedFreqs(currentconfigindex, i),
              config->getDMaxRecordedPCalTones(currentconfigindex, i), 
              0/*no state counts*/, config->getDNumRecordedBands(currentconfigindex, i));
      pcaloutput.write(pcalstr, strlen(pcalstr));
      for(int p=0;p<maxpol;p++)
      {
        for(int j=0;j<config->getDNumRecordedBands(currentconfigindex, i);j++)
        //we have to loop over bands as they are used to index the pcal results
        {
          if(config->getDRecordedBandPol(currentconfigindex, i, j) != polpair[p]) {
	    //skip band if it's not the right polarisation without writing out dummy pcal
	    continue;
          }
          //Go to the matching band in the pcal results
          resultindex = config->getCoreResultPCalOffset(currentconfigindex, i) +
                        j*config->getDRecordedFreqNumPCalTones(currentconfigindex, i, config->getDLocalRecordedFreqIndex(currentconfigindex, i, j));
	  for(int t=0;t<config->getDMaxRecordedPCalTones(currentconfigindex, i);t++)
          {
            //get the default response ready in case we don't find anything
	    sprintf(pcalstr, " %3d %d %.5e %.5e", -1, 0, 0.0, 0.0);
	      
            //write out empty tone and continue for any tones outside the bandwidth of the channel.
	    if(t >= config->getDRecordedFreqNumPCalTones(currentconfigindex, i, config->getDLocalRecordedFreqIndex(currentconfigindex, i, j))) {
		pcaloutput.write(pcalstr, strlen(pcalstr));
		continue; //move on
	    }
	    tonefreq = config->getDRecordedFreqPCalToneFreq(currentconfigindex, i, config->getDLocalRecordedFreqIndex(currentconfigindex, i, j), t);
            sprintf(pcalstr, " %3d %d %12.5e %12.5e", p, tonefreq, 
	    results[resultindex+t].re,
	    results[resultindex+t].im);
            pcaloutput.write(pcalstr, strlen(pcalstr));
          }
        }
      }
      pcaloutput.write("\n", 1);
    }
    pcaloutput.close();
  }
}

void Visibility::multicastweights()
{
  float *weight;
  double mjd;
  int dumpmjd, intsec;
  double dumpseconds;

  if(currentscan >= model->getNumScans() || (model->getScanStartSec(currentscan, expermjd, experseconds) + currentstartseconds) >= executeseconds)
  {
    cdebug << startl << "Vis. " << visID << " is not multicasting any weights, since the time is past the end of the correlation" << endl;
    return; //NOTE EXIT HERE!!!
  }

  weight = new float[numdatastreams];
  
  //work out the time of this integration
  intsec = experseconds + model->getScanStartSec(currentscan, expermjd, experseconds) + currentstartseconds;
  dumpmjd = expermjd + intsec/86400;
  dumpseconds = double(intsec%86400) + ((double)currentstartns)/1000000000.0 + config->getIntTime(currentconfigindex)/2.0;
  if(dumpseconds > 86400.0) {
    dumpmjd++;
    dumpseconds -= 86400.0;
  }

  for(int i=0;i<numdatastreams;i++)
  {
    for(int j=0;j<config->getDNumRecordedBands(currentconfigindex, i);j++)
      weight[i] += autocorrweights[i][0][j]/config->getDNumRecordedBands(currentconfigindex, i);
  }

  mjd = dumpmjd + dumpseconds/86400.0;

  difxMessageSendDifxStatus(DIFX_STATE_RUNNING, "", mjd, numdatastreams, weight);

  delete [] weight;
} 


void Visibility::writeDiFXHeader(ofstream * output, int baselinenum, int dumpmjd, double dumpseconds, int configindex, int sourceindex, int freqindex, const char polproduct[3], int pulsarbin, int flag, float weight, double buvw[3], int filecount)
{
  /* *output << setprecision(15);
  *output << "BASELINE NUM:       " << baselinenum << endl;
  *output << "MJD:                " << dumpmjd << endl;
  *output << "SECONDS:            " << dumpseconds << endl;
  *output << "CONFIG INDEX:       " << configindex << endl;
  *output << "SOURCE INDEX:       " << sourceindex << endl;
  *output << "FREQ INDEX:         " << freqindex << endl;
  *output << "POLARISATION PAIR:  " << polproduct[0] << polproduct[1] << endl;
  *output << "PULSAR BIN:         " << pulsarbin << endl;
  *output << "FLAGGED:            " << flag << endl;
  *output << "DATA WEIGHT:        " << weight << endl;
  *output << "U (METRES):         " << buvw[0] << endl;
  *output << "V (METRES):         " << buvw[1] << endl;
  *output << "W (METRES):         " << buvw[2] << endl;
  sprintf(&(todiskbuffer[todiskmemptrs[filecount]]), "BASELINE NUM:       %d\n", baselinenum);
  todiskmemptrs[filecount] += strlen(&(todiskbuffer[todiskmemptrs[filecount]]));
  sprintf(&(todiskbuffer[todiskmemptrs[filecount]]), "MJD:                %d\n", dumpmjd);
  todiskmemptrs[filecount] += strlen(&(todiskbuffer[todiskmemptrs[filecount]]));
  sprintf(&(todiskbuffer[todiskmemptrs[filecount]]), "SECONDS:            %15.9f\n", dumpseconds);
  todiskmemptrs[filecount] += strlen(&(todiskbuffer[todiskmemptrs[filecount]]));
  sprintf(&(todiskbuffer[todiskmemptrs[filecount]]), "CONFIG INDEX:       %d\n", configindex);
  todiskmemptrs[filecount] += strlen(&(todiskbuffer[todiskmemptrs[filecount]]));
  sprintf(&(todiskbuffer[todiskmemptrs[filecount]]), "SOURCE INDEX:       %d\n", sourceindex);
  todiskmemptrs[filecount] += strlen(&(todiskbuffer[todiskmemptrs[filecount]]));
  sprintf(&(todiskbuffer[todiskmemptrs[filecount]]), "FREQ INDEX:         %d\n", freqindex);
  todiskmemptrs[filecount] += strlen(&(todiskbuffer[todiskmemptrs[filecount]]));
  sprintf(&(todiskbuffer[todiskmemptrs[filecount]]), "POLARISATION PAIR:  %c%c\n", polproduct[0], polproduct[1]);
  todiskmemptrs[filecount] += strlen(&(todiskbuffer[todiskmemptrs[filecount]]));
  sprintf(&(todiskbuffer[todiskmemptrs[filecount]]), "PULSAR BIN:         %d\n", pulsarbin);
  todiskmemptrs[filecount] += strlen(&(todiskbuffer[todiskmemptrs[filecount]]));
  sprintf(&(todiskbuffer[todiskmemptrs[filecount]]), "FLAGGED:            %d\n", flag);
  todiskmemptrs[filecount] += strlen(&(todiskbuffer[todiskmemptrs[filecount]]));
  sprintf(&(todiskbuffer[todiskmemptrs[filecount]]), "DATA WEIGHT:        %.9f\n", weight);
  todiskmemptrs[filecount] += strlen(&(todiskbuffer[todiskmemptrs[filecount]]));
  if(baselinenum % 257 > 0)
  {
    sprintf(&(todiskbuffer[todiskmemptrs[filecount]]), "U (METRES):         %.9f\n", buvw[0]);
    todiskmemptrs[filecount] += strlen(&(todiskbuffer[todiskmemptrs[filecount]]));
    sprintf(&(todiskbuffer[todiskmemptrs[filecount]]), "V (METRES):         %.9f\n", buvw[1]);
    todiskmemptrs[filecount] += strlen(&(todiskbuffer[todiskmemptrs[filecount]]));
    sprintf(&(todiskbuffer[todiskmemptrs[filecount]]), "W (METRES):         %.9f\n", buvw[2]);
    todiskmemptrs[filecount] += strlen(&(todiskbuffer[todiskmemptrs[filecount]]));
  }
  else
  {
    sprintf(&(todiskbuffer[todiskmemptrs[filecount]]), "U (METRES):         0.0\n");
    todiskmemptrs[filecount] += strlen(&(todiskbuffer[todiskmemptrs[filecount]]));
    sprintf(&(todiskbuffer[todiskmemptrs[filecount]]), "V (METRES):         0.0\n");
    todiskmemptrs[filecount] += strlen(&(todiskbuffer[todiskmemptrs[filecount]]));
    sprintf(&(todiskbuffer[todiskmemptrs[filecount]]), "W (METRES):         0.0\n");
    todiskmemptrs[filecount] += strlen(&(todiskbuffer[todiskmemptrs[filecount]]));
  }*/
  *((unsigned int*)(&(todiskbuffer[todiskmemptrs[filecount]]))) = SYNC_WORD;
  todiskmemptrs[filecount] += 4;
  *((int*)(&(todiskbuffer[todiskmemptrs[filecount]]))) = BINARY_HEADER_VERSION;
  todiskmemptrs[filecount] += 4;
  *((int*)(&(todiskbuffer[todiskmemptrs[filecount]]))) = baselinenum;
  todiskmemptrs[filecount] += 4;
  *((int*)(&(todiskbuffer[todiskmemptrs[filecount]]))) = dumpmjd;
  todiskmemptrs[filecount] += 4;
  *((double*)(&(todiskbuffer[todiskmemptrs[filecount]]))) = dumpseconds;
  todiskmemptrs[filecount] += 8;
  *((int*)(&(todiskbuffer[todiskmemptrs[filecount]]))) = configindex;
  todiskmemptrs[filecount] += 4;
  *((int*)(&(todiskbuffer[todiskmemptrs[filecount]]))) = sourceindex;
  todiskmemptrs[filecount] += 4;
  *((int*)(&(todiskbuffer[todiskmemptrs[filecount]]))) = freqindex;
  todiskmemptrs[filecount] += 4;
  todiskbuffer[todiskmemptrs[filecount]++] = polproduct[0];
  todiskbuffer[todiskmemptrs[filecount]++] = polproduct[1];
  *((int*)(&(todiskbuffer[todiskmemptrs[filecount]]))) = pulsarbin;
  todiskmemptrs[filecount] += 4;
  *((double*)(&(todiskbuffer[todiskmemptrs[filecount]]))) = weight;
  todiskmemptrs[filecount] += 8;
  memcpy(&(todiskbuffer[todiskmemptrs[filecount]]), buvw, 3*8);
  todiskmemptrs[filecount] += 3*8;
}

void Visibility::changeConfig(int configindex)
{
  char polpair[3];
  polpair[2] = 0;
  int pulsarwidth;

  if(first) 
  {
    //can just allocate without freeing all the old stuff
    first = false;
    autocorrcalibs = new cf32*[numdatastreams];
    autocorrweights = new f32**[numdatastreams];
    baselineweights = new f32***[numbaselines];
    binweightsums = new f32**[config->getFreqTableLength()];
    binscales = new cf32**[config->getFreqTableLength()];
    pulsarbins = new s32*[config->getFreqTableLength()];
  }
  else
  {
    pulsarwidth = 1;
    if(pulsarbinon && !config->scrunchOutputOn(currentconfigindex))
      pulsarwidth = config->getNumPulsarBins(currentconfigindex);
    cverbose << startl << "Starting to delete some old arrays" << endl;
    //need to delete the old arrays before allocating the new ones
    for(int i=0;i<numdatastreams;i++) {
      delete [] autocorrcalibs[i];
      for(int j=0;j<autocorrwidth;j++)
        delete [] autocorrweights[i][j];
      delete [] autocorrweights[i];
    }
    for(int i=0;i<numbaselines;i++)
    {
      for(int j=0;j<config->getBNumFreqs(currentconfigindex, i);j++) {
        for(int k=0;k<pulsarwidth;k++)
          delete [] baselineweights[i][j][k];
        delete [] baselineweights[i][j];
      }
      delete [] baselineweights[i];
    }
    if(pulsarbinon) {
      cverbose << startl << "Starting to delete some pulsar arrays" << endl;
      for(int i=0;i<config->getFreqTableLength();i++) {
        for(int j=0;j<config->getFNumChannels(i)+1;j++)
          vectorFree(binweightsums[i][j]);
        for(int j=0;j<((config->scrunchOutputOn(currentconfigindex))?1:config->getNumPulsarBins(currentconfigindex));j++)
          vectorFree(binscales[i][j]);
        vectorFree(pulsarbins[i]);
        delete [] binweightsums[i];
        delete [] binscales[i];
      }
      vectorFree(binweightdivisor);
      cverbose << startl << "Finished deleting some pulsar arrays" << endl;
    }
  }

  //get the new parameters for this configuration from the config object
  currentconfigindex = configindex;
  autocorrwidth = 1;
  if (maxproducts > 2 && config->writeAutoCorrs(configindex))
    autocorrwidth = 2;
  pulsarbinon = config->pulsarBinOn(configindex);
  pulsarwidth = 1;
  if(pulsarbinon && !config->scrunchOutputOn(currentconfigindex))
    pulsarwidth = config->getNumPulsarBins(currentconfigindex);
  offsetnsperintegration = (int)(((long long)(1000000000.0*config->getIntTime(configindex)))%((long long)config->getSubintNS(configindex)));
  meansubintsperintegration =config->getIntTime(configindex)/(((double)config->getSubintNS(configindex))/1000000000.0);
  fftsperintegration = meansubintsperintegration*config->getBlocksPerSend(configindex);
  cverbose << startl << "For Visibility " << visID << ", offsetnsperintegration is " << offsetnsperintegration << ", subintns is " << config->getSubintNS(configindex) << ", and configindex is now " << configindex << endl;
  resultlength = config->getCoreResultLength(configindex);
  for(int i=0;i<numdatastreams;i++) {
    autocorrcalibs[i] = new cf32[config->getDNumTotalBands(configindex, i)];
    autocorrweights[i] = new f32*[autocorrwidth];
    for(int j=0;j<autocorrwidth;j++)
      autocorrweights[i][j] = new f32[config->getDNumTotalBands(configindex, i)];
  }

  //Set up the baseline weights array
  for(int i=0;i<numbaselines;i++)
  {
    baselineweights[i] = new f32**[config->getBNumFreqs(configindex, i)];
    for(int j=0;j<config->getBNumFreqs(configindex, i);j++) {
      baselineweights[i][j] = new f32*[pulsarwidth];
      for(int k=0;k<pulsarwidth;k++)
        baselineweights[i][j][k] = new f32[config->getBNumPolProducts(configindex, i, j)];
    }
  }

  //create the pulsar bin weight accumulation arrays
  if(pulsarbinon) {
    cverbose << startl << "Starting the pulsar bin initialisation" << endl;
    polyco = Polyco::getCurrentPolyco(configindex, expermjd + (experseconds + model->getScanStartSec(currentscan, expermjd, experseconds) + currentstartseconds)/86400, double((experseconds + model->getScanStartSec(currentscan, expermjd, experseconds) + currentstartseconds)%86400)/86400.0, config->getPolycos(configindex), config->getNumPolycos(configindex), false);
    if (polyco == NULL) {
      cfatal << startl << "Could not locate a Polyco to cover the timerange MJD " << expermjd + (experseconds + model->getScanStartSec(currentscan, expermjd, experseconds) + currentstartseconds)/86400 << ", seconds " << (experseconds + model->getScanStartSec(currentscan, expermjd, experseconds) + currentstartseconds)%86400 << " - aborting" << endl;
      Polyco::getCurrentPolyco(configindex, expermjd + (experseconds + model->getScanStartSec(currentscan, expermjd, experseconds) + currentstartseconds)/86400, double((experseconds + model->getScanStartSec(currentscan, expermjd, experseconds) + currentstartseconds)%86400)/86400.0, config->getPolycos(configindex), config->getNumPolycos(configindex), true);
      configuredok = false;
    }
    //polyco->setTime(expermjd + (experseconds + currentstartseconds)/86400, double((experseconds + currentstartseconds)%86400)/86400.0);
    if(config->scrunchOutputOn(configindex)) {
      binweightdivisor = vectorAlloc_f32(1);
      binweightdivisor[0] = 0.0;
      for (int i=0;i<config->getNumPulsarBins(configindex);i++)
      {
        binweightdivisor[0] += polyco->getBinWeightTimesWidth(i)*fftsperintegration;
      }
      binweightdivisor[0] /= double(config->getNumPulsarBins(configindex));
    }
    else {
      binweightdivisor = vectorAlloc_f32(config->getNumPulsarBins(configindex));
      for (int i=0;i<config->getNumPulsarBins(configindex);i++)
      {
        binweightdivisor[i] = polyco->getBinWeightTimesWidth(i)*fftsperintegration;
      }
    }
    for(int i=0;i<config->getFreqTableLength();i++) {
      binweightsums[i] = new f32*[config->getFNumChannels(i)+1];
      binscales[i] = new cf32*[config->scrunchOutputOn(configindex)?1:config->getNumPulsarBins(configindex)];
      pulsarbins[i] = vectorAlloc_s32(config->getFNumChannels(i)+1);
      for(int j=0;j<config->getFNumChannels(i)+1;j++) {
        if(config->scrunchOutputOn(configindex))
          binweightsums[i][j] = vectorAlloc_f32(1);
        else
          binweightsums[i][j] = vectorAlloc_f32(config->getNumPulsarBins(configindex));
      }
      for(int j=0;j<(config->scrunchOutputOn(configindex)?1:config->getNumPulsarBins(configindex));j++)
        binscales[i][j] = vectorAlloc_cf32(config->getFNumChannels(i) + 1);
    }
    cverbose << startl << "Finished the pulsar bin initialisation" << endl;
  }
}
// vim: shiftwidth=2:softtabstop=2:expandtab
