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
#include "IOBase.h"
#include "Mappings.h"

namespace Bavieca {

// contructor
Mappings::Mappings(const char *strFile)
{
	m_strFile = strFile;
}

// destructor
Mappings::~Mappings() {

	m_mMappings.clear();	
}

// load the mappings
void Mappings::load() {

	FileInput file(m_strFile.c_str(),false);
	file.open();
	
	string strLine;
	while(std::getline(file.getStream(),strLine).good()) {
		std::stringstream s(strLine);
		string str1,str2;
		IOBase::readString(s,str1);
		IOBase::readString(s,str2);
		m_mMappings.insert(map<string,string>::value_type(str1,str2));
	}
	
	file.close();
}

// map a lexical unit if a mapping is defined 
const char *Mappings::operator[](const char *str) {
	map<string,string>::iterator it = m_mMappings.find(str);
	if (it == m_mMappings.end()) {
		return str;
	} else {
		return it->second.c_str();
	}	
}

};	// end-of-namespace


