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
#include <iomanip>
#include "mode.h"
#include "math.h"
#include "architecture.h"
#include "alert.h"
#include "pcal.h"
#include "filterchain.h"

//using namespace std;
const float Mode::TINY = 0.000000001;

Mode::Mode(Configuration * conf, int confindex, int dsindex, int recordedbandchan, int chanstoavg, int bpersend, int gsamples, int nrecordedfreqs, double recordedbw, double * recordedfreqclkoffs, double * recordedfreqlooffs, int nrecordedbands, int nzoombands, int nbits, Configuration::datasampling sampling, int unpacksamp, bool fbank, int fringerotorder, int arraystridelen, bool cacorrs, double bclock)
  : config(conf), configindex(confindex), datastreamindex(dsindex), recordedbandchannels(recordedbandchan), channelstoaverage(chanstoavg), blockspersend(bpersend), guardsamples(gsamples), fftchannels(recordedbandchan*2), numrecordedfreqs(nrecordedfreqs), numrecordedbands(nrecordedbands), numzoombands(nzoombands), numbits(nbits), unpacksamples(unpacksamp), fringerotationorder(fringerotorder), arraystridelength(arraystridelen), recordedbandwidth(recordedbw), blockclock(bclock), filterbank(fbank), calccrosspolautocorrs(cacorrs), recordedfreqclockoffsets(recordedfreqclkoffs), recordedfreqlooffsets(recordedfreqlooffs)
{
  int status, localfreqindex;
  int decimationfactor = config->getDDecimationFactor(configindex, datastreamindex);
  int pcalOffset;
  estimatedbytes = 0;

  if (sampling==Configuration::COMPLEX) 
    usecomplex=1;
  else
    usecomplex=0;

  model = config->getModel();
  initok = true;
  intclockseconds = int(floor(config->getDClockCoeff(configindex, dsindex, 0)/1000000.0 + 0.5));
  if (usecomplex) fftchannels /=2;

  numfracstrides = numfrstrides = fftchannels/arraystridelength;
  if (usecomplex) numfracstrides *= 2;
  sampletime = 1.0/(2.0*recordedbandwidth); //microseconds
  if (usecomplex) sampletime *= 2.0;
  fftdurationmicrosec = fftchannels*sampletime;  // This is never used??
  flaglength = blockspersend/FLAGS_PER_INT;
  if(blockspersend%FLAGS_PER_INT > 0)
    flaglength++;
  validflags = vectorAlloc_s32(flaglength);
  fractionalLoFreq = false;
  for(int i=0;i<numrecordedfreqs;i++)
  {
    if(config->getDRecordedFreq(configindex, datastreamindex, i) - int(config->getDRecordedFreq(configindex, datastreamindex, i)) > TINY)
      fractionalLoFreq = true;
  }

  //now do the rest of the initialising
  samplesperblock = int(recordedbandwidth*2/blockclock);
  if(samplesperblock == 0)
  {
    cfatal << startl << "Error!!! Samplesperblock is less than 1, current implementation cannot handle this situation.  Aborting!" << endl;
    initok = false;
  }
  else
  {
    int numsamplebits = numbits;
    if (usecomplex) numsamplebits *=2;
    bytesperblocknumerator = (numrecordedbands*samplesperblock*numsamplebits*decimationfactor)/8;
    if(bytesperblocknumerator == 0)
    {
      bytesperblocknumerator = 1;
      bytesperblockdenominator = 8/(numrecordedbands*samplesperblock*numsamplebits*decimationfactor);
      unpacksamples += bytesperblockdenominator*sizeof(u16)*samplesperblock;
    }
    else
    {
      bytesperblockdenominator = 1;
    }
    samplesperlookup = (numrecordedbands*sizeof(u16)*samplesperblock*bytesperblockdenominator)/bytesperblocknumerator;
    numlookups = (unpacksamples*bytesperblocknumerator)/(bytesperblockdenominator*sizeof(u16)*samplesperblock);
    if(samplesperblock > 1)
      numlookups++;

    unpackedarrays = new f32*[numrecordedbands];
    if (usecomplex) unpackedcomplexarrays = new cf32*[numrecordedbands];
    for(int i=0;i<numrecordedbands;i++) {
      unpackedarrays[i] = vectorAlloc_f32(unpacksamples);
      estimatedbytes += sizeof(f32)*unpacksamples;
      if (usecomplex) unpackedcomplexarrays[i] = (cf32*) unpackedarrays[i];
    }

    interpolator = new f64[3];

    fftoutputs = new cf32**[numrecordedbands + numzoombands];
    conjfftoutputs = new cf32**[numrecordedbands + numzoombands];
    estimatedbytes += 4*(numrecordedbands + numzoombands);
    for(int j=0;j<numrecordedbands+numzoombands;j++)
    {
      fftoutputs[j] = new cf32*[config->getNumBufferedFFTs(confindex)];
      conjfftoutputs[j] = new cf32*[config->getNumBufferedFFTs(confindex)];
      for(int k=0;k<config->getNumBufferedFFTs(confindex);k++)
      {
        if(j<numrecordedbands)
        {
	  if(fringerotationorder == 0) // post-F
	  {
	    fftoutputs[j][k] = vectorAlloc_cf32(recordedbandchannels+1);
            conjfftoutputs[j][k] = vectorAlloc_cf32(recordedbandchannels+1);
	  }
	  else
	  {
            fftoutputs[j][k] = vectorAlloc_cf32(recordedbandchannels);
            conjfftoutputs[j][k] = vectorAlloc_cf32(recordedbandchannels);
	  }
          estimatedbytes += 2*4*recordedbandchannels;
        }
        else
        {
          localfreqindex = config->getDLocalZoomFreqIndex(confindex, dsindex, j-numrecordedbands);
	  fftoutputs[j][k] = &(fftoutputs[config->getDZoomFreqParentFreqIndex(confindex, dsindex, localfreqindex)][k][config->getDZoomFreqChannelOffset(confindex, dsindex, localfreqindex)]);
	  conjfftoutputs[j][k] = &(conjfftoutputs[config->getDZoomFreqParentFreqIndex(confindex, dsindex,localfreqindex)][k][config->getDZoomFreqChannelOffset(confindex, dsindex, localfreqindex)]);
	}
      }
    }
    dataweight = 0.0;

    lookup = vectorAlloc_s16((MAX_U16+1)*samplesperlookup);
    linearunpacked = vectorAlloc_s16(numlookups*samplesperlookup);
    estimatedbytes += 2*(numlookups*samplesperlookup + (MAX_U16+1)*samplesperlookup);

    //initialise the fft info
    order = 0;
    while((fftchannels) >> order != 1)
      order++;
    flag = vecFFT_NoReNorm;
    hint = vecAlgHintFast;

    switch(fringerotationorder) {
      case 2: // Quadratic
        piecewiserotator = vectorAlloc_cf32(arraystridelength);
        quadpiecerotator = vectorAlloc_cf32(arraystridelength);
        estimatedbytes += 2*8*arraystridelength;

        subquadxval  = vectorAlloc_f64(arraystridelength);
        subquadphase = vectorAlloc_f64(arraystridelength);
        subquadarg   = vectorAlloc_f32(arraystridelength);
        subquadsin   = vectorAlloc_f32(arraystridelength);
        subquadcos   = vectorAlloc_f32(arraystridelength);
        estimatedbytes += (8+8+4+4+4)*arraystridelength;

        stepxoffsquared = vectorAlloc_f64(numfrstrides);
        tempstepxval    = vectorAlloc_f64(numfrstrides);
        estimatedbytes += 16*numfrstrides;
      case 1:
        subxoff  = vectorAlloc_f64(arraystridelength);
        subxval  = vectorAlloc_f64(arraystridelength);
        subphase = vectorAlloc_f64(arraystridelength);
        subarg   = vectorAlloc_f32(arraystridelength);
        subsin   = vectorAlloc_f32(arraystridelength);
        subcos   = vectorAlloc_f32(arraystridelength);
        estimatedbytes += (3*8+3*4)*arraystridelength;

        stepxoff  = vectorAlloc_f64(numfrstrides);
        stepxval  = vectorAlloc_f64(numfrstrides);
        stepphase = vectorAlloc_f64(numfrstrides);
        steparg   = vectorAlloc_f32(numfrstrides);
        stepsin   = vectorAlloc_f32(numfrstrides);
        stepcos   = vectorAlloc_f32(numfrstrides);
        stepcplx  = vectorAlloc_cf32(numfrstrides);
        estimatedbytes += (3*8+3*4+8)*numfrstrides;

	complexunpacked = vectorAlloc_cf32(fftchannels);
	complexrotator = vectorAlloc_cf32(fftchannels);
	fftd = vectorAlloc_cf32(fftchannels);
	estimatedbytes += 3*sizeof(cf32)*fftchannels;

        for(int i=0;i<arraystridelength;i++)
          subxoff[i] = (double(i)/double(fftchannels));
        for(int i=0;i<numfrstrides;i++)
          stepxoff[i] = double(i*arraystridelength)/double(fftchannels);
        if(fringerotationorder == 2) { // Quadratic
          for(int i=0;i<numfrstrides;i++)
            stepxoffsquared[i] = stepxoff[i]*stepxoff[i];
        }

        status = vectorInitFFTC_cf32(&pFFTSpecC, order, flag, hint);
        if(status != vecNoErr)
          csevere << startl << "Error in FFT initialisation!!!" << status << endl;
        status = vectorGetFFTBufSizeC_cf32(pFFTSpecC, &fftbuffersize);
        if(status != vecNoErr)
          csevere << startl << "Error in FFT buffer size calculation!!!" << status << endl;
        break;
      case 0: //zeroth order interpolation, can do "post-F"
        status = vectorInitFFTR_f32(&pFFTSpecR, order, flag, hint);
        if(status != vecNoErr)
          csevere << startl << "Error in FFT initialisation!!!" << status << endl;
        status = vectorGetFFTBufSizeR_f32(pFFTSpecR, &fftbuffersize);
        if(status != vecNoErr)
          csevere << startl << "Error in FFT buffer size calculation!!!" << status << endl;
        break;
    }

    fftbuffer = vectorAlloc_u8(fftbuffersize);

    subfracsamparg = vectorAlloc_f32(arraystridelength);
    subfracsampsin = vectorAlloc_f32(arraystridelength);
    subfracsampcos = vectorAlloc_f32(arraystridelength);
    subchannelfreqs = vectorAlloc_f32(arraystridelength);
    estimatedbytes += 5*4*arraystridelength;
    /*cout << "subfracsamparg is " << subfracsamparg << endl;
    cout << "subfracsampsin is " << subfracsampsin << endl;
    cout << "subfracsampcos is " << subfracsampcos << endl;
    cout << "subchannelfreqs is " << subchannelfreqs << endl;
    cout << "lsbsubchannelfreqs is " << lsbsubchannelfreqs << endl;*/
    for(int i=0;i<arraystridelength;i++) {
      subchannelfreqs[i] = (float)((TWO_PI*(i)*recordedbandwidth)/recordedbandchannels);
      //lsbsubchannelfreqs[i] = (float)((-TWO_PI*(arraystridelength-(i+1))*recordedbandwidth)/recordedbandchannels);
    }

    stepfracsamparg = vectorAlloc_f32(numfracstrides/2);
    stepfracsampsin = vectorAlloc_f32(numfracstrides/2);
    stepfracsampcos = vectorAlloc_f32(numfracstrides/2);
    stepfracsampcplx = vectorAlloc_cf32(numfracstrides/2);
    stepchannelfreqs = vectorAlloc_f32(numfracstrides/2);
    lsbstepchannelfreqs = vectorAlloc_f32(numfracstrides/2);
    estimatedbytes += (5*2+4)*numfracstrides;

    for(int i=0;i<numfracstrides/2;i++) {
      stepchannelfreqs[i] = (float)((TWO_PI*i*arraystridelength*recordedbandwidth)/recordedbandchannels);
      lsbstepchannelfreqs[i] = (float)((-TWO_PI*((numfracstrides/2-i)*arraystridelength-1)*recordedbandwidth)/recordedbandchannels);
    }

    fracsamprotator = vectorAlloc_cf32(recordedbandchannels);
    estimatedbytes += 8*recordedbandchannels;
    /*cout << "Numstrides is " << numstrides << ", recordedbandchannels is " << recordedbandchannels << ", arraystridelength is " << arraystridelength << endl;
    cout << "fracsamprotator is " << fracsamprotator << endl;
    cout << "stepchannelfreqs[5] is " << stepchannelfreqs[5] << endl;
    cout << "subchannelfreqs[5] is " << subchannelfreqs[5] << endl;
    cout << "stepchannelfreqs(last) is " << stepchannelfreqs[numstrides/2-1] << endl;
    fracmult = vectorAlloc_f32(recordedbandchannels + 1);
    fracmultcos = vectorAlloc_f32(recordedbandchannels + 1);
    fracmultsin = vectorAlloc_f32(recordedbandchannels + 1);
    complexfracmult = vectorAlloc_cf32(recordedbandchannels + 1);

    channelfreqs = vectorAlloc_f32(recordedbandchannels + 1);
    for(int i=0;i<recordedbandchannels + 1;i++)
      channelfreqs[i] = (float)((TWO_PI*i*recordedbandwidth)/recordedbandchannels);
    lsbchannelfreqs = vectorAlloc_f32(recordedbandchannels + 1);
    for(int i=0;i<recordedbandchannels + 1;i++)
      lsbchannelfreqs[i] = (float)((-TWO_PI*(recordedbandchannels-i)*recordedbandwidth)/recordedbandchannels);
    */

    //space for the autocorrelations
    if(calccrosspolautocorrs)
      autocorrwidth = 2;
    else
      autocorrwidth = 1;
    autocorrelations = new cf32**[autocorrwidth];
    weights = new f32*[autocorrwidth];
    for(int i=0;i<autocorrwidth;i++)
    {
      weights[i] = new f32[numrecordedbands];
      autocorrelations[i] = new cf32*[numrecordedbands+numzoombands];
      for(int j=0;j<numrecordedbands;j++) {
        autocorrelations[i][j] = vectorAlloc_cf32(recordedbandchannels);
        estimatedbytes += 8*recordedbandchannels;
      }
      for(int j=0;j<numzoombands;j++)
      {
        localfreqindex = config->getDLocalZoomFreqIndex(confindex, dsindex, j);
        autocorrelations[i][j+numrecordedbands] = &(autocorrelations[i][config->getDZoomFreqParentFreqIndex(confindex, dsindex, localfreqindex)][config->getDZoomFreqChannelOffset(confindex, dsindex, localfreqindex)/channelstoaverage]);
      }
    }

    //initialize rfi filtering in autocorrelations
    if (config->rfifilterEnabled())
    {
      const char* filterfile = config->getRFIFilterFilename().c_str();
      autocorrfilters = new FilterChain**[autocorrwidth];
      for (int xx=0;xx<autocorrwidth;xx++)
      { 
        autocorrfilters[xx] = new FilterChain* [numrecordedbands];
        for (int band=0;band<numrecordedbands;band++) 
        {
          // note1: zoomed bands copied from filter output subsets autocorrelations[0][j][a..b]), need no own filter
          // note2: for cross-pol, different output arrays attached later (see Mode::process())
          FilterChain* flt = new FilterChain();
          flt->buildFromFile(filterfile,recordedbandchannels);
          flt->setUserOutbuffer(autocorrelations[0][band]); 
          autocorrfilters[xx][band] = flt;
        }
      }
      rfiscratch = vectorAlloc_cf32(recordedbandchannels+1);
      DUT_CHECK( vectorZero_cf32(rfiscratch, recordedbandchannels+1), csevere, "RFI scratch zeroing" );
    }

    //kurtosis-specific stuff
    dumpkurtosis = false; //off by default
    s1 = 0;
    s2 = 0;
    sk = 0;
    kscratch = 0;
  }


  // Phase cal stuff
  if(config->getDPhaseCalIntervalMHz(configindex, datastreamindex))
  {
    pcalresults = new cf32*[numrecordedbands];
    extractor = new PCal*[numrecordedbands];
    pcalnbins = new int[numrecordedbands];
    for(int i=0;i<numrecordedbands;i++)
    {
      localfreqindex = conf->getDLocalRecordedFreqIndex(confindex, dsindex, i);

      pcalresults[i] = new cf32[conf->getDRecordedFreqNumPCalTones(configindex, dsindex, localfreqindex)];

      pcalOffset = static_cast<int>(config->getDRecordedFreqPCalOffsetsHz(configindex, dsindex, localfreqindex) + 0.5);

      extractor[i] = PCal::getNew(1e6*recordedbandwidth, 
                                  1e6*config->getDPhaseCalIntervalMHz(configindex, datastreamindex),
                                      pcalOffset, 0);
      pcalnbins[i] = extractor[i]->getNBins();
      cdebug << startl << "Band " << i << " phase cal extractor buffer length (N bins)" << pcalnbins[i] << endl;
    }
  }
}

