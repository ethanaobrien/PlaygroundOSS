/*
   Copyright 2013 KLab Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <string.h>
#include <sys/stat.h>

#include "CPFInterface.h"

bool
KLBCreateDirectories(const char * path)
{
	char * subPath = new char [ strlen(path) + 1 ];
	for (const char * p = path; *p; p++) {
		if (*p == '/') {
			size_t len = p - path;
			strncpy(subPath, path, len + 1);
			subPath[len + 1] = 0;

			struct stat st;
			if (stat(subPath, &st) != 0) {
				if (mkdir(subPath, 0755) != 0) {
					delete [] subPath;
					return false;
				}
				CPFInterface::getInstance().platform().excludePathFromBackup(subPath);
			}
		}
	}
	delete [] subPath;
	return true;
}
