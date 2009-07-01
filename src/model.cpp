/***************************************************************************
 *   Copyright (C) 2009 by Adam Deller                                     *
 *                                                                         *
 *   This program is free for non-commercial use: see the license file     *
 *   at  http://cira.ivec.org/dokuwiki/doku.php/difx/documentation for     *
 *   more details.                                                         *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                  *
 ***************************************************************************/
//===========================================================================
// SVN properties (DO NOT CHANGE)
//
// $Id: $
// $HeadURL: $
// $LastChangedRevision: $
// $Author: $
// $LastChangedDate: $
//
//============================================================================

#include <sstream>
#include "architecture.h"
#include "configuration.h"
#include "alert.h"
#include "model.h"

Model::Model(Configuration * conf, string cfilename)
  : config(conf), calcfilename(cfilename)
{
  opensuccess = true;
  ifstream * input = new ifstream(calcfilename.c_str());

  if(!input->is_open() || input->bad()) {
    cfatal << startl << "Error opening model file " << calcfilename << " - aborting!!!" << endl;
    opensuccess = false;
  }

  estimatedbytes = 0;
  stationtable = 0;
  sourcetable = 0;
  eoptable = 0;
  spacecrafttable = 0;
  scantable = 0;
  tpowerarray = 0;

  //read the files
  if(opensuccess) 
    opensuccess = readInfoData(input);
  if(opensuccess)
    opensuccess = readCommonData(input);
  if(opensuccess)
    opensuccess = readStationData(input);
  if(opensuccess)
    opensuccess = readSourceData(input);
  if(opensuccess)
    opensuccess = readScanData(input);
  if(opensuccess)
    opensuccess = readEOPData(input);
  if(opensuccess)
    opensuccess = readSpacecraftData(input);
  if(opensuccess)
    opensuccess = readPolynomialSamples(input);

  tpowerarray = vectorAlloc_f64(polyorder+1);

  delete input;
}

Model::~Model()
{
  if(tpowerarray)
    vectorFree(tpowerarray);
  if(stationtable)
    delete [] stationtable;
  if(sourcetable)
    delete [] sourcetable;
  if(eoptable)
    delete [] eoptable;
  if(spacecrafttable) {
    for(int i=0;i<numspacecraft;i++) {
      delete [] spacecrafttable[i].samplemjd;
      delete [] spacecrafttable[i].x;
      delete [] spacecrafttable[i].y;
      delete [] spacecrafttable[i].z;
      delete [] spacecrafttable[i].vx;
      delete [] spacecrafttable[i].vy;
      delete [] spacecrafttable[i].vz;
    }
    delete [] spacecrafttable;
  }
  if(scantable) {
    for(int i=0;i<numscans;i++) {
      for(int j=0;j<scantable[i].nummodelsamples;j++) {
        for(int k=0;k<scantable[i].numphasecentres+1;k++) {
          for(int l=0;l<numstations;l++) {
            vectorFree(scantable[i].u[j][k][l]);
            vectorFree(scantable[i].v[j][k][l]);
            vectorFree(scantable[i].w[j][k][l]);
            vectorFree(scantable[i].delay[j][k][l]);
            vectorFree(scantable[i].wet[j][k][l]);
            vectorFree(scantable[i].dry[j][k][l]);
          }
          delete [] scantable[i].u[j][k];
          delete [] scantable[i].v[j][k];
          delete [] scantable[i].w[j][k];
          delete [] scantable[i].delay[j][k];
          delete [] scantable[i].wet[j][k];
          delete [] scantable[i].dry[j][k];
        }
        delete [] scantable[i].u[j];
        delete [] scantable[i].v[j];
        delete [] scantable[i].w[j];
        delete [] scantable[i].delay[j];
        delete [] scantable[i].wet[j];
        delete [] scantable[i].dry[j];
      }
      delete [] scantable[i].u;
      delete [] scantable[i].v;
      delete [] scantable[i].w;
      delete [] scantable[i].delay;
      delete [] scantable[i].wet;
      delete [] scantable[i].dry;
      delete [] scantable[i].phasecentres;
    }
    delete [] scantable;
  }
}