Mode::~Mode()
{
  int status;

  vectorFree(validflags);
  for(int j=0;j<numrecordedbands+numzoombands;j++)
  {
    for(int k=0;k<config->getNumBufferedFFTs(configindex);k++)
    {
      vectorFree(fftoutputs[j][k]);
      vectorFree(conjfftoutputs[j][k]);
    }
    delete [] fftoutputs[j];
    delete [] conjfftoutputs[j];
  }
  delete [] fftoutputs;
  delete [] conjfftoutputs;
  delete [] interpolator;

  if (config->rfifilterEnabled())
  {
    for (int xx=0;xx<autocorrwidth;xx++) 
    {
      for (int i=0;i<numrecordedbands;i++) 
        delete autocorrfilters[xx][i];
      delete [] autocorrfilters[xx];
    }
    delete [] autocorrfilters;
    vectorFree(rfiscratch);
  }

  for(int i=0;i<numrecordedbands;i++)
    vectorFree(unpackedarrays[i]);
  delete [] unpackedarrays;

  switch(fringerotationorder) {
    case 2: // Quadratic
      vectorFree(piecewiserotator);
      vectorFree(quadpiecerotator);

      vectorFree(subquadxval);
      vectorFree(subquadphase);
      vectorFree(subquadarg);
      vectorFree(subquadsin);
      vectorFree(subquadcos);

      vectorFree(stepxoffsquared);
      vectorFree(tempstepxval);
    case 1:
      vectorFree(subxoff);
      vectorFree(subxval);
      vectorFree(subphase);
      vectorFree(subarg);
      vectorFree(subsin);
      vectorFree(subcos);

      vectorFree(stepxoff);
      vectorFree(stepxval);
      vectorFree(stepphase);
      vectorFree(steparg);
      vectorFree(stepsin);
      vectorFree(stepcos);
      vectorFree(stepcplx);

      vectorFree(complexunpacked);
      vectorFree(complexrotator);
      vectorFree(fftd);

      status = vectorFreeFFTC_cf32(pFFTSpecC);
      if(status != vecNoErr)
        csevere << startl << "Error in freeing FFT spec!!!" << status << endl;
      break;
    case 0: //zeroth order interpolation, "post-F"
      status = vectorFreeFFTR_f32(pFFTSpecR);
      if(status != vecNoErr)
        csevere << startl << "Error in freeing FFT spec!!!" << status << endl;
      break;
  }

  vectorFree(lookup);
  vectorFree(linearunpacked);
  vectorFree(fftbuffer);

  vectorFree(subfracsamparg);
  vectorFree(subfracsampsin);
  vectorFree(subfracsampcos);
  vectorFree(subchannelfreqs);
  //vectorFree(lsbsubchannelfreqs);

  vectorFree(stepfracsamparg);
  vectorFree(stepfracsampsin);
  vectorFree(stepfracsampcos);
  vectorFree(stepfracsampcplx);
  vectorFree(stepchannelfreqs);
  vectorFree(lsbstepchannelfreqs);

  vectorFree(fracsamprotator);
  //vectorFree(fracmult);
  //vectorFree(fracmultcos);
  //vectorFree(fracmultsin);
  //vectorFree(complexfracmult);
  //vectorFree(channelfreqs);

  for(int i=0;i<autocorrwidth;i++)
  {
    for(int j=0;j<numrecordedbands;j++)
      vectorFree(autocorrelations[i][j]);
    delete [] autocorrelations[i];
    delete [] weights[i];
  }
  delete [] weights;
  delete [] autocorrelations;

  if(config->getDPhaseCalIntervalMHz(configindex, datastreamindex))
  {
    for(int i=0;i<numrecordedbands;i++) {
       delete extractor[i];
       delete[] pcalresults[i];
    }
    delete[] pcalresults;
    delete[] extractor;
    delete[] pcalnbins;
  }

  if(sk != 0) //also need to delete kurtosis stuff
  {
    for(int i=0;i<numrecordedbands;i++)
    {
      vectorFree(s1[i]);
      vectorFree(s2[i]);
      vectorFree(sk[i]);
    }
    delete [] s1;
    delete [] s2;
    delete [] sk;
    vectorFree(kscratch);
  }
}

