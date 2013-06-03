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

#include <stdexcept>

#include "CommandLineManager.h"
#include "FillerManager.h"
#include "HMMManager.h"
#include "LMManager.h"
#include "TimeUtils.h"
#include "WFSABuilder.h"

using namespace Bavieca;

// main for the tool "wfsbuilder"
int main(int argc, char *argv[]) {

	try {

		// (1) define command line parameters
		CommandLineManager commandLineManager("wfsabuilder",SYSTEM_VERSION,SYSTEM_AUTHOR,SYSTEM_DATE);
		commandLineManager.defineParameter("-pho","phonetic symbol set",PARAMETER_TYPE_FILE,false);
		commandLineManager.defineParameter("-mod","acoustic models",PARAMETER_TYPE_FILE,false);
		commandLineManager.defineParameter("-lex","pronunciation dictionary (lexicon)",PARAMETER_TYPE_FILE,false);
		commandLineManager.defineParameter("-lm","language model",PARAMETER_TYPE_FILE,false);
		commandLineManager.defineParameter("-scl","language model scaling factor",PARAMETER_TYPE_FLOAT,false);
		commandLineManager.defineParameter("-ip","insertion penalty (standard lexical units)",PARAMETER_TYPE_FLOAT,false);
		commandLineManager.defineParameter("-ips","insertion penalty (silence and filler lexical units)",PARAMETER_TYPE_FLOAT,false);	
		commandLineManager.defineParameter("-ipf","filler specific insertion penalties",PARAMETER_TYPE_FILE,true);	
		commandLineManager.defineParameter("-srg","semiring used to do weight pushing",PARAMETER_TYPE_STRING,true,"none|tropical|log","log");
		commandLineManager.defineParameter("-net","decoding network to build",PARAMETER_TYPE_FILE,false);
		
		// parse the parameters
		if (commandLineManager.parseParameters(argc,argv) == false) {
			return -1;
		}
		
		// get the parameters
		const char *strFilePhoneticSet = commandLineManager.getStrParameterValue("-pho");
		const char *strFileLexicon = commandLineManager.getStrParameterValue("-lex");
		const char *strFileLanguageModel = commandLineManager.getStrParameterValue("-lm");
		const char *strLanguageModelFormat = "ARPA";
		const char *strLanguageModelType = "ngram";
		const char *strFileModels = commandLineManager.getStrParameterValue("-mod");
		float fLMScalingFactor = commandLineManager.getFloatParameterValue("-scl");
		float fInsertionPenaltyStandard = commandLineManager.getFloatParameterValue("-ip");
		float fInsertionPenaltyFiller = commandLineManager.getFloatParameterValue("-ips");
		const char *strFileInsertionPenaltyFiller = NULL;
		if (commandLineManager.isParameterSet("-ipf")) {
			strFileInsertionPenaltyFiller = commandLineManager.getStrParameterValue("-ipf");
		}
		const char *strFileDecodingNetwork = commandLineManager.getStrParameterValue("-net");
	
		// load the phone set
		PhoneSet phoneSet(strFilePhoneticSet);
		phoneSet.load();
		
		// load the acoustic models
		HMMManager hmmManager(&phoneSet,HMM_PURPOSE_EVALUATION);
		hmmManager.load(strFileModels);
		hmmManager.initializeDecoding();	
		
		// load the lexicon
		LexiconManager lexiconManager(strFileLexicon,&phoneSet);
		lexiconManager.load();
		// set default insertion penalty to each lexical unit in the lexicon
		lexiconManager.attachLexUnitPenalties(fInsertionPenaltyStandard,fInsertionPenaltyFiller);
		// set specific insertion penalties if available
		if (strFileInsertionPenaltyFiller != NULL) {
			FillerManager fillerManager(strFileInsertionPenaltyFiller);	
			fillerManager.load();
			fillerManager.attachInsertionPenaltyFillers(&lexiconManager);
		}
		lexiconManager.print();  
		
		// load the language model
		LMManager lmManager(&lexiconManager,strFileLanguageModel,strLanguageModelFormat,strLanguageModelType); 
		lmManager.load();
	
		// build the decoding network
		WFSABuilder wfsaBuilder(&phoneSet,&hmmManager,&lexiconManager,&lmManager,fLMScalingFactor);
		
		double dBegin = TimeUtils::getTimeMilliseconds();
		
		WFSAcceptor *wfsAcceptor = wfsaBuilder.build();
		if (!wfsAcceptor) {
			BVC_ERROR << "unable to create the WFSA";
		}
		wfsAcceptor->print();
		
		// store the acceptor to disk
		wfsAcceptor->store(&lexiconManager,strFileDecodingNetwork);
		
		double dEnd = TimeUtils::getTimeMilliseconds();
		double dMillisecondsInterval = dEnd - dBegin;
		printf("building time: %.2f\n",dMillisecondsInterval/1000.0);
	
		delete wfsAcceptor;
	
	} catch (std::runtime_error &e) {
	
		std::cerr << e.what() << std::endl;
		return -1;
	}	
	
	return 0;
}