bool Model::interpolateUVW(int scanindex, double offsettime, int antennaindex1, int antennaindex2, int scansourceindex, double* uvw)
{
  int scansample, polyoffset;
  double deltat, tempuvw;
  double * coeffs;

  //work out the correct sample and offset from that sample
  polyoffset = (modelmjd - scantable[scanindex].polystartmjd)*86400 + (modelstartseconds + (int)offsettime) - scantable[scanindex].polystartseconds;
  scansample = int((offsettime+polyoffset)/double(modelincsecs));
  if(scansample < 0 || scansample >= scantable[scanindex].nummodelsamples)
    return false;
  deltat = offsettime+polyoffset - scansample*modelincsecs;

  tpowerarray[0] = 1.0;
  for(int i=0;i<polyorder;i++)
    tpowerarray[i+1] = tpowerarray[i]*deltat;

  //calculate the uvw values
  coeffs = scantable[scanindex].u[scansample][scansourceindex][antennaindex1];
  vectorDotProduct_f64(tpowerarray, coeffs, polyorder+1, &(uvw[0]));
  coeffs = scantable[scanindex].u[scansample][scansourceindex][antennaindex2];
  vectorDotProduct_f64(tpowerarray, coeffs, polyorder+1, &tempuvw);
  uvw[0] = uvw[0] - tempuvw;
  coeffs = scantable[scanindex].v[scansample][scansourceindex][antennaindex1];
  vectorDotProduct_f64(tpowerarray, coeffs, polyorder+1, &(uvw[1]));
  coeffs = scantable[scanindex].v[scansample][scansourceindex][antennaindex2];
  vectorDotProduct_f64(tpowerarray, coeffs, polyorder+1, &tempuvw);
  uvw[1] = uvw[1] - tempuvw;
  coeffs = scantable[scanindex].w[scansample][scansourceindex][antennaindex1];
  vectorDotProduct_f64(tpowerarray, coeffs, polyorder+1, &(uvw[2]));
  coeffs = scantable[scanindex].w[scansample][scansourceindex][antennaindex2];
  vectorDotProduct_f64(tpowerarray, coeffs, polyorder+1, &tempuvw);
  uvw[2] = uvw[2] - tempuvw;
  return true;
}