float Mode::unpack(int sampleoffset)
{
  int status, leftoversamples, stepin = 0;

  if(bytesperblockdenominator/bytesperblocknumerator == 0)
    leftoversamples = 0;
  else
    leftoversamples = sampleoffset%(bytesperblockdenominator/bytesperblocknumerator);
  unpackstartsamples = sampleoffset - leftoversamples;
  if(samplesperblock > 1)
    stepin = unpackstartsamples%(samplesperblock*bytesperblockdenominator);
  u16 * packed = (u16 *)(&(data[((unpackstartsamples/samplesperblock)*bytesperblocknumerator)/bytesperblockdenominator]));

  //copy from the lookup table to the linear unpacked array
  for(int i=0;i<numlookups;i++)
  {
    status = vectorCopy_s16(&lookup[packed[i]*samplesperlookup], &linearunpacked[i*samplesperlookup], samplesperlookup);
    if(status != vecNoErr) {
      csevere << startl << "Error in lookup for unpacking!!!" << status << endl;
      return 0;
    }
  }

  //split the linear unpacked array into the separate subbands
  status = vectorSplitScaled_s16f32(&(linearunpacked[stepin*numrecordedbands]), unpackedarrays, numrecordedbands, unpacksamples);
  if(status != vecNoErr) {
    csevere << startl << "Error in splitting linearunpacked!!!" << status << endl;
    return 0;
  }

  return 1.0;
}

