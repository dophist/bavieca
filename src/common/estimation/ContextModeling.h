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


#ifndef CONTEXTMODELING_H
#define CONTEXTMODELING_H

#include "ContextDecisionTree.h"
#include "HMMManager.h"
#include "PhoneSet.h"
#include "PhoneticRulesManager.h"

namespace Bavieca {

// triphone clustering methods 
// (string format)
#define CLUSTERING_METHOD_LOCAL_STR			"local"
#define CLUSTERING_METHOD_GLOBAL_STR		"global"
// (numeric format)
#define CLUSTERING_METHOD_LOCAL		0
#define CLUSTERING_METHOD_GLOBAL		1

/**
	@author daniel <dani.bolanos@gmail.com>
*/
class ContextModeling {

	private: 
	
		int m_iDim;
		int m_iCovarianceModelling;
		PhoneSet *m_phoneSet;
		unsigned char m_iPhones;
		Vector<float> *m_vMeanGlobal;				// global mean
		Vector<float> *m_vCovarianceGlobal;		// global covariance (diagonal covariance is used for clustering)
	
		// triphone clustering configuration
		string m_strFilePhoneticRules;
		bool m_bGlobalClustering;
		bool m_bBottomUpMerging;	
		float m_fMinimumClusterOccupation;
		float m_fMinimumLikelihoodGainClustering;	
		int m_iContextModelingOrderCW;				// cross-word context modeling order
		int m_iContextModelingOrderWW;				// within-word context modeling order
		int m_iContextSizeCW;	
		int m_iContextSizeWW;	
			
		// clustered HMM-states
		HMMState **m_hmmStatesClustered;
		
	private:
	
		// store context decision trees to a file
		void storeContextDecisionTrees(const char *strFile, ContextDecisionTree **contextDecisionTrees, int ContextDecisionTrees);	

	public:
    
		// constructor
		ContextModeling(int iFeatureDimensionality, int iCovarianceModelling, PhoneSet *phoneSet, int iContextModelingOrderWW, int iContextModelingOrderCW, const char *strFilePhoneticRules, bool bGlobalClustering, bool bBottomUpMerging, float fMinimumClusterOccupation, float fMinimumLikelihoodGain);
		
		// destructor
		~ContextModeling();
		
		// tree-based n-phone clustering (top-down clustering using phonetic rules)
		// the following information is needed:
		// - estimation counts from all the observed units in the training data
		// - a set of phonetic rules
		bool clusterContextDependentUnits(MAccumulatorLogical &mAccumulators, HMMManager *hmmManager);
		
		// load context decision trees from a file
		static ContextDecisionTree **loadContextDecisionTrees(const char *strFile, int iFeatureDimensionality, int iCovarianceModelling, PhoneticRulesManager *phoneticRulesManager, int *iContextDecisionTrees, PhoneSet *phoneSet, unsigned char iContextModelingOrder, unsigned char iClusteringMethod);
		
		// compute the global distribution given the accumulators
		void computeGlobalDistribution(MAccumulatorLogical mAccumulators);

};

};	// end-of-namespace

#endif