bool Model::calculateDelayInterpolator(int scanindex, f64 offsettime, f64 timespan, int numincrements, int antennaindex, int scansourceindex, int order, f64 * delaycoeffs)
{
  int scansample, status, polyoffset;
  double deltat;
  double delaysamples[3];

  //check that order is ok
  if(order < 0 || order > 2) {
    csevere << startl << "Model delay interpolator asked to produce " << order << "th order output - can only do 0, 1 or 2!" << endl;
    return false;
  } 

  //work out the correct sample and offset for the midrange of the timespan
  polyoffset = (modelmjd - scantable[scanindex].polystartmjd)*86400 + (modelstartseconds + (int)offsettime) - scantable[scanindex].polystartseconds;
  scansample = int((offsettime+polyoffset)/double(modelincsecs));
  if(scansample < 0 || scansample >= scantable[scanindex].nummodelsamples) {
    cwarn << startl << "Model delay interpolator was asked to produce results for scan " << scanindex << " from outside the scans valid range (worked out scansample " << scansample << ", when numsamples was " << scantable[scanindex].nummodelsamples << ")" << endl;
    return false;
  }
  deltat = offsettime+polyoffset+timespan/2.0 - scansample*modelincsecs;
  tpowerarray[0] = 1.0;
  for(int i=0;i<polyorder;i++)
    tpowerarray[i+1] = tpowerarray[i]*deltat;
  
  //zero-th order interpolation - the simplest case
  if(order==0) {
    //cout << "Model is calculating a zero-th order delay, with offsettime " << offsettime << ", scansourceindex " << scansourceindex << ", scan " << scanindex << ", and polyoffset " << polyoffset << ", scansample was " << scansample << endl;
    //cout << "tpowerarray[0],[1],[2] is " << tpowerarray[0] << "," << tpowerarray[1] << "," << tpowerarray[2] << ", delay[0][1][2] is " << scantable[scanindex].delay[scansample][scansourceindex][antennaindex][0] << "," << scantable[scanindex].delay[scansample][scansourceindex][antennaindex][1] << "," << scantable[scanindex].delay[scansample][scansourceindex][antennaindex][2] << endl;
    status = vectorDotProduct_f64(tpowerarray, scantable[scanindex].delay[scansample][scansourceindex][antennaindex], polyorder+1, delaycoeffs);
    if (status != vecNoErr)
      cerror << startl << "Error calculating zero-th order interpolation in Model" << endl;
    return true; //note return
  }

  //If not 0th order interpolation, need to fill out all 3 spots
  status = vectorDotProduct_f64(tpowerarray, scantable[scanindex].delay[scansample][scansourceindex][antennaindex], polyorder+1, &(delaysamples[1]));
  if (status != vecNoErr)
    cerror << startl << "Error calculating sample 1 for interpolation in Model" << endl;
  deltat = offsettime+polyoffset - scansample*modelincsecs;
  for(int i=0;i<polyorder;i++)
    tpowerarray[i+1] = tpowerarray[i]*deltat;
  status = vectorDotProduct_f64(tpowerarray, scantable[scanindex].delay[scansample][scansourceindex][antennaindex], polyorder+1, &(delaysamples[0]));
  if (status != vecNoErr)
    cerror << startl << "Error calculating sample 0 for interpolation in Model" << endl;
  deltat = offsettime+polyoffset + timespan - scansample*modelincsecs;
  for(int i=0;i<polyorder;i++)
    tpowerarray[i+1] = tpowerarray[i]*deltat;
  status = vectorDotProduct_f64(tpowerarray, scantable[scanindex].delay[scansample][scansourceindex][antennaindex], polyorder+1, &(delaysamples[2]));
  if (status != vecNoErr)
    cerror << startl << "Error calculating sample 2 for interpolation in Model" << endl;
  //cout << "In calculateinterpolator, sample0 was " << delaysamples[0] << ", sample1 was " << delaysamples[1] << ", sample2 was " << delaysamples[2] << ", timespan was " << timespan << endl;
 
  //linear interpolation
  if(order==1) {
    delaycoeffs[0] = (delaysamples[2]-delaysamples[0])/numincrements;
    delaycoeffs[1] = delaysamples[0] + (delaysamples[1] - (delaycoeffs[0]*numincrements/2.0 + delaysamples[0]))/3.0;
    return true; //note return
  }

  //quadratic interpolation
  delaycoeffs[0] = (2.0*delaysamples[0]-4.0*delaysamples[1]+2.0*delaysamples[2])/(numincrements*numincrements);
  delaycoeffs[1] = (-3.0*delaysamples[0]+4.0*delaysamples[1]-delaysamples[2])/numincrements;
  delaycoeffs[2] = delaysamples[0];
  //cout << "Interpolator produced coefficients " << delaycoeffs[0] << ", " << delaycoeffs[1] << ", " << delaycoeffs[2] << " from samples " << delaysamples[0] << ", " << delaysamples[1] << ", " << delaysamples[2] << " for a time range " << timespan << endl;
  return true;
}

bool Model::readInfoData(ifstream * input)
{
  string line = "";
  //nothing here is worth saving, so just skip it all
  config->getinputline(input, &line, "JOB ID");
  config->getinputline(input, &line, "JOB START TIME");
  config->getinputline(input, &line, "JOB STOP TIME");
  config->getinputline(input, &line, "DUTY CYCLE");
  config->getinputline(input, &line, "OBSCODE");
  config->getinputline(input, &line, "DIFX VERSION");
  config->getinputline(input, &line, "SUBJOB ID");
  config->getinputline(input, &line, "SUBARRAY ID");
  return true;
}