float Mode::process(int index, int subloopindex)  //frac sample error, fringedelay and wholemicroseconds are in microseconds 
{
  double phaserotation, averagedelay, nearestsampletime, starttime, lofreq, walltimesecs, fftcentre, d0, d1, d2;
  f32 phaserotationfloat, fracsampleerror;
  int status, count, nearestsample, integerdelay, dcchannel, acoffset;
  cf32* fftptr;
  cf32* fracsampptr1;
  cf32* fracsampptr2;
  f32* currentstepchannelfreqs;
  int indices[numrecordedbands+4];
  //cout << "For Mode of datastream " << datastreamindex << ", index " << index << ", validflags is " << validflags[index/FLAGS_PER_INT] << ", after shift you get " << ((validflags[index/FLAGS_PER_INT] >> (index%FLAGS_PER_INT)) & 0x01) << endl;
  
  if((datalengthbytes <= 1) || (offsetseconds == INVALID_SUBINT) || (((validflags[index/FLAGS_PER_INT] >> (index%FLAGS_PER_INT)) & 0x01) == 0))
  {
    for(int i=0;i<numrecordedbands;i++)
    {
      status = vectorZero_cf32(fftoutputs[i][subloopindex], recordedbandchannels);
      if(status != vecNoErr)
        csevere << startl << "Error trying to zero fftoutputs when data is bad!" << endl;
      status = vectorZero_cf32(conjfftoutputs[i][subloopindex], recordedbandchannels);
      if(status != vecNoErr)
        csevere << startl << "Error trying to zero fftoutputs when data is bad!" << endl;
    }
    //cout << "Mode for DS " << datastreamindex << " is bailing out of index " << index << "/" << subloopindex << " which is scan " << currentscan << ", sec " << offsetseconds << ", ns " << offsetns << " because datalengthbytes is " << datalengthbytes << " and validflag was " << ((validflags[index/FLAGS_PER_INT] >> (index%FLAGS_PER_INT)) & 0x01) << endl;
    return 0.0; //don't process crap data
  }

  if (index < 0)
  {
    csevere << startl << "Mode::process index=" << index << " < 0, I knew it!!" << endl;
    return 0.0;
  }

  fftcentre = index+0.5;
  averagedelay = interpolator[0]*fftcentre*fftcentre + interpolator[1]*fftcentre + interpolator[2];
  fftstartmicrosec = index*fftchannels*sampletime; //CHRIS CHECK
  starttime = (offsetseconds-datasec)*1000000.0 + double(offsetns - datans)/1000.0 + fftstartmicrosec - averagedelay;
  //cout << "starttime for " << datastreamindex << " is " << starttime << endl;
  nearestsample = int(starttime/sampletime + 0.5);
  //cout << "nearestsample for " << datastreamindex << " is " << nearestsample << endl; 
 walltimesecs  =model->getScanStartSec(currentscan, config->getStartMJD(), config->getStartSeconds()) + offsetseconds + ((double)offsetns)/1000000000.0 + fftstartmicrosec/1000000.0;

  //if we need to, unpack some more data - first check to make sure the pos is valid at all
  //cout << "Datalengthbytes for " << datastreamindex << " is " << datalengthbytes << endl;
  //cout << "Fftchannels for " << datastreamindex << " is " << fftchannels << endl;
  //cout << "samplesperblock for " << datastreamindex << " is " << samplesperblock << endl;
  //cout << "nearestsample for " << datastreamindex << " is " << nearestsample << endl;
  //cout << "bytesperblocknumerator for " << datastreamindex << " is " << bytesperblocknumerator << endl;
  //cout << "bytesperblockdenominator for " << datastreamindex << " is " << bytesperblockdenominator << endl;
  if(nearestsample < -1 || (((nearestsample + fftchannels)/samplesperblock)*bytesperblocknumerator)/bytesperblockdenominator > datalengthbytes)
  {
    cerror << startl << "MODE error for datastream " << datastreamindex << " - trying to process data outside range - aborting!!! nearest sample was " << nearestsample << ", the max bytes should be " << datalengthbytes << ".  offsetseconds was " << offsetseconds << ", offsetns was " << offsetns << ", index was " << index << ", average delay was " << averagedelay << ", datasec was " << datasec << ", datans was " << datans << ", fftstartmicrosec was " << fftstartmicrosec << endl;
    return 0.0;
  }
  if(nearestsample == -1)
  {
    nearestsample = 0;
    dataweight = unpack(nearestsample);
  }
  else if(nearestsample < unpackstartsamples || nearestsample > unpackstartsamples + unpacksamples - fftchannels)
    //need to unpack more data
    dataweight = unpack(nearestsample);

  if(!(dataweight > 0.0))
    return 0.0;

  nearestsampletime = nearestsample*sampletime;
  fracsampleerror = float(starttime - nearestsampletime);

  if(!(config->getDPhaseCalIntervalMHz(configindex, datastreamindex) == 0)) 
  {
      for(int i=0;i<numrecordedbands;i++)
      {
        extractor[i]->adjustSampleOffset(datasamples+nearestsample);
        // status = extractor[i]->extractAndIntegrate(unpackedarrays[i], unpacksamples);
	status = extractor[i]->extractAndIntegrate(&(unpackedarrays[i][nearestsample
	    - unpackstartsamples]), fftchannels);
        if(status != true)
          csevere << startl << "Error in phase cal extractAndIntegrate" << endl;
      }
  }

  integerdelay = 0;
  switch(fringerotationorder) {
    case 0: //post-F
      integerdelay = static_cast<int>(averagedelay);
      break;
    case 1: //linear
      d0 = interpolator[0]*index*index + interpolator[1]*index + interpolator[2];
      d1 = interpolator[0]*(index+0.5)*(index+0.5) + interpolator[1]*(index+0.5) + interpolator[2];
      d2 = interpolator[0]*(index+1)*(index+1) + interpolator[1]*(index+1) + interpolator[2];
      a = d2-d0;
      b = d0 + (d1 - (a*0.5 + d0))/3.0;
      integerdelay = static_cast<int>(b);
      b -= integerdelay;

      status = vectorMulC_f64(subxoff, a, subxval, arraystridelength);
      if(status != vecNoErr)
        csevere << startl << "Error in linearinterpolate, subval multiplication" << endl;
      status = vectorMulC_f64(stepxoff, a, stepxval, numfrstrides);
      if(status != vecNoErr)
        csevere << startl << "Error in linearinterpolate, stepval multiplication" << endl;
      status = vectorAddC_f64_I(b, subxval, arraystridelength);
      if(status != vecNoErr)
        csevere << startl << "Error in linearinterpolate, subval addition!!!" << endl;
      break;
    case 2: //quadratic
      a = interpolator[0];
      b = interpolator[1] + index*interpolator[0]*2.0;
      c = interpolator[2] + index*interpolator[1] + index*index*interpolator[0];
      integerdelay = int(c);
      c -= integerdelay;

      status = vectorMulC_f64(subxoff, b + a*stepxoff[1], subxval, arraystridelength);
      if(status != vecNoErr)
        csevere << startl << "Error in quadinterpolate, subval multiplication" << endl;
      status = vectorMulC_f64(subxoff, 2*a*stepxoff[1], subquadxval, arraystridelength);
      if(status != vecNoErr)
        csevere << startl << "Error in quadinterpolate, subquadval multiplication" << endl;
      status = vectorMulC_f64(stepxoff, b, stepxval, numfrstrides);
      if(status != vecNoErr)
        csevere << startl << "Error in quadinterpolate, stepval multiplication" << endl;
      status = vectorMulC_f64(stepxoffsquared, a, tempstepxval, numfrstrides);
      if(status != vecNoErr)
        csevere << startl << "Error in quadinterpolate, tempstepval multiplication" << endl;
      status = vectorAdd_f64_I(tempstepxval, stepxval, numfrstrides);
      if(status != vecNoErr)
        csevere << startl << "Error in quadinterpolate, stepval addition!!!" << endl;
      status = vectorAddC_f64_I(c, subxval, arraystridelength);
      if(status != vecNoErr)
        csevere << startl << "Error in quadinterpolate, subval addition!!!" << endl;
      break;
  }

  //cout << "Done initial fringe rotation stuff, subquadsin is now " << subquadsin << endl;

  for(int i=0;i<numrecordedfreqs;i++)
  {
    count = 0;
    //updated so that Nyquist channel is not accumulated for either USB or LSB data
    //and is excised entirely, so both USB and LSB data start at the same place (no sidebandoffset)
    currentstepchannelfreqs = stepchannelfreqs;
    dcchannel = 0;
    acoffset = 0;
    fracsampptr1 = &(fracsamprotator[0]);
    fracsampptr2 = &(fracsamprotator[arraystridelength]);
    if(config->getDRecordedLowerSideband(configindex, datastreamindex, i))
    {
      currentstepchannelfreqs = lsbstepchannelfreqs;
      dcchannel = recordedbandchannels-1;
      if(config->anyUsbXLsb(configindex))
      {
        acoffset++;
      }
    }

    //get ready to apply fringe rotation, if its pre-F
    lofreq = config->getDRecordedFreq(configindex, datastreamindex, i);
    switch(fringerotationorder) {
      case 1: // linear
        status = vectorMulC_f64(subxval, lofreq, subphase, arraystridelength);
        if(status != vecNoErr)
          csevere << startl << "Error in linearinterpolate lofreq sub multiplication!!!" << status << endl;
        status = vectorMulC_f64(stepxval, lofreq, stepphase, numfrstrides);
        if(status != vecNoErr)
          csevere << startl << "Error in linearinterpolate lofreq step multiplication!!!" << status << endl;
        if(fractionalLoFreq) {
          status = vectorAddC_f64_I((lofreq-int(lofreq))*double(integerdelay), subphase, arraystridelength);
          if(status != vecNoErr)
            csevere << startl << "Error in linearinterpolate lofreq non-integer freq addition!!!" << status << endl;
        }
        for(int j=0;j<arraystridelength;j++) {
          subarg[j] = -TWO_PI*(subphase[j] - int(subphase[j]));
        }
        for(int j=0;j<numfrstrides;j++) {
          steparg[j] = -TWO_PI*(stepphase[j] - int(stepphase[j]));
        }
        status = vectorSinCos_f32(subarg, subsin, subcos, arraystridelength);
        if(status != vecNoErr)
          csevere << startl << "Error in sin/cos of sub rotate argument!!!" << endl;
        status = vectorSinCos_f32(steparg, stepsin, stepcos, numfrstrides);
        if(status != vecNoErr)
          csevere << startl << "Error in sin/cos of step rotate argument!!!" << endl;
        status = vectorRealToComplex_f32(subcos, subsin, complexrotator, arraystridelength);
        if(status != vecNoErr)
          csevere << startl << "Error assembling sub into complex!!!" << endl;
        status = vectorRealToComplex_f32(stepcos, stepsin, stepcplx, numfrstrides);
        if(status != vecNoErr)
          csevere << startl << "Error assembling step into complex!!!" << endl;
        for(int j=1;j<numfrstrides;j++) {
          status = vectorMulC_cf32(complexrotator, stepcplx[j], &complexrotator[j*arraystridelength], arraystridelength);
          if(status != vecNoErr)
            csevere << startl << "Error doing the time-saving complex multiplication!!!" << endl;
        }
        break;
      case 2: // Quadratic
        status = vectorMulC_f64(subxval, lofreq, subphase, arraystridelength);
        if(status != vecNoErr)
          csevere << startl << "Error in quadinterpolate lofreq sub multiplication!!!" << status << endl;
        if(fractionalLoFreq) {
          status = vectorAddC_f64_I((lofreq-int(lofreq))*double(integerdelay), subphase, arraystridelength);
          if(status != vecNoErr)
            csevere << startl << "Error in linearinterpolate lofreq non-integer freq addition!!!" << status << endl;
        }
        status = vectorMulC_f64(subquadxval, lofreq, subquadphase, arraystridelength);
        if(status != vecNoErr)
          csevere << startl << "Error in quadinterpolate lofreq subquad multiplication!!!" << status << endl;
        status = vectorMulC_f64(stepxval, lofreq, stepphase, numfrstrides);
        if(status != vecNoErr)
          csevere << startl << "Error in quadinterpolate lofreq step multiplication!!!" << status << endl;
        for(int j=0;j<arraystridelength;j++) {
          subarg[j] = -TWO_PI*(subphase[j] - int(subphase[j]));
          subquadarg[j] = -TWO_PI*(subquadphase[j] - int(subquadphase[j]));
        }
        for(int j=0;j<numfrstrides;j++) {
          steparg[j] = -TWO_PI*(stepphase[j] - int(stepphase[j]));
        }
        status = vectorSinCos_f32(subarg, subsin, subcos, arraystridelength);
        if(status != vecNoErr)
          csevere << startl << "Error in sin/cos of sub rotate argument!!!" << endl;
        status = vectorSinCos_f32(subquadarg, subquadsin, subquadcos, arraystridelength);
        if(status != vecNoErr)
          csevere << startl << "Error in sin/cos of subquad rotate argument!!!" << endl;
        status = vectorSinCos_f32(steparg, stepsin, stepcos, numfrstrides);
        if(status != vecNoErr)
          csevere << startl << "Error in sin/cos of step rotate argument!!!" << endl;
        status = vectorRealToComplex_f32(subcos, subsin, piecewiserotator, arraystridelength);
        if(status != vecNoErr)
          csevere << startl << "Error assembling sub into complex" << endl;
        status = vectorRealToComplex_f32(subquadcos, subquadsin, quadpiecerotator, arraystridelength);
        if(status != vecNoErr)
          csevere << startl << "Error assembling sub into complex" << endl;
        status = vectorRealToComplex_f32(stepcos, stepsin, stepcplx, numfrstrides);
        if(status != vecNoErr)
          csevere << startl << "Error assembling step into complex" << endl;
        for(int j=0;j<numfrstrides;j++) {
          status = vectorMulC_cf32(piecewiserotator, stepcplx[j], &complexrotator[j*arraystridelength], arraystridelength);
          if(status != vecNoErr)
            csevere << startl << "Error doing the time-saving complex mult (striding)" << endl;
          status = vectorMul_cf32_I(quadpiecerotator, piecewiserotator, arraystridelength);
          if(status != vecNoErr)
            csevere << startl << "Error doing the time-saving complex mult (adjusting linear gradient)" << endl;
        }
        break;
    }

    status = vectorMulC_f32(subchannelfreqs, fracsampleerror - recordedfreqclockoffsets[i], subfracsamparg, arraystridelength);
    if(status != vecNoErr) {
      csevere << startl << "Error in frac sample correction, arg generation (sub)!!!" << status << endl;
      exit(1);
    }
    status = vectorMulC_f32(currentstepchannelfreqs, fracsampleerror - recordedfreqclockoffsets[i], stepfracsamparg, numfracstrides/2);
    if(status != vecNoErr)
      csevere << startl << "Error in frac sample correction, arg generation (step)!!!" << status << endl;

    //sort out any LO offsets (+ fringe rotation if its post-F)
    if(fringerotationorder == 0) { // do both LO offset and fringe rotation  (post-F)
      phaserotation = (averagedelay-integerdelay)*lofreq;
      if(fractionalLoFreq)
        phaserotation += integerdelay*(lofreq-int(lofreq));
      phaserotation -= walltimesecs*recordedfreqlooffsets[i];
      phaserotationfloat = (f32)(-TWO_PI*(phaserotation-int(phaserotation)));
      status = vectorAddC_f32_I(phaserotationfloat, subfracsamparg, arraystridelength);
      if(status != vecNoErr)
        csevere << startl << "Error in post-f phase rotation addition (+ maybe LO offset correction), sub!!!" << status << endl;
    }
    else { //not post-F, but must take care of LO offsets if present
      if(recordedfreqlooffsets[i] > 0.0 || recordedfreqlooffsets[i] < 0.0)
      {
        phaserotation = -walltimesecs*recordedfreqlooffsets[i];
        phaserotationfloat = (f32)(-TWO_PI*(phaserotation-int(phaserotation)));
        status = vectorAddC_f32_I(phaserotationfloat, subfracsamparg, arraystridelength);
        if(status != vecNoErr)
          csevere << startl << "Error in LO offset correction (sub)!!!" << status << endl;
      }
    }

    //create the fractional sample correction array
    status = vectorSinCos_f32(subfracsamparg, subfracsampsin, subfracsampcos, arraystridelength);
    if(status != vecNoErr)
      csevere << startl << "Error in frac sample correction, sin/cos (sub)!!!" << status << endl;
    status = vectorSinCos_f32(stepfracsamparg, stepfracsampsin, stepfracsampcos, numfracstrides/2);
    if(status != vecNoErr)
      csevere << startl << "Error in frac sample correction, sin/cos (sub)!!!" << status << endl;
    status = vectorRealToComplex_f32(subfracsampcos, subfracsampsin, fracsampptr1, arraystridelength);
    if(status != vecNoErr)
      csevere << startl << "Error in frac sample correction, real to complex (sub)!!!" << status << endl;
    status = vectorRealToComplex_f32(stepfracsampcos, stepfracsampsin, stepfracsampcplx, numfracstrides/2);
    if(status != vecNoErr)
      csevere << startl << "Error in frac sample correction, real to complex (step)!!!" << status << endl;
    for(int j=1;j<numfracstrides/2;j++) {
      status = vectorMulC_cf32(fracsampptr1, stepfracsampcplx[j], &(fracsampptr2[(j-1)*arraystridelength]), arraystridelength);
      if(status != vecNoErr)
        csevere << startl << "Error doing the time-saving complex multiplication in frac sample correction!!!" << endl;
    }
    // now do the first arraystridelength elements (which are different from fracsampptr1 for LSB case)
    status = vectorMulC_cf32_I(stepfracsampcplx[0], fracsampptr1, arraystridelength);
    if(status != vecNoErr)
    csevere << startl << "Error doing the first bit of the time-saving complex multiplication in frac sample correction!!!" << endl;

    for(int j=0;j<numrecordedbands;j++)
    {
      if(config->matchingRecordedBand(configindex, datastreamindex, i, j))
      {
        indices[count++] = j;

        //fringe rotation
        switch(fringerotationorder) {
          case 0: //post-F
            fftptr = (config->getDRecordedLowerSideband(configindex, datastreamindex, i))?conjfftoutputs[j][subloopindex]:fftoutputs[j][subloopindex];

            //do the fft
	    // Chris add C2C fft for complex data
            status = vectorFFT_RtoC_f32(&(unpackedarrays[j][nearestsample - unpackstartsamples]), (f32*)fftptr, pFFTSpecR, fftbuffer);
            if(status != vecNoErr)
              csevere << startl << "Error in FFT!!!" << status << endl;
            //fix the lower sideband if required
            if(config->getDRecordedLowerSideband(configindex, datastreamindex, i))
            {
              status = vectorConjFlip_cf32(fftptr, fftoutputs[j][subloopindex], recordedbandchannels);
              if(status != vecNoErr)
                csevere << startl << "Error in conjugate!!!" << status << endl;
            }
            break;
  	  case 1: // Linear
  	  case 2: // Quadratic
	    if (usecomplex) {
	      status = vectorMul_cf32(complexrotator, &unpackedcomplexarrays[j][nearestsample - unpackstartsamples], complexunpacked, fftchannels);
              //CJP// status = vectorCopy_cf32(&unpackedcomplexarrays[j][nearestsample - unpackstartsamples], complexunpacked, recordedbandchannels);
	      if(status != vecNoErr)
		csevere << startl << "Error in real->complex conversion" << endl;
	    } else {
	      status = vectorRealToComplex_f32(&(unpackedarrays[j][nearestsample - unpackstartsamples]), NULL, complexunpacked, fftchannels);
	      if(status != vecNoErr)
		csevere << startl << "Error in real->complex conversion" << endl;
	      status = vectorMul_cf32_I(complexrotator, complexunpacked, fftchannels);
	      //if(status != vecNoErr)
	      //	csevere << startl << "Error in fringe rotation!!!" << status << endl;
	    }
	    status = vectorFFT_CtoC_cf32(complexunpacked, fftd, pFFTSpecC, fftbuffer);
            if(status != vecNoErr)
              csevere << startl << "Error doing the FFT!!!" << endl;
            if(!usecomplex && config->getDRecordedLowerSideband(configindex, datastreamindex, i)) {
              status = vectorCopy_cf32(&(fftd[recordedbandchannels+1]), fftoutputs[j][subloopindex], recordedbandchannels-1);
              fftoutputs[j][subloopindex][recordedbandchannels-1] = fftd[0];
            }
            else {
              status = vectorCopy_cf32(fftd, fftoutputs[j][subloopindex], recordedbandchannels);
            }
            if(status != vecNoErr)
              csevere << startl << "Error copying FFT results!!!" << endl;
            break;
        }

        if(dumpkurtosis) //do the necessary accumulation
        {
          status = vectorMagnitude_cf32(fftoutputs[j][subloopindex], kscratch, recordedbandchannels);
          if(status != vecNoErr)
            csevere << startl << "Error taking kurtosis magnitude!" << endl;
          status = vectorSquare_f32_I(kscratch, recordedbandchannels);
          if(status != vecNoErr)
            csevere << startl << "Error in first kurtosis square!" << endl;
          status = vectorAdd_f32_I(kscratch, s1[j], recordedbandchannels);
          if(status != vecNoErr)
            csevere << startl << "Error in kurtosis s1 accumulation!" << endl;
          status = vectorSquare_f32_I(kscratch, recordedbandchannels);
          if(status != vecNoErr)
            csevere << startl << "Error in second kurtosis square!" << endl;
          status = vectorAdd_f32_I(kscratch, s2[j], recordedbandchannels);
          if(status != vecNoErr)
            csevere << startl << "Error in kurtosis s2 accumulation!" << endl;
        }

        //do the frac sample correct (+ phase shifting if applicable, + fringe rotate if its post-f)
        status = vectorMul_cf32_I(fracsamprotator, fftoutputs[j][subloopindex], recordedbandchannels);
        if(status != vecNoErr)
          csevere << startl << "Error in application of frac sample correction!!!" << status << endl;

        //do the conjugation
        status = vectorConj_cf32(fftoutputs[j][subloopindex], conjfftoutputs[j][subloopindex], recordedbandchannels);
        if(status != vecNoErr)
          csevere << startl << "Error in conjugate!!!" << status << endl;

        //do the autocorrelation
        if (!config->rfifilterEnabled()) 
        {
          // skipping Nyquist channel - if any LSB * USB correlations, shift all LSB bands to appear as USB
          DUT_CHECK( vectorAddProduct_cf32(fftoutputs[j][subloopindex]+acoffset, conjfftoutputs[j][subloopindex]+acoffset, autocorrelations[0][j]+acoffset, recordedbandchannels-acoffset), csevere, "Error in autocorrelation!!!" );
        } 
        else 
        {
          // do not skip any channels here, filter all
          DUT_CHECK( vectorMul_cf32(fftoutputs[j][subloopindex], conjfftoutputs[j][subloopindex], rfiscratch, recordedbandchannels), csevere, "Error in autocorrelation scratch!!!" );
          // filter operates into vector memory {autocorrelations[0][j]}
          autocorrfilters[0][j]->filter(rfiscratch); 
        }

        //store the weight
        weights[0][j] += dataweight;
      }
    }

    //if we need to, do the cross-polar autocorrelations
    //JanW: potential BUG, why is xpol ac computed only for first pair of all numrecordedbands?
    if(calccrosspolautocorrs && count > 1)
    {
      if (!config->rfifilterEnabled())
      {
        status = vectorAddProduct_cf32(fftoutputs[indices[0]][subloopindex]+acoffset, conjfftoutputs[indices[1]][subloopindex]+acoffset, autocorrelations[1][indices[0]]+acoffset, recordedbandchannels-acoffset);
        if(status != vecNoErr)
          csevere << startl << "Error in cross-polar autocorrelation!!!" << status << endl;
        status = vectorAddProduct_cf32(fftoutputs[indices[1]][subloopindex]+acoffset, conjfftoutputs[indices[0]][subloopindex]+acoffset, autocorrelations[1][indices[1]]+acoffset, recordedbandchannels-acoffset);
        if(status != vecNoErr)
          csevere << startl << "Error in cross-polar autocorrelation!!!" << status << endl;
      }
      else
      {
        // set filters to operate into preallocated vector memory (done here because indices[] not known in c'stor)
        autocorrfilters[1][0]->setUserOutbuffer(autocorrelations[1][indices[0]]);
        autocorrfilters[1][1]->setUserOutbuffer(autocorrelations[1][indices[1]]);
        // finally, filter
        DUT_CHECK( vectorMul_cf32(fftoutputs[indices[0]][subloopindex], conjfftoutputs[indices[1]][subloopindex], rfiscratch, recordedbandchannels), csevere, "Error in cross-polar autocorrelation!!!" );
        autocorrfilters[1][0]->filter(rfiscratch);
        DUT_CHECK( vectorMul_cf32(fftoutputs[indices[1]][subloopindex], conjfftoutputs[indices[0]][subloopindex], rfiscratch, recordedbandchannels), csevere, "Error in cross-polar autocorrelation!!!" );
        autocorrfilters[1][1]->filter(rfiscratch);
      }

      //store the weights
      weights[1][indices[0]] += dataweight;
      weights[1][indices[1]] += dataweight;
    }
  }

  return dataweight;
}

