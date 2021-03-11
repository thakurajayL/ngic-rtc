/*
 * Copyright (c) 2020 Sprint
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#ifndef __CP_CONFIG_AS_CSV_H_
#define __CP_CONFIG_AS_CSV_H_

#include <iostream>
#include <stdint.h>
#include <fstream>

#include "CpConfig.h"

class CpConfigAsCSV : public CpConfig
{
	private:
		std::string strCSVPath;
	public:
		CpConfigAsCSV();
		CpConfigAsCSV(const std::string &strPath);

		/**
		 * @brief  : Reads csv file containing entries of all registered Cp's 
                             and stores it in vector
		 * @param  : No param
		 * @return : Returns 0 in case of Success, -1 otherwise
		 */
		int8_t ReadCpConfig(void);

		/**
		 * @brief  : Adds/Updates/Deletes Cp configuration entry from csv file
		 * @param  : uiAction, action can be add(1)/update(2)/delete(3)
		 * @param  : strIpAddr, Ip-address of Cp
		 * @return : Returns 0 in case of Success, -1 otherwise
		 */
		int8_t UpdateCpConfig(uint8_t uiAction, const std::string &strIpAddr);

		~CpConfigAsCSV();
};

#endif /* __CP_CONFIG_AS_CSV_H_ */