bool Model::readCommonData(ifstream * input)
{
  int year, month, day, hour, minute, second;
  double mjd;
  string line = "";

  //Get the start time
  config->getinputline(input, &line, "START MJD");
  mjd = atof(line.c_str());
  config->getinputline(input, &line, "START YEAR");
  year = atoi(line.c_str());
  config->getinputline(input, &line, "START MONTH");
  month = atoi(line.c_str());
  config->getinputline(input, &line, "START DAY");
  day = atoi(line.c_str());
  config->getinputline(input, &line, "START HOUR");
  hour = atoi(line.c_str());
  config->getinputline(input, &line, "START MINUTE");
  minute = atoi(line.c_str());
  config->getinputline(input, &line, "START SECOND");
  second = atoi(line.c_str());
  config->getMJD(modelmjd, modelstartseconds, year, month, day, hour, minute, second);
  if(fabs(mjd - ((double)modelmjd+(double)modelstartseconds/86400.0)) > 0.000001)
    cwarn << startl << " START MJD does not seem to agree with START YEAR/MONTH/.../SECOND..?" << mjd-int(mjd) << ", " << (double)modelstartseconds/86400.0 << endl;

  //ignore the next two lines, not relevant to the correlator
  config->getinputline(input, &line, "SPECTRAL AVG");
  config->getinputline(input, &line, "TAPER FUNCTION");
  return true;
}

bool Model::readStationData(ifstream * input)
{
  string line = "";

  config->getinputline(input, &line, "NUM TELESCOPES");
  numstations = atoi(line.c_str());
  stationtable = new station[numstations];
  for(int i=0;i<numstations;i++) {
    config->getinputline(input, &(stationtable[i].name), "TELESCOPE ", i);
    //trim the whitespace off the end
    while((stationtable[i].name).at((stationtable[i].name).length()-1) == ' ')
      stationtable[i].name = (stationtable[i].name).substr(0, (stationtable[i].name).length()-1);
    config->getinputline(input, &line, "TELESCOPE ", i);
    stationtable[i].mount = getMount(line);
    config->getinputline(input, &line, "TELESCOPE ", i);
    stationtable[i].axisoffset = atoi(line.c_str());
    config->getinputline(input, &line, "TELESCOPE ", i);
    stationtable[i].x = atoi(line.c_str());
    config->getinputline(input, &line, "TELESCOPE ", i);
    stationtable[i].y = atoi(line.c_str());
    config->getinputline(input, &line, "TELESCOPE ", i);
    stationtable[i].z = atoi(line.c_str());
    config->getinputline(input, &line, "TELESCOPE ", i); //ignore this, its the shelf
  }
  return true;
}
bool Model::readSourceData(ifstream * input)
{
  string line = "";

  config->getinputline(input, &line, "NUM SOURCES");
  numsources = atoi(line.c_str());
  sourcetable = new source[numsources];
  for(int i=0;i<numsources;i++) {
    sourcetable[i].index = i;
    config->getinputline(input, &(sourcetable[i].name), "SOURCE ", i);
    //trim the whitespace off the end
    while((sourcetable[i].name).at((sourcetable[i].name).length()-1) == ' ')
      sourcetable[i].name = (sourcetable[i].name).substr(0, (sourcetable[i].name).length()-1);
    config->getinputline(input, &line, "SOURCE ", i);
    sourcetable[i].ra = atof(line.c_str());
    config->getinputline(input, &line, "SOURCE ", i);
    sourcetable[i].dec = atof(line.c_str());
    config->getinputline(input, &(sourcetable[i].calcode), "SOURCE ", i);
    config->getinputline(input, &line, "SOURCE ", i);
    sourcetable[i].qual = atoi(line.c_str());
  }
  return true;
}