void Mode::averageFrequency()
{
  cf32 tempsum;
  int status, outputchans;

  if(channelstoaverage == 1)
    return; //no need to do anything;

  outputchans = recordedbandchannels/channelstoaverage;
  for(int i=0;i<autocorrwidth;i++)
  {
    for(int j=0;j<numrecordedbands;j++)
    {
      status = vectorMean_cf32(autocorrelations[i][j], channelstoaverage, &tempsum, vecAlgHintFast);
      if(status != vecNoErr)
        cerror << startl << "Error trying to average in frequency!" << endl;
      autocorrelations[i][j][0] = tempsum;
      for(int k=1;k<outputchans;k++)
      {
        status = vectorMean_cf32(&(autocorrelations[i][j][k*channelstoaverage]), channelstoaverage, &(autocorrelations[i][j][k]), vecAlgHintFast);
        if(status != vecNoErr)
          cerror << startl << "Error trying to average in frequency!" << endl;
      }
    }
  }
}

bool Mode::calculateAndAverageKurtosis(int numblocks, int maxchannels)
{
  int status, kchanavg;
  bool nonzero = false;

  for(int i=0;i<numrecordedbands;i++)
  {
    nonzero = false;
    for(int j=0;j<recordedbandchannels;j++)
    {
      if(s1[i][j] > 0.0)
      {
        nonzero = true;
        break;
      }
    }
    if(!nonzero)
      continue;
    status = vectorSquare_f32_I(s1[i], recordedbandchannels);
    if(status != vecNoErr)
      cerror << startl << "Problem calculating kurtosis (squaring s1)" << endl;
    status = vectorMulC_f32_I((f32)numblocks, s2[i], recordedbandchannels);
    if(status != vecNoErr)
      cerror << startl << "Problem calculating kurtosis (scaling s2)" << endl;
    status = vectorDivide_f32(s1[i], s2[i], sk[i], recordedbandchannels);
    if(status != vecNoErr)
      cerror << startl << "Problem calculating kurtosis (s2/s1^2)" << endl;
    status = vectorAddC_f32_I(-1.0, sk[i], recordedbandchannels);
    if(status != vecNoErr)
      cerror << startl << "Problem calculating kurtosis (sk - 1)" << endl;
    status = vectorMulC_f32_I((f32)numblocks/(numblocks - 1.0), sk[i], recordedbandchannels);
    if(status != vecNoErr)
      cerror << startl << "Problem calculating kurtosis (sk * numblocks/(numblocks-1))" << endl;
    status = vectorAddC_f32_I(-1.0, sk[i], recordedbandchannels);
    if(status != vecNoErr)
      cerror << startl << "Problem calculating kurtosis (sk - 1)" << endl;
    if(maxchannels < recordedbandchannels)
    {
      kchanavg = recordedbandchannels/maxchannels;
      status = vectorMulC_f32_I(1.0/((f32)kchanavg), sk[i], recordedbandchannels);
      if(status != vecNoErr)
        cerror << startl << "Problem calculating kurtosis (sk average)" << endl;
      for(int j=0;j<maxchannels;j++)
      {
        sk[i][j] = sk[i][j*kchanavg];
        for(int k=1;k<kchanavg;k++)
          sk[i][j] += sk[i][j*kchanavg + k];
      }
    }
  }

  return nonzero;
}

