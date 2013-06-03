/*---------------------------------------------------------------------------------------------*
 * Copyright (C) 2012 Daniel Bolaños - www.bltek.com - Boulder Language Technologies           *
 *                                                                                             *
 * www.bavieca.org is the website of the Bavieca Speech Recognition Toolkit                    *
 *                                                                                             *
 * Licensed under the Apache License, Version 2.0 (the "License");                             *
 * you may not use this file except in compliance with the License.                            *
 * You may obtain a copy of the License at                                                     *
 *                                                                                             *
 *         http://www.apache.org/licenses/LICENSE-2.0                                          *
 *                                                                                             *
 * Unless required by applicable law or agreed to in writing, software                         *
 * distributed under the License is distributed on an "AS IS" BASIS,                           *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.                    *
 * See the License for the specific language governing permissions and                         *
 * limitations under the License.                                                              *
 *---------------------------------------------------------------------------------------------*/


#include "FileInput.h"
#include "FileOutput.h"
#include "HMMStateDecoding.h"
#include "IOBase.h"
#include "PhoneSet.h"

#include <limits.h>

namespace Bavieca {

// constructor
HMMStateDecoding::HMMStateDecoding(int iDim, PhoneSet *phoneSet, int iId)
{
	m_iTimestamp = -1;
	m_iDim = iDim;
	m_phoneSet = phoneSet;
	m_iId = iId;
	m_gaussians = NULL;
	m_bCovarianceOriginal = true;
}

// constructor
HMMStateDecoding::HMMStateDecoding(int iDim, PhoneSet *phoneSet, unsigned char iPhone, unsigned char iState, 
	unsigned char iPosition, int iId, int iGaussians, GaussianDecoding *gaussians) {
	
	m_iDim = iDim;
	m_phoneSet = phoneSet;
	m_iPhone = iPhone;
	m_iState = iState;
	m_iPosition = iPosition;
	m_iId = iId;
	m_iGaussianComponents = iGaussians;
	m_gaussians = gaussians;
	m_bCovarianceOriginal = true;	
}


// default constructor
HMMStateDecoding::HMMStateDecoding()
{
	m_iTimestamp = -1;
	m_phoneSet = NULL;
	m_iId = -1;
	m_gaussians = NULL;
}

// set initial parameters
void HMMStateDecoding::setInitialParameters(int iDim, PhoneSet *phoneSet, int iId) {
	
	m_iDim = iDim;
	m_iTimestamp = -1;
	m_phoneSet = phoneSet;
	m_iId = iId;
	m_gaussians = NULL;
}

// destructor
HMMStateDecoding::~HMMStateDecoding()
{
	// free memory
#if defined __AVX__ || defined __SSE__
	free(m_gaussians);
#else
	if (m_gaussians)
		delete [] m_gaussians;
#endif		
}

// initialize the estimation by precomputing constants and invariant terms
void HMMStateDecoding::initialize() {

	m_fConstant = powf(((float)PI_NUMBER)*2.0f,((float)DIMENSIONALITY)/2.0f);

	#ifdef OPTIMIZED_COMPUTATION
	// compute the covariance determinantof every gaussian
	for(int iGaussian = 0 ; iGaussian < m_iGaussianComponents ; ++iGaussian) {
		double dDeterminant = 1.0;
		for(int i = 0 ; i < DIMENSIONALITY ; ++i) {
			// compute the determinant of the covariance matrix
			dDeterminant *= m_gaussians[iGaussian].fCovariance[i];
			assert(m_gaussians[iGaussian].fCovariance[i] != 0);
		}
		m_gaussians[iGaussian].fConstant = (float)log(m_gaussians[iGaussian].fWeight/(m_fConstant*sqrt(dDeterminant)));
		assert(finite(m_gaussians[iGaussian].fConstant));
		// invert covariance and divide it by two
		for(int i = 0 ; i < DIMENSIONALITY ; ++i) {
			m_gaussians[iGaussian].fCovariance[i] = (float)(1.0/(2.0*m_gaussians[iGaussian].fCovariance[i]));
		}
	}
	m_bCovarianceOriginal = false;
	#endif
	
	//printf("%x %d %d\n",this,m_iId,m_iGaussianComponents);
}

// store the HMM into a file
void HMMStateDecoding::store(FileOutput &file) {
	
	// phonetic symbol
	char strPhone[MAX_PHONETIC_SYMBOL_LENGTH+1];
	strcpy(strPhone,m_phoneSet->getStrPhone(m_iPhone));	
	IOBase::writeBytes(file.getStream(),reinterpret_cast<char*>(strPhone),MAX_PHONETIC_SYMBOL_LENGTH+1);
	
	// state
	IOBase::write(file.getStream(),m_iState);
	
	// within word position (DEPRECATED)
	IOBase::write(file.getStream(),m_iPosition);		
	
	// Gaussian components
	IOBase::write(file.getStream(),m_iGaussianComponents);
	for(int iGaussian = 0 ; iGaussian < m_iGaussianComponents ; ++iGaussian) {
		IOBase::write(file.getStream(),m_gaussians[iGaussian].fWeight);
		IOBase::writeBytes(file.getStream(),reinterpret_cast<char*>(m_gaussians[iGaussian].fMean),m_iDim*sizeof(float));
		for(int i = 0 ; i < DIMENSIONALITY ; ++i) {
			float f = m_gaussians[iGaussian].fCovariance[i];
		#ifdef OPTIMIZED_COMPUTATION
			f = (float)(1.0/(2.0*f));
		#endif
			IOBase::write(file.getStream(),f);
		}
	}
}

// load the HMM from a file (binary format)
void HMMStateDecoding::load(FileInput &file) {

	// phonetic symbol
	char strPhone[MAX_PHONETIC_SYMBOL_LENGTH+1];	
	IOBase::readBytes(file.getStream(),reinterpret_cast<char*>(strPhone),MAX_PHONETIC_SYMBOL_LENGTH+1);
	m_iPhone = m_phoneSet->getPhoneIndex(strPhone);
	assert(m_iPhone != UCHAR_MAX);
	
	// state
	IOBase::read(file.getStream(),&m_iState);
	assert(m_iState < NUMBER_HMM_STATES);
	
	// within word position (DEPRECATED)
	IOBase::read(file.getStream(),&m_iPosition);
	
	// Gaussian components
	IOBase::read(file.getStream(),&m_iGaussianComponents);
	assert(m_iGaussianComponents > 0);
	
	// allocate memory
#if defined __AVX__ || defined __SSE__
	if (posix_memalign((void**)&m_gaussians,ALIGN_BOUNDARY,m_iGaussianComponents*sizeof(GaussianDecoding)) != 0) {
		BVC_ERROR << "memory allocation error, unable to allocate aligned memory using posix_memalign";
	}
#else
	m_gaussians = new GaussianDecoding[m_iGaussianComponents];
#endif

	// load
	for(int iGaussian = 0 ; iGaussian < m_iGaussianComponents ; ++iGaussian) {
	
		m_gaussians[iGaussian].iId = -1;
		IOBase::read(file.getStream(),&m_gaussians[iGaussian].fWeight);
		IOBase::readBytes(file.getStream(),reinterpret_cast<char*>(m_gaussians[iGaussian].fMean),m_iDim*sizeof(float));
		IOBase::readBytes(file.getStream(),reinterpret_cast<char*>(m_gaussians[iGaussian].fCovariance),
			m_iDim*sizeof(float));
	
		//m_gaussians[iGaussian].iBaseClass = -1;
		//m_gaussians[iGaussian].accumulator = NULL;	
	}
	
	m_bCovarianceOriginal = true;
}

// computes the emission probability of the state given the feature vector ("brute force")
// each of the gaussians is a multivariate normal distribution
float HMMStateDecoding::computeEmissionProbabilityBruteForce(float *fFeatures, int iTime) {

	if (iTime == m_iTimestamp) {	
		return m_fProbabilityCached;
	}
	
	double fAcc,fDeterminant;
	float fProbability = 0.0;
	float fExponent;
	
	float fConstant = (float)(powf((float)(PI_NUMBER)*2.0f,((float)DIMENSIONALITY)/2.0));
	
	// accumulate emission probabilities from each gaussian
	for(int i = 0 ; i < m_iGaussianComponents ; ++i) {	
	
		fDeterminant = 1.0;
		fExponent = 0.0;
		
		for(int j = 0 ; j < DIMENSIONALITY ; ++j) {
			// compute the determinant of the covariance matrix
			fDeterminant *= m_gaussians[i].fCovariance[j];
			// compute the numerator
			fAcc = fFeatures[j]-m_gaussians[i].fMean[j];
			fExponent += (float)(fAcc*fAcc*(1.0/m_gaussians[i].fCovariance[j]));
		}		
		fDeterminant = pow(fDeterminant,0.5);
		fProbability += (float)(m_gaussians[i].fWeight*exp(-0.5f*fExponent)/(fConstant*fDeterminant));
	}	
	
	fProbability = (float)std::max<double>((double)LOG_LIKELIHOOD_FLOOR,log(fProbability));	
	if (finite(fProbability) == 0) {
		fProbability = LOG_LIKELIHOOD_FLOOR;
	}
	
	// cache the probability
	m_iTimestamp = iTime;
	m_fProbabilityCached = fProbability;
	
	return fProbability;
}


// computes the emission probability of the state given the feature vector 
// each of the gaussians is a multivariate normal distribution
// uses nearest-neighbor approximation
float HMMStateDecoding::computeEmissionProbabilityNearestNeighbor(float *fFeatures, int iTime) {

	if (iTime == m_iTimestamp) {	
		return m_fProbabilityCached;
	}
	
	float *fMean,*fCovariance,fAcc;
	float fLogLikelihood = LOG_LIKELIHOOD_FLOOR;
	
	for(int iGaussian = 0 ; iGaussian < m_iGaussianComponents ; ++iGaussian) {
	
		fMean = m_gaussians[iGaussian].fMean;
		fCovariance = m_gaussians[iGaussian].fCovariance;	
		fAcc = m_gaussians[iGaussian].fConstant;
	
		/*for(int i=0 ; i<39 ; ++i) {
			fAcc -= (fFeatures[i]-fMean[i])*(fFeatures[i]-fMean[i])*fCovariance[i];
		}*/
      fAcc -= (fFeatures[0]-fMean[0])*(fFeatures[0]-fMean[0])*fCovariance[0];
      fAcc -= (fFeatures[1]-fMean[1])*(fFeatures[1]-fMean[1])*fCovariance[1];
      fAcc -= (fFeatures[2]-fMean[2])*(fFeatures[2]-fMean[2])*fCovariance[2];
      fAcc -= (fFeatures[3]-fMean[3])*(fFeatures[3]-fMean[3])*fCovariance[3];
      fAcc -= (fFeatures[4]-fMean[4])*(fFeatures[4]-fMean[4])*fCovariance[4];
      fAcc -= (fFeatures[5]-fMean[5])*(fFeatures[5]-fMean[5])*fCovariance[5];
      fAcc -= (fFeatures[6]-fMean[6])*(fFeatures[6]-fMean[6])*fCovariance[6];
      fAcc -= (fFeatures[7]-fMean[7])*(fFeatures[7]-fMean[7])*fCovariance[7];
      fAcc -= (fFeatures[8]-fMean[8])*(fFeatures[8]-fMean[8])*fCovariance[8];
      fAcc -= (fFeatures[9]-fMean[9])*(fFeatures[9]-fMean[9])*fCovariance[9];
      fAcc -= (fFeatures[10]-fMean[10])*(fFeatures[10]-fMean[10])*fCovariance[10];
      fAcc -= (fFeatures[11]-fMean[11])*(fFeatures[11]-fMean[11])*fCovariance[11];
      fAcc -= (fFeatures[12]-fMean[12])*(fFeatures[12]-fMean[12])*fCovariance[12];
      
      fAcc -= (fFeatures[13]-fMean[13])*(fFeatures[13]-fMean[13])*fCovariance[13];
      fAcc -= (fFeatures[14]-fMean[14])*(fFeatures[14]-fMean[14])*fCovariance[14];
      fAcc -= (fFeatures[15]-fMean[15])*(fFeatures[15]-fMean[15])*fCovariance[15];
      fAcc -= (fFeatures[16]-fMean[16])*(fFeatures[16]-fMean[16])*fCovariance[16];
      fAcc -= (fFeatures[17]-fMean[17])*(fFeatures[17]-fMean[17])*fCovariance[17];
      fAcc -= (fFeatures[18]-fMean[18])*(fFeatures[18]-fMean[18])*fCovariance[18];
      fAcc -= (fFeatures[19]-fMean[19])*(fFeatures[19]-fMean[19])*fCovariance[19];
      fAcc -= (fFeatures[20]-fMean[20])*(fFeatures[20]-fMean[20])*fCovariance[20];
      fAcc -= (fFeatures[21]-fMean[21])*(fFeatures[21]-fMean[21])*fCovariance[21];
      fAcc -= (fFeatures[22]-fMean[22])*(fFeatures[22]-fMean[22])*fCovariance[22];
      fAcc -= (fFeatures[23]-fMean[23])*(fFeatures[23]-fMean[23])*fCovariance[23];
      fAcc -= (fFeatures[24]-fMean[24])*(fFeatures[24]-fMean[24])*fCovariance[24];
      fAcc -= (fFeatures[25]-fMean[25])*(fFeatures[25]-fMean[25])*fCovariance[25];
      
      fAcc -= (fFeatures[26]-fMean[26])*(fFeatures[26]-fMean[26])*fCovariance[26];
      fAcc -= (fFeatures[27]-fMean[27])*(fFeatures[27]-fMean[27])*fCovariance[27];
      fAcc -= (fFeatures[28]-fMean[28])*(fFeatures[28]-fMean[28])*fCovariance[28];
      fAcc -= (fFeatures[29]-fMean[29])*(fFeatures[29]-fMean[29])*fCovariance[29];
      fAcc -= (fFeatures[30]-fMean[30])*(fFeatures[30]-fMean[30])*fCovariance[30];
      fAcc -= (fFeatures[31]-fMean[31])*(fFeatures[31]-fMean[31])*fCovariance[31];
      fAcc -= (fFeatures[32]-fMean[32])*(fFeatures[32]-fMean[32])*fCovariance[32];
      fAcc -= (fFeatures[33]-fMean[33])*(fFeatures[33]-fMean[33])*fCovariance[33];
      fAcc -= (fFeatures[34]-fMean[34])*(fFeatures[34]-fMean[34])*fCovariance[34];
      fAcc -= (fFeatures[35]-fMean[35])*(fFeatures[35]-fMean[35])*fCovariance[35];
      fAcc -= (fFeatures[36]-fMean[36])*(fFeatures[36]-fMean[36])*fCovariance[36];
      fAcc -= (fFeatures[37]-fMean[37])*(fFeatures[37]-fMean[37])*fCovariance[37];
      fAcc -= (fFeatures[38]-fMean[38])*(fFeatures[38]-fMean[38])*fCovariance[38];
 		
		fLogLikelihood = max(fAcc,fLogLikelihood);
	}
	
	assert(finite(fLogLikelihood) != 0);

	// cache the probability
	m_iTimestamp = iTime;
	m_fProbabilityCached = fLogLikelihood;
	
	return fLogLikelihood;
}

// computes the emission probability of the state given the feature vector 
// each of the gaussians is a multivariate normal distribution
// uses nearest-neighbor approximation
// uses Partial Distance Elimination (PDE)
float HMMStateDecoding::computeEmissionProbabilityNearestNeighborPDE(float *fFeatures, int iTime) {

	if (iTime == m_iTimestamp) {	
		return m_fProbabilityCached;
	}
	
	float *fMean,*fCovariance,fAcc;
	float fLogLikelihood = LOG_LIKELIHOOD_FLOOR;
	
	for(int iGaussian = 0 ; iGaussian < m_iGaussianComponents ; ++iGaussian) {
	
		fMean = m_gaussians[iGaussian].fMean;
		fCovariance = m_gaussians[iGaussian].fCovariance;	
		fAcc = m_gaussians[iGaussian].fConstant;
		
      fAcc -= (fFeatures[0]-fMean[0])*(fFeatures[0]-fMean[0])*fCovariance[0];
      fAcc -= (fFeatures[1]-fMean[1])*(fFeatures[1]-fMean[1])*fCovariance[1];
      fAcc -= (fFeatures[2]-fMean[2])*(fFeatures[2]-fMean[2])*fCovariance[2];
      fAcc -= (fFeatures[3]-fMean[3])*(fFeatures[3]-fMean[3])*fCovariance[3];
      fAcc -= (fFeatures[4]-fMean[4])*(fFeatures[4]-fMean[4])*fCovariance[4];
      fAcc -= (fFeatures[5]-fMean[5])*(fFeatures[5]-fMean[5])*fCovariance[5];
      fAcc -= (fFeatures[6]-fMean[6])*(fFeatures[6]-fMean[6])*fCovariance[6];
      fAcc -= (fFeatures[7]-fMean[7])*(fFeatures[7]-fMean[7])*fCovariance[7];
      fAcc -= (fFeatures[8]-fMean[8])*(fFeatures[8]-fMean[8])*fCovariance[8];
      fAcc -= (fFeatures[9]-fMean[9])*(fFeatures[9]-fMean[9])*fCovariance[9];
      fAcc -= (fFeatures[10]-fMean[10])*(fFeatures[10]-fMean[10])*fCovariance[10];
      fAcc -= (fFeatures[11]-fMean[11])*(fFeatures[11]-fMean[11])*fCovariance[11];
		fAcc -= (fFeatures[12]-fMean[12])*(fFeatures[12]-fMean[12])*fCovariance[12];
      
      if (fAcc > fLogLikelihood) {

			fAcc -= (fFeatures[13]-fMean[13])*(fFeatures[13]-fMean[13])*fCovariance[13];
			fAcc -= (fFeatures[14]-fMean[14])*(fFeatures[14]-fMean[14])*fCovariance[14];
			fAcc -= (fFeatures[15]-fMean[15])*(fFeatures[15]-fMean[15])*fCovariance[15];
			fAcc -= (fFeatures[16]-fMean[16])*(fFeatures[16]-fMean[16])*fCovariance[16];
			fAcc -= (fFeatures[17]-fMean[17])*(fFeatures[17]-fMean[17])*fCovariance[17];
			fAcc -= (fFeatures[18]-fMean[18])*(fFeatures[18]-fMean[18])*fCovariance[18];
			fAcc -= (fFeatures[19]-fMean[19])*(fFeatures[19]-fMean[19])*fCovariance[19];
			fAcc -= (fFeatures[20]-fMean[20])*(fFeatures[20]-fMean[20])*fCovariance[20];
			fAcc -= (fFeatures[21]-fMean[21])*(fFeatures[21]-fMean[21])*fCovariance[21];
			fAcc -= (fFeatures[22]-fMean[22])*(fFeatures[22]-fMean[22])*fCovariance[22];
			fAcc -= (fFeatures[23]-fMean[23])*(fFeatures[23]-fMean[23])*fCovariance[23];
			fAcc -= (fFeatures[24]-fMean[24])*(fFeatures[24]-fMean[24])*fCovariance[24];
			fAcc -= (fFeatures[25]-fMean[25])*(fFeatures[25]-fMean[25])*fCovariance[25];
			
	      if (fAcc > fLogLikelihood) {

				fAcc -= (fFeatures[26]-fMean[26])*(fFeatures[26]-fMean[26])*fCovariance[26];
				fAcc -= (fFeatures[27]-fMean[27])*(fFeatures[27]-fMean[27])*fCovariance[27];
				fAcc -= (fFeatures[28]-fMean[28])*(fFeatures[28]-fMean[28])*fCovariance[28];
				fAcc -= (fFeatures[29]-fMean[29])*(fFeatures[29]-fMean[29])*fCovariance[29];
				fAcc -= (fFeatures[30]-fMean[30])*(fFeatures[30]-fMean[30])*fCovariance[30];
				fAcc -= (fFeatures[31]-fMean[31])*(fFeatures[31]-fMean[31])*fCovariance[31];
				fAcc -= (fFeatures[32]-fMean[32])*(fFeatures[32]-fMean[32])*fCovariance[32];
				fAcc -= (fFeatures[33]-fMean[33])*(fFeatures[33]-fMean[33])*fCovariance[33];
				fAcc -= (fFeatures[34]-fMean[34])*(fFeatures[34]-fMean[34])*fCovariance[34];
				fAcc -= (fFeatures[35]-fMean[35])*(fFeatures[35]-fMean[35])*fCovariance[35];
		      fAcc -= (fFeatures[36]-fMean[36])*(fFeatures[36]-fMean[36])*fCovariance[36];
				fAcc -= (fFeatures[37]-fMean[37])*(fFeatures[37]-fMean[37])*fCovariance[37];
				fAcc -= (fFeatures[38]-fMean[38])*(fFeatures[38]-fMean[38])*fCovariance[38];
				
				fLogLikelihood = max(fAcc,fLogLikelihood);	
	      }
		}	
	}

	// cache the probability
	m_iTimestamp = iTime;
	m_fProbabilityCached = fLogLikelihood;
	
	return fLogLikelihood;
}

// computes the emission probability of the state given the feature vector 
// uses nearest-neighbor approximation
// uses SIMD instructions (SSE) (sse support must be enabled during compilation!)
#ifdef __SSE__
float HMMStateDecoding::computeEmissionProbabilityNearestNeighborSSE(float *fFeatures, int iTime) {

	if (iTime == m_iTimestamp) {	
		return m_fProbabilityCached;
	}
	
	// check memory alignment
	assert(is_aligned(fFeatures,ALIGN_BOUNDARY));	

#if defined __linux__ || defined __APPLE__ || __MINGW32__
	__attribute__((aligned(16))) float *fMean,*fCovariance,fAcc;
	__attribute__((aligned(16))) float fLogLikelihood = LOG_LIKELIHOOD_FLOOR;	
	__attribute__((aligned(16))) float tmpf[4];
#elif _MSC_VER
	__declspec(align(16)) float *fMean,*fCovariance,fAcc;
	__declspec(align(16)) float fLogLikelihood = LOG_LIKELIHOOD_FLOOR;	
	__declspec(align(16)) float tmpf[4];
#endif

	__m128 tmp;
	__m128 ans;
	__m128 obs;
	__m128 mean;
	__m128 cov;	

	for(int iGaussian = 0 ; iGaussian < m_iGaussianComponents ; ++iGaussian) {
	
		fMean = m_gaussians[iGaussian].fMean;
		fCovariance = m_gaussians[iGaussian].fCovariance;	
		fAcc = m_gaussians[iGaussian].fConstant;
		
		obs  = _mm_load_ps(fFeatures);
		mean = _mm_load_ps(fMean);
		cov  = _mm_load_ps(fCovariance);
		tmp  = _mm_sub_ps(obs, mean);
		tmp  = _mm_mul_ps(tmp,tmp);
		ans  = _mm_mul_ps(tmp,cov);

		obs  = _mm_load_ps(fFeatures+4);
		mean = _mm_load_ps(fMean+4);
		cov  = _mm_load_ps(fCovariance+4);
		tmp  = _mm_sub_ps(obs, mean);
		tmp  = _mm_mul_ps(tmp,tmp);
		tmp  = _mm_mul_ps(tmp,cov);
		ans  = _mm_add_ps(ans, tmp);

		obs  = _mm_load_ps(fFeatures+8);
		mean = _mm_load_ps(fMean+8);
		cov  = _mm_load_ps(fCovariance+8);
		tmp  = _mm_sub_ps(obs, mean);
		tmp  = _mm_mul_ps(tmp,tmp);
		tmp  = _mm_mul_ps(tmp,cov);
		ans  = _mm_add_ps(ans, tmp);

		obs  = _mm_load_ps(fFeatures+12);
		mean = _mm_load_ps(fMean+12);
		cov  = _mm_load_ps(fCovariance+12);
		tmp  = _mm_sub_ps(obs, mean);
		tmp  = _mm_mul_ps(tmp,tmp);
		tmp  = _mm_mul_ps(tmp,cov);
		ans  = _mm_add_ps(ans, tmp);

		obs  = _mm_load_ps(fFeatures+16);
		mean = _mm_load_ps(fMean+16);
		cov  = _mm_load_ps(fCovariance+16);
		tmp  = _mm_sub_ps(obs, mean);
		tmp  = _mm_mul_ps(tmp,tmp);
		tmp  = _mm_mul_ps(tmp,cov);
		ans  = _mm_add_ps(ans, tmp);

		obs  = _mm_load_ps(fFeatures+20);
		mean = _mm_load_ps(fMean+20);
		cov  = _mm_load_ps(fCovariance+20);
		tmp  = _mm_sub_ps(obs, mean);
		tmp  = _mm_mul_ps(tmp,tmp);
		tmp  = _mm_mul_ps(tmp,cov);
		ans  = _mm_add_ps(ans, tmp);

		obs  = _mm_load_ps(fFeatures+24);
		mean = _mm_load_ps(fMean+24);
		cov  = _mm_load_ps(fCovariance+24);
		tmp  = _mm_sub_ps(obs, mean);
		tmp  = _mm_mul_ps(tmp,tmp);
		tmp  = _mm_mul_ps(tmp,cov);
		ans  = _mm_add_ps(ans, tmp);

		obs  = _mm_load_ps(fFeatures+28);
		mean = _mm_load_ps(fMean+28);
		cov  = _mm_load_ps(fCovariance+28);
		tmp  = _mm_sub_ps(obs, mean);
		tmp  = _mm_mul_ps(tmp,tmp);
		tmp  = _mm_mul_ps(tmp,cov);
		ans  = _mm_add_ps(ans, tmp);
 
		obs  = _mm_load_ps(fFeatures+32);
		mean = _mm_load_ps(fMean+32);
		cov  = _mm_load_ps(fCovariance+32);
		tmp  = _mm_sub_ps(obs, mean);
		tmp  = _mm_mul_ps(tmp,tmp);
		tmp  = _mm_mul_ps(tmp,cov);
		ans  = _mm_add_ps(ans, tmp);

		obs  = _mm_set_ps(0, fFeatures[38], fFeatures[37], fFeatures[36]);
		mean = _mm_set_ps(0, fMean[38], fMean[37], fMean[36]);
		cov  = _mm_set_ps(0, fCovariance[38], fCovariance[37], fCovariance[36]);
		tmp  = _mm_sub_ps(obs, mean);
		tmp  = _mm_mul_ps(tmp,tmp);
		tmp  = _mm_mul_ps(tmp,cov);
		ans  = _mm_add_ps(ans, tmp);
		
		/*__attribute__((aligned(16))) float f = 0.0;
		ans = _mm_dp_ps(tmp,cov128,0xF1);		
		_mm_store_ss(&f,ans);	
		fAcc -= f;*/

		_mm_store_ps(tmpf, ans);
		fAcc -= (tmpf[0]+tmpf[1]+tmpf[2]+tmpf[3]);

		fLogLikelihood = max(fAcc,fLogLikelihood);
	}

	// cache the probability
	m_iTimestamp = iTime;
	m_fProbabilityCached = fLogLikelihood;
	
	return fLogLikelihood;
}
#endif	




// computes the emission probability of the state given the feature vector 
// uses nearest-neighbor approximation
// uses Partial Distance Elimination (PDE)
// uses SIMD instructions (SSE) (sse support must be enabled during compilation!)
/*float HMMStateDecoding::computeEmissionProbabilityNearestNeighborPDE_SIMD(float *fFeatures, int iTime) {

	if (iTime == m_iTimestamp) {	
		//return m_fProbabilityCached;
	}
	
#if defined __linux__ || defined __APPLE__ || __MINGW32__
	__attribute__((aligned(16))) float *fMean,*fCovariance,fAcc;
	__attribute__((aligned(16))) float fLogLikelihood = LOG_LIKELIHOOD_FLOOR;	
	__attribute__((aligned(16))) float tmpf[4];
#elif _MSC_VER
	__declspec(align(16)) float *fMean,*fCovariance,fAcc;
	__declspec(align(16)) float fLogLikelihood = LOG_LIKELIHOOD_FLOOR;	
	__declspec(align(16)) float tmpf[4];
#endif 


  __m128 tmp;
  __m128 ans;
  __m128 obs128;
  __m128 mean128;
  __m128 cov128;	
  __m128 likelihood;
  
  likelihood = _mm_load_ss(&fLogLikelihood);
  
	for(int iGaussian = 0 ; iGaussian < m_iGaussianComponents ; ++iGaussian) {
	
		fMean = m_gaussians[iGaussian].fMean;
		fCovariance = m_gaussians[iGaussian].fCovariance;	
		fAcc = m_gaussians[iGaussian].fConstant;
		
      obs128  = _mm_load_ps(fFeatures);
      mean128 = _mm_load_ps(fMean);
      cov128  = _mm_load_ps(fCovariance);
      tmp     = _mm_sub_ps(obs128, mean128);
      tmp     = _mm_mul_ps(tmp,tmp);
      //ans     = _mm_mul_ps(tmp,cov128);
      ans     = _mm_dp_ps(tmp,cov128,0xF1);
     
      obs128  = _mm_load_ps(fFeatures+4);
      mean128 = _mm_load_ps(fMean+4);
      cov128  = _mm_load_ps(fCovariance+4);
      tmp     = _mm_sub_ps(obs128, mean128);
      tmp     = _mm_mul_ps(tmp,tmp);
      //tmp     = _mm_mul_ps(tmp,cov128);
      tmp     = _mm_dp_ps(tmp,cov128,0xF1);
      ans     = _mm_add_ps(ans, tmp);
      
      obs128  = _mm_load_ps(fFeatures+8);
      mean128 = _mm_load_ps(fMean+8);
      cov128  = _mm_load_ps(fCovariance+8);
      tmp     = _mm_sub_ps(obs128, mean128);
      tmp     = _mm_mul_ps(tmp,tmp);
      //tmp     = _mm_mul_ps(tmp,cov128);
      tmp     = _mm_dp_ps(tmp,cov128,0xF1);
      ans     = _mm_add_ps(ans, tmp);
      
      //if (_mm_cmpgt_ss(ans,likelihood) > 0.0) {
      //if (likelihood) {
      
		_mm_store_ps(tmpf, ans);
		fAcc -= (tmpf[0]+tmpf[1]+tmpf[2]+tmpf[3]);
		if (fAcc > fLogLikelihood) {
      
			obs128  = _mm_load_ps(fFeatures+12);
			mean128 = _mm_load_ps(fMean+12);
			cov128  = _mm_load_ps(fCovariance+12);
			tmp     = _mm_sub_ps(obs128, mean128);
			tmp     = _mm_mul_ps(tmp,tmp);
			ans     = _mm_mul_ps(tmp,cov128);
			
			obs128  = _mm_load_ps(fFeatures+16);
			mean128 = _mm_load_ps(fMean+16);
			cov128  = _mm_load_ps(fCovariance+16);
			tmp     = _mm_sub_ps(obs128, mean128);
			tmp     = _mm_mul_ps(tmp,tmp);
			tmp     = _mm_mul_ps(tmp,cov128);
			ans     = _mm_add_ps(ans, tmp);
			
			obs128  = _mm_load_ps(fFeatures+20);
			mean128 = _mm_load_ps(fMean+20);
			cov128  = _mm_load_ps(fCovariance+20);
			tmp     = _mm_sub_ps(obs128, mean128);
			tmp     = _mm_mul_ps(tmp,tmp);
			tmp     = _mm_mul_ps(tmp,cov128);
			ans     = _mm_add_ps(ans, tmp);
			
			_mm_store_ps(tmpf, ans);
			fAcc -= (tmpf[0]+tmpf[1]+tmpf[2]+tmpf[3]);
			if (fAcc > fLogLikelihood) {
			
				obs128  = _mm_load_ps(fFeatures+24);
				mean128 = _mm_load_ps(fMean+24);
				cov128  = _mm_load_ps(fCovariance+24);
				tmp     = _mm_sub_ps(obs128, mean128);
				tmp     = _mm_mul_ps(tmp,tmp);
				ans     = _mm_mul_ps(tmp,cov128);
				
				obs128  = _mm_load_ps(fFeatures+28);
				mean128 = _mm_load_ps(fMean+28);
				cov128  = _mm_load_ps(fCovariance+28);
				tmp     = _mm_sub_ps(obs128, mean128);
				tmp     = _mm_mul_ps(tmp,tmp);
				tmp     = _mm_mul_ps(tmp,cov128);
				ans     = _mm_add_ps(ans, tmp);
				
				obs128  = _mm_load_ps(fFeatures+32);
				mean128 = _mm_load_ps(fMean+32);
				cov128  = _mm_load_ps(fCovariance+32);
				tmp     = _mm_sub_ps(obs128, mean128);
				tmp     = _mm_mul_ps(tmp,tmp);
				tmp     = _mm_mul_ps(tmp,cov128);
				ans     = _mm_add_ps(ans, tmp);
				
				obs128  = _mm_set_ps(0, fFeatures[38], fFeatures[37], fFeatures[36]);
				mean128 = _mm_set_ps(0, fMean[38], fMean[37], fMean[36]);
				cov128  = _mm_set_ps(0, fCovariance[38], fCovariance[37], fCovariance[36]);
				tmp     = _mm_sub_ps(obs128, mean128);
				tmp     = _mm_mul_ps(tmp,tmp);
				tmp     = _mm_mul_ps(tmp,cov128);
				ans     = _mm_add_ps(ans, tmp);	
		
				_mm_store_ps(tmpf, ans);
				fAcc -= (tmpf[0]+tmpf[1]+tmpf[2]+tmpf[3]);
				
				fLogLikelihood = max(fAcc,fLogLikelihood);
			}
		}
	}

	// cache the probability
	m_iTimestamp = iTime;
	m_fProbabilityCached = fLogLikelihood;
	
	return fLogLikelihood;
}*/

// return the best scoring gaussian for a given feature vector
// uses nearest-neighbor approximation
// uses Partial Distance Elimination (PDE)
GaussianDecoding *HMMStateDecoding::getBestScoringGaussian(float *fFeatures, float *fScore) {

	float *fMean,*fCovariance,fAcc;
	float fLogLikelihood = LOG_LIKELIHOOD_FLOOR;
	GaussianDecoding *gaussianBest = NULL;
	
	for(int iGaussian = 0 ; iGaussian < m_iGaussianComponents ; ++iGaussian) {
	
		fMean = m_gaussians[iGaussian].fMean;
		fCovariance = m_gaussians[iGaussian].fCovariance;
		fAcc = m_gaussians[iGaussian].fConstant;
	
		fAcc -= (fFeatures[0]-fMean[0])*(fFeatures[0]-fMean[0])*fCovariance[0];
		fAcc -= (fFeatures[1]-fMean[1])*(fFeatures[1]-fMean[1])*fCovariance[1];
		fAcc -= (fFeatures[2]-fMean[2])*(fFeatures[2]-fMean[2])*fCovariance[2];
		fAcc -= (fFeatures[3]-fMean[3])*(fFeatures[3]-fMean[3])*fCovariance[3];
		fAcc -= (fFeatures[4]-fMean[4])*(fFeatures[4]-fMean[4])*fCovariance[4];
		fAcc -= (fFeatures[5]-fMean[5])*(fFeatures[5]-fMean[5])*fCovariance[5];
		fAcc -= (fFeatures[6]-fMean[6])*(fFeatures[6]-fMean[6])*fCovariance[6];
		fAcc -= (fFeatures[7]-fMean[7])*(fFeatures[7]-fMean[7])*fCovariance[7];
		fAcc -= (fFeatures[8]-fMean[8])*(fFeatures[8]-fMean[8])*fCovariance[8];
		fAcc -= (fFeatures[9]-fMean[9])*(fFeatures[9]-fMean[9])*fCovariance[9];
		fAcc -= (fFeatures[10]-fMean[10])*(fFeatures[10]-fMean[10])*fCovariance[10];
		fAcc -= (fFeatures[11]-fMean[11])*(fFeatures[11]-fMean[11])*fCovariance[11];
		fAcc -= (fFeatures[12]-fMean[12])*(fFeatures[12]-fMean[12])*fCovariance[12];

		if (fAcc > fLogLikelihood) {

			fAcc -= (fFeatures[13]-fMean[13])*(fFeatures[13]-fMean[13])*fCovariance[13];
			fAcc -= (fFeatures[14]-fMean[14])*(fFeatures[14]-fMean[14])*fCovariance[14];
			fAcc -= (fFeatures[15]-fMean[15])*(fFeatures[15]-fMean[15])*fCovariance[15];
			fAcc -= (fFeatures[16]-fMean[16])*(fFeatures[16]-fMean[16])*fCovariance[16];
			fAcc -= (fFeatures[17]-fMean[17])*(fFeatures[17]-fMean[17])*fCovariance[17];
			fAcc -= (fFeatures[18]-fMean[18])*(fFeatures[18]-fMean[18])*fCovariance[18];
			fAcc -= (fFeatures[19]-fMean[19])*(fFeatures[19]-fMean[19])*fCovariance[19];
			fAcc -= (fFeatures[20]-fMean[20])*(fFeatures[20]-fMean[20])*fCovariance[20];
			fAcc -= (fFeatures[21]-fMean[21])*(fFeatures[21]-fMean[21])*fCovariance[21];
			fAcc -= (fFeatures[22]-fMean[22])*(fFeatures[22]-fMean[22])*fCovariance[22];
			fAcc -= (fFeatures[23]-fMean[23])*(fFeatures[23]-fMean[23])*fCovariance[23];
			fAcc -= (fFeatures[24]-fMean[24])*(fFeatures[24]-fMean[24])*fCovariance[24];
			fAcc -= (fFeatures[25]-fMean[25])*(fFeatures[25]-fMean[25])*fCovariance[25];
			
			if (fAcc > fLogLikelihood) {

				fAcc -= (fFeatures[26]-fMean[26])*(fFeatures[26]-fMean[26])*fCovariance[26];
				fAcc -= (fFeatures[27]-fMean[27])*(fFeatures[27]-fMean[27])*fCovariance[27];
				fAcc -= (fFeatures[28]-fMean[28])*(fFeatures[28]-fMean[28])*fCovariance[28];
				fAcc -= (fFeatures[29]-fMean[29])*(fFeatures[29]-fMean[29])*fCovariance[29];
				fAcc -= (fFeatures[30]-fMean[30])*(fFeatures[30]-fMean[30])*fCovariance[30];
				fAcc -= (fFeatures[31]-fMean[31])*(fFeatures[31]-fMean[31])*fCovariance[31];
				fAcc -= (fFeatures[32]-fMean[32])*(fFeatures[32]-fMean[32])*fCovariance[32];
				fAcc -= (fFeatures[33]-fMean[33])*(fFeatures[33]-fMean[33])*fCovariance[33];
				fAcc -= (fFeatures[34]-fMean[34])*(fFeatures[34]-fMean[34])*fCovariance[34];
				fAcc -= (fFeatures[35]-fMean[35])*(fFeatures[35]-fMean[35])*fCovariance[35];
				fAcc -= (fFeatures[36]-fMean[36])*(fFeatures[36]-fMean[36])*fCovariance[36];
				fAcc -= (fFeatures[37]-fMean[37])*(fFeatures[37]-fMean[37])*fCovariance[37];
				fAcc -= (fFeatures[38]-fMean[38])*(fFeatures[38]-fMean[38])*fCovariance[38];
				
				fLogLikelihood = max(fAcc,fLogLikelihood);	
				if (fLogLikelihood == fAcc) {
					gaussianBest = &m_gaussians[iGaussian];
				}
	      }
		}	
	}

	assert((gaussianBest != NULL) || (fLogLikelihood == LOG_LIKELIHOOD_FLOOR));
	
	// if no Gaussian is good enough, return the first one
	if (fLogLikelihood == LOG_LIKELIHOOD_FLOOR) {
		gaussianBest = &m_gaussians[0];
	}	
	
	*fScore = fLogLikelihood;
	
	return gaussianBest;
}
	
	
// return the Gaussian likelihood for the given feature vector
// uses nearest-neighbor approximation
// uses Partial Distance Elimination (PDE)
double HMMStateDecoding::computeGaussianProbability(int iGaussian, float *fFeatures) {
	
	float *fMean = m_gaussians[iGaussian].fMean;
	float *fCovariance = m_gaussians[iGaussian].fCovariance;	
	double dAcc = m_gaussians[iGaussian].fConstant;
	
	for(int i = 0 ; i < m_iDim ; ++i) {
		dAcc -= (fFeatures[i]-fMean[i])*(fFeatures[i]-fMean[i])*fCovariance[i];
	}
	
	return dAcc;
}	

// computes the emission probability of the state given the feature vector 
// uses nearest-neighbor approximation
// uses AVX (Intel® Advanced Vector Extensions) instructions (avx support must be enabled during compilation!)
// compared to SSE, AVX offers 256 bit registers instead of 128 bit registers 
#ifdef __AVX__
float HMMStateDecoding::computeEmissionProbabilityNearestNeighborAVX(float *fFeatures, int iTime) {

	if (iTime == m_iTimestamp) {	
		return m_fProbabilityCached;
	}
	
	// check memory alignment
	assert(is_aligned(fFeatures,ALIGN_BOUNDARY));	

#if defined __linux__ || defined __APPLE__ || __MINGW32__
	__attribute__((aligned(32))) float *fMean,*fCov,fAcc;
	__attribute__((aligned(32))) float fLogLikelihood = LOG_LIKELIHOOD_FLOOR;	
	__attribute__((aligned(32))) float fTmp[8];
#elif _MSC_VER
	__declspec(align(32)) float *fMean,*fCov,fAcc;
	__declspec(align(32)) float fLogLikelihood = LOG_LIKELIHOOD_FLOOR;	
	__declspec(align(32)) float fTmp[8];
#endif

	__m256 tmp;
	__m256 ans;
	__m256 obs;
	__m256 mean;
	__m256 cov;	

	for(int iGaussian = 0 ; iGaussian < m_iGaussianComponents ; ++iGaussian) {
	
		fMean = m_gaussians[iGaussian].fMean;
		fCov = m_gaussians[iGaussian].fCovariance;	
		fAcc = m_gaussians[iGaussian].fConstant;

		obs  = _mm256_load_ps(fFeatures);
		mean = _mm256_load_ps(fMean);
		cov  = _mm256_load_ps(fCov);
		tmp  = _mm256_sub_ps(obs, mean);
		tmp  = _mm256_mul_ps(tmp,tmp);
		ans  = _mm256_mul_ps(tmp,cov);

		obs  = _mm256_load_ps(fFeatures+8);
		mean = _mm256_load_ps(fMean+8);
		cov  = _mm256_load_ps(fCov+8);
		tmp  = _mm256_sub_ps(obs, mean);
		tmp  = _mm256_mul_ps(tmp,tmp);
		tmp  = _mm256_mul_ps(tmp,cov);
		ans  = _mm256_add_ps(ans, tmp);

		obs  = _mm256_load_ps(fFeatures+16);
		mean = _mm256_load_ps(fMean+16);
		cov  = _mm256_load_ps(fCov+16);
		tmp  = _mm256_sub_ps(obs, mean);
		tmp  = _mm256_mul_ps(tmp,tmp);
		tmp  = _mm256_mul_ps(tmp,cov);
		ans  = _mm256_add_ps(ans, tmp);

		obs  = _mm256_load_ps(fFeatures+24);
		mean = _mm256_load_ps(fMean+24);
		cov  = _mm256_load_ps(fCov+24);
		tmp  = _mm256_sub_ps(obs, mean);
		tmp  = _mm256_mul_ps(tmp,tmp);
		tmp  = _mm256_mul_ps(tmp,cov);
		ans  = _mm256_add_ps(ans, tmp);
 
		obs  = _mm256_set_ps(0, fFeatures[38], fFeatures[37], fFeatures[36], fFeatures[35], fFeatures[34], fFeatures[33], fFeatures[32]);
		mean = _mm256_set_ps(0, fMean[38], fMean[37], fMean[36], fMean[35], fMean[34], fMean[33], fMean[32]);
		cov  = _mm256_set_ps(0, fCov[38], fCov[37], fCov[36], fCov[35], fCov[34], fCov[33], fCov[32]);
		tmp     = _mm256_sub_ps(obs, mean);
		tmp     = _mm256_mul_ps(tmp,tmp);
		tmp     = _mm256_mul_ps(tmp,cov);
		ans     = _mm256_add_ps(ans, tmp);
		
		/*__attribute__((aligned(16))) float f = 0.0;
		ans = _mm256_dp_ps(tmp,cov128,0xF1);		
		_mm_store_ss(&f,ans);	
		fAcc -= f;*/

		_mm256_store_ps(fTmp, ans);
		fAcc -= (fTmp[0]+fTmp[1]+fTmp[2]+fTmp[3]+fTmp[4]+fTmp[5]+fTmp[6]+fTmp[7]);

		fLogLikelihood = max(fAcc,fLogLikelihood);
	}

	// cache the probability
	m_iTimestamp = iTime;
	m_fProbabilityCached = fLogLikelihood;
	
	return fLogLikelihood;
}
#endif


};	// end-of-namespace