bool Model::readScanData(ifstream * input)
{
  int at, next;
  string line = "";

  config->getinputline(input, &line, "NUM SCANS");
  numscans = atoi(line.c_str());
  scantable = new scan[numscans];
  for(int i=0;i<numscans;i++) {
    config->getinputline(input, &scantable[i].identifier, "SCAN ", i);
    config->getinputline(input, &line, "SCAN ", i);
    scantable[i].offsetseconds = atoi(line.c_str());
    config->getinputline(input, &line, "SCAN ", i);
    scantable[i].durationseconds = atoi(line.c_str());
    config->getinputline(input, &(scantable[i].obsmodename), "SCAN ", i);
    config->getinputline(input, &line, "SCAN ", i);
    scantable[i].pointingcentre = &(sourcetable[atoi(line.c_str())]);
    config->getinputline(input, &line, "SCAN ", i);
    scantable[i].numphasecentres = atoi(line.c_str());
    scantable[i].phasecentres = new source*[scantable[i].numphasecentres];
    scantable[i].pointingcentrecorrelated = false;
    for(int j=0;j<scantable[i].numphasecentres;j++) {
      config->getinputline(input, &line, "SCAN ", i);
      scantable[i].phasecentres[j] = &(sourcetable[atoi(line.c_str())]);
      if(scantable[i].phasecentres[j] == scantable[i].pointingcentre) {
        scantable[i].pointingcentrecorrelated = true;
        if(j != 0) {
          cfatal << startl << "If pointing centre is correlated, it must be the first phase centre - aborting!" << endl;
          return false;
        }
      }
    }
  }
  return true;
}

bool Model::readEOPData(ifstream * input)
{
  string line = "";

  config->getinputline(input, &line, "NUM EOPS");
  numeops = atoi(line.c_str());
  eoptable = new eop[numeops];
  for(int i=0;i<numeops;i++) {
    config->getinputline(input, &line, "EOP ", i);
    eoptable[i].mjd = atoi(line.c_str());
    config->getinputline(input, &line, "EOP ", i);
    eoptable[i].taiutc = atoi(line.c_str());
    config->getinputline(input, &line, "EOP ", i);
    eoptable[i].ut1utc = atof(line.c_str());
    config->getinputline(input, &line, "EOP ", i);
    eoptable[i].xpole = atof(line.c_str());
    config->getinputline(input, &line, "EOP ", i);
    eoptable[i].ypole = atof(line.c_str());
  }
  return true;
}

bool Model::readSpacecraftData(ifstream * input)
{
  string line = "";

  config->getinputline(input, &line, "NUM SPACECRAFT");
  numspacecraft = atoi(line.c_str());
  spacecrafttable = new spacecraft[numspacecraft];
  for(int i=0;i<numspacecraft;i++) {
    config->getinputline(input, &spacecrafttable[i].name, "SPACECRAFT ", i);
    config->getinputline(input, &line, "SPACECRAFT ", i);
    spacecrafttable[i].numsamples = atoi(line.c_str());
    spacecrafttable[i].samplemjd = new double[spacecrafttable[i].numsamples];
    spacecrafttable[i].x  = new double[spacecrafttable[i].numsamples];
    spacecrafttable[i].y  = new double[spacecrafttable[i].numsamples];
    spacecrafttable[i].z  = new double[spacecrafttable[i].numsamples];
    spacecrafttable[i].vx = new double[spacecrafttable[i].numsamples];
    spacecrafttable[i].vy = new double[spacecrafttable[i].numsamples];
    spacecrafttable[i].vz = new double[spacecrafttable[i].numsamples];
    for(int j=0;j<spacecrafttable[i].numsamples;j++) {
      config->getinputline(input, &line, "SPACECRAFT ", i);
      spacecrafttable[i].samplemjd[j] = atof(line.substr(0,17).c_str());
      spacecrafttable[i].x[j] = atof(line.substr(18,18).c_str());
      spacecrafttable[i].y[j] = atof(line.substr(37,18).c_str());
      spacecrafttable[i].z[j] = atof(line.substr(56,18).c_str());
      spacecrafttable[i].vx[j] = atof(line.substr(75,18).c_str());
      spacecrafttable[i].vy[j] = atof(line.substr(94,18).c_str());
      spacecrafttable[i].vz[j] = atof(line.substr(103,18).c_str());
    }
  }
  return true;
}