void Mode::zeroAutocorrelations()
{
  int status;

  for(int i=0;i<autocorrwidth;i++)
  {
    for(int j=0;j<numrecordedbands;j++)
    {
      if (config->rfifilterEnabled())
        autocorrfilters[i][j]->clear(); // zero the internal state and output buffer (can be autocorrelations[][])
      DUT_CHECK( vectorZero_cf32(autocorrelations[i][j], recordedbandchannels), cerror, "Error trying to zero autocorrelations!" );
      weights[i][j] = 0.0;
    }
  }
}

void Mode::zeroKurtosis()
{
  int status;

  if(sk == 0)
  {
    s1 = new f32*[numrecordedbands];
    s2 = new f32*[numrecordedbands];
    sk = new f32*[numrecordedbands];
    kscratch = vectorAlloc_f32(recordedbandchannels);
    for(int i=0;i<numrecordedbands;i++)
    {
      s1[i] = vectorAlloc_f32(recordedbandchannels);
      s2[i] = vectorAlloc_f32(recordedbandchannels);
      sk[i] = vectorAlloc_f32(recordedbandchannels);
    }
  }

  for(int i=0;i<numrecordedbands;i++)
  {
    status = vectorZero_f32(s1[i], recordedbandchannels);
    if(status != vecNoErr)
      cerror << startl << "Error trying to zero kurtosis!" << endl;
    status = vectorZero_f32(s2[i], recordedbandchannels);
    if(status != vecNoErr)
      cerror << startl << "Error trying to zero kurtosis!" << endl;
  }
}

