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
#ifndef KLBPlatformExtension_h
#define KLBPlatformExtension_h

#include <vector>

class KLBPlatformInitCallback
{
public:
	virtual void beforePlatformInit() = 0;
	virtual void afterPlatformInit() = 0;
};

class KLBPlatformExtensionRegistry
{
public:
	static KLBPlatformExtensionRegistry* getInstance();

	void invokeBeforePlatformInit();
	void invokeAfterPlatformInit();
	void registerCallback(
		const char* timing,
		KLBPlatformInitCallback* callback);
	void clear();
	void initializeExtensions();

private:
	KLBPlatformExtensionRegistry() {}

	std::vector<KLBPlatformInitCallback*> m_beforePlatformInit;
	std::vector<KLBPlatformInitCallback*> m_afterPlatformInit;
};

#endif // KLBPlatformExtension_h