bool Model::readPolynomialSamples(ifstream * input)
{
  int year, month, day, hour, minute, second, mjd, daysec;
  string line;
  bool polyok = true;

  config->getinputline(input, &imfilename, "IM FILENAME");
  input->close();

  input->open(imfilename.c_str());
  if(!input->is_open() || input->bad()) {
    cfatal << startl << "Error opening IM file " << imfilename << " - aborting!!!" << endl;
    return false; //note exit here
  }
  //The following data is not needed here - just skim over it
  config->getinputline(input, &line, "CALC SERVER");
  config->getinputline(input, &line, "CALC PROGRAM");
  config->getinputline(input, &line, "CALC VERSION");

  //Now we get to some stuff that should be checked
  config->getinputline(input, &line, "START YEAR");
  year = atoi(line.c_str());
  config->getinputline(input, &line, "START MONTH");
  month = atoi(line.c_str());
  config->getinputline(input, &line, "START DAY");
  day = atoi(line.c_str());
  config->getinputline(input, &line, "START HOUR");
  hour = atoi(line.c_str());
  config->getinputline(input, &line, "START MINUTE");
  minute = atoi(line.c_str());
  config->getinputline(input, &line, "START SECOND");
  second = atoi(line.c_str());
  config->getMJD(mjd, daysec, year, month, day, hour, minute, second);
  if(!((mjd == modelmjd) && (daysec == modelstartseconds))) {
    cfatal << startl << "IM file and CALC file start dates disagree - aborting!!!" << endl;
    cfatal << startl << "MJD from IM file is " << mjd << ", from CALC file is " << modelmjd << ", IM file sec is " << daysec << ", CALC file sec is " << modelstartseconds << endl;
    return false;
  }

  //some important info on the polynomials
  config->getinputline(input, &line, "POLYNOMIAL ORDER");
  polyorder = atoi(line.c_str());
  config->getinputline(input, &line, "INTERVAL (SECS)");
  modelincsecs = atoi(line.c_str());

  //Another unimportant value
  config->getinputline(input, &line, "ABERRATION CORR");

  //now check the telescope names match
  config->getinputline(input, &line, "NUM TELESCOPES");
  if(numstations != atoi(line.c_str())) {
    cfatal << startl << "IM file and CALC file disagree on number of telescopes - aborting!!!" << endl;
    return false;
  }
  for(int i=0;i<numstations;i++) {
    config->getinputline(input, &line, "TELESCOPE ", i);
    if(line.compare(stationtable[i].name) != 0) {
      cfatal << startl << "IM file and CALC file disagree on telescope " << i << " name - aborting!!!" << endl;
      return false;
    }
  }

  //now loop through scans - make sure sources match, and store polynomials
  config->getinputline(input, &line, "NUM SCANS");
  if(numscans != atoi(line.c_str())) {
    cfatal << startl << "IM file and CALC file disagree on number of scans - aborting!!!" << endl;
    return false;
  }
  for(int i=0;i<numscans;i++) {
    config->getinputline(input, &line, "SCAN ", i);
    if(line.compare((scantable[i].pointingcentre)->name) != 0) {
      cfatal << startl << "IM file and CALC file disagree on scan " << i << " pointing centre - aborting!!!" << endl;
      return false;
    }
    config->getinputline(input, &line, "SCAN ", i);
    if(scantable[i].numphasecentres != atoi(line.c_str())) {
      cfatal << startl << "IM file and CALC file disagree on scan " << i << " number of phase centres - aborting!!!" << endl;
      return false;
    }
    for(int j=0;j<scantable[i].numphasecentres;j++) {
      config->getinputline(input, &line, "SCAN ", i);
      if(line.compare((scantable[i].phasecentres[j])->name) != 0) {
        cfatal << startl << "IM file and CALC file disagree on scan " << i << " phase centre " << j << " - aborting!!!" << endl;
        return false;
      }
    }
    config->getinputline(input, &line, "SCAN ", i);
    scantable[i].nummodelsamples = atoi(line.c_str());
    scantable[i].u = new f64***[scantable[i].nummodelsamples];
    scantable[i].v = new f64***[scantable[i].nummodelsamples];
    scantable[i].w = new f64***[scantable[i].nummodelsamples];
    scantable[i].delay = new f64***[scantable[i].nummodelsamples];
    scantable[i].wet = new f64***[scantable[i].nummodelsamples];
    scantable[i].dry = new f64***[scantable[i].nummodelsamples];
    for(int j=0;j<scantable[i].nummodelsamples;j++) {
      config->getinputline(input, &line, "SCAN ", i);
      mjd = atoi(line.c_str());
      config->getinputline(input, &line, "SCAN ", i);
      daysec = atoi(line.c_str());
      if(j==0) {
        scantable[i].polystartmjd = mjd;
        scantable[i].polystartseconds = daysec;
      }
      else if((mjd-scantable[i].polystartmjd)*86400 + daysec-scantable[i].polystartseconds != j*modelincsecs) {
        cfatal << startl << "IM file has polynomials separated by a different amount than increment - aborting!" << endl;
        return false;
      }
      scantable[i].u[j] = new f64**[scantable[i].numphasecentres+1];
      scantable[i].v[j] = new f64**[scantable[i].numphasecentres+1];
      scantable[i].w[j] = new f64**[scantable[i].numphasecentres+1];
      scantable[i].delay[j] = new f64**[scantable[i].numphasecentres+1];
      scantable[i].wet[j] = new f64**[scantable[i].numphasecentres+1];
      scantable[i].dry[j] = new f64**[scantable[i].numphasecentres+1];
      for(int k=0;k<scantable[i].numphasecentres+1;k++) {
        scantable[i].u[j][k] = new f64*[numstations];
        scantable[i].v[j][k] = new f64*[numstations];
        scantable[i].w[j][k] = new f64*[numstations];
        scantable[i].delay[j][k] = new f64*[numstations];
        scantable[i].wet[j][k] = new f64*[numstations];
        scantable[i].dry[j][k] = new f64*[numstations];
        for(int l=0;l<numstations;l++) {
          estimatedbytes += 6*8*(polyorder + 1);
          scantable[i].u[j][k][l] = vectorAlloc_f64(polyorder+1);
          scantable[i].v[j][k][l] = vectorAlloc_f64(polyorder+1);
          scantable[i].w[j][k][l] = vectorAlloc_f64(polyorder+1);
          scantable[i].delay[j][k][l] = vectorAlloc_f64(polyorder+1);
          scantable[i].wet[j][k][l] = vectorAlloc_f64(polyorder+1);
          scantable[i].dry[j][k][l] = vectorAlloc_f64(polyorder+1);
          config->getinputline(input, &line, "SRC ", k);
          polyok = polyok && fillPolyRow(scantable[i].delay[j][k][l], line, polyorder+1);
          config->getinputline(input, &line, "SRC ", k);
          polyok = polyok && fillPolyRow(scantable[i].dry[j][k][l], line, polyorder+1);
          config->getinputline(input, &line, "SRC ", k);
          polyok = polyok && fillPolyRow(scantable[i].wet[j][k][l], line, polyorder+1);
          config->getinputline(input, &line, "SRC ", k);
          polyok = polyok && fillPolyRow(scantable[i].u[j][k][l], line, polyorder+1);
          config->getinputline(input, &line, "SRC ", k);
          polyok = polyok && fillPolyRow(scantable[i].v[j][k][l], line, polyorder+1);
          config->getinputline(input, &line, "SRC ", k);
          polyok = polyok && fillPolyRow(scantable[i].w[j][k][l], line, polyorder+1);
          if(!polyok) {
            cfatal << startl << "IM file has problem with polynomials - aborting!" << endl;
            return false;
          }
        }
      }
    }
  }
  return true;
}

//Split a whitespace-separated row and store the values in an array of doubles
bool Model::fillPolyRow(f64* vals, string line, int npoly)
{
  istringstream iss(line);
  string val;

  for(int i=0;i<npoly;i++) {
    if(!iss)
      return false;
    iss >> val;
    vals[i] = atof(val.c_str());
  }
  return true;
}

//utility routine which returns an integer which FITS expects based on the type of mount
Model::axistype Model::getMount(string mount)
{
  if(mount.compare("azel") == 0 || mount.compare("altz") == 0) //its an azel mount
    return ALTAZ;
  if(mount.compare("equa") == 0 || mount.compare("hadec") == 0) //equatorial mount
    return RADEC;
  if(mount.compare("orbi") == 0) //orbital mount
    return ORB;
  if((mount.substr(0,2)).compare("xy") == 0) //xy mount
    return XY;

  //otherwise unknown
  cerror << startl << "Warning - unknown mount type: Assuming Az-El" << endl;
  return ALTAZ;
}