void Mode::setOffsets(int scan, int seconds, int ns)
{
  bool foundok;
  int srcindex;
  currentscan = scan;
  offsetseconds = seconds;
  offsetns = ns;

  if(datasec <= INVALID_SUBINT)
    return; //there is no valid data - this whole subint will be ignored

  if(currentscan != datascan) {
    cerror << startl << "Received a request to process scan " << currentscan << " (" << seconds << "/" << ns << " but received data from scan " << datascan << " (" << datasec << "/" << datans << ") - I'm confused and will ignore this data!" << endl;
    datalengthbytes = 0; //torch the whole subint - can't work with different scans!
    return;
  }

  //fill in the quadratic interpolator values from model
  if(model->getNumPhaseCentres(currentscan) == 1)
    srcindex = 1;
  else
    srcindex = 0;

  f64 timespan = blockspersend*2*recordedbandchannels*sampletime/1e6;
  if (usecomplex) timespan/=2;
  foundok = model->calculateDelayInterpolator(currentscan, (double)offsetseconds + ((double)offsetns)/1000000000.0, timespan, blockspersend, config->getDModelFileIndex(configindex, datastreamindex), srcindex, 2, interpolator);
  interpolator[2] -= 1000000*intclockseconds;

  if(!foundok) {
    cerror << startl << "Could not find a Model interpolator for scan " << scan << " offsetseconds " << seconds << " offsetns " << ns << " - will torch this subint!" << endl;
    offsetseconds = INVALID_SUBINT;
  }
}

void Mode::setValidFlags(s32 * v)
{
  int status = vectorCopy_s32(v, validflags, flaglength);
  if(status != vecNoErr)
    csevere << startl << "Error trying to copy valid data flags!!!" << endl;
}

void Mode::setData(u8 * d, int dbytes, int dscan, int dsec, int dns)
{
  data = d;
  datalengthbytes = dbytes;
  datascan = dscan;
  datasec = dsec;
  datans = dns;
  unpackstartsamples = -999999999;
  datasamples = static_cast<int>(datans/(sampletime*1e3));
}

void Mode::resetpcal()
{
  for(int i=0;i<numrecordedbands;i++)
  {
    extractor[i]->clear();
  }
}

void Mode::finalisepcal()
{
  for(int i=0;i<numrecordedbands;i++)
  {
    uint64_t samples = extractor[i]->getFinalPCal(pcalresults[i]);
    if ((samples == 0) && (datasec != INVALID_SUBINT) && (datalengthbytes > 1)) {
        //cdebug << startl << "finalisepcal band " << i << " samples==0 over valid subint " << datasec << "s+" << datans << "ns" << endl;
    }
  }
}

const float Mode::decorrelationpercentage[] = {0.63662, 0.88, 0.94, 0.96, 0.98, 0.99, 0.996, 0.998}; //note these are just approximate!!!
// vim: shiftwidth=2:softtabstop=2:expandtab
