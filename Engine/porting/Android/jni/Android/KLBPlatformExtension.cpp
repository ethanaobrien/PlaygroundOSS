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

#include "BacktraceExtension.h"
#include "KLBPlatformExtension.h"

static KLBPlatformExtensionRegistry* s_platformExtensionRegistry = NULL;

KLBPlatformExtensionRegistry*
KLBPlatformExtensionRegistry::getInstance()
{
	if (!s_platformExtensionRegistry) {
		s_platformExtensionRegistry = new KLBPlatformExtensionRegistry();
	}
	return s_platformExtensionRegistry;
}

void
KLBPlatformExtensionRegistry::invokeBeforePlatformInit()
{
	const std::vector<KLBPlatformInitCallback*>::iterator end =
		m_beforePlatformInit.end();
	for (std::vector<KLBPlatformInitCallback*>::iterator callback =
			 m_beforePlatformInit.begin();
		 callback != end;
		 ++callback)
	{
		(*callback)->beforePlatformInit();
	}
}

void
KLBPlatformExtensionRegistry::invokeAfterPlatformInit()
{
	const std::vector<KLBPlatformInitCallback*>::iterator end =
		m_afterPlatformInit.end();
	for (std::vector<KLBPlatformInitCallback*>::iterator callback =
			 m_afterPlatformInit.begin();
		 callback != end;
		 ++callback)
	{
		(*callback)->afterPlatformInit();
	}
}

void
KLBPlatformExtensionRegistry::registerCallback(
	const char* timing,
	KLBPlatformInitCallback* callback)
{
	const size_t timingLength = strlen(timing) + 1;
	if (!strncmp(timing, "beforePlatformInit", timingLength)) {
		m_beforePlatformInit.push_back(callback);
	} else if (!strncmp(timing, "afterPlatformInit", timingLength)) {
		m_afterPlatformInit.push_back(callback);
	}
}

void
KLBPlatformExtensionRegistry::clear()
{
	m_beforePlatformInit.clear();
	m_afterPlatformInit.clear();
}

void
KLBPlatformExtensionRegistry::initializeExtensions()
{
	clear();
	BacktraceExtension::getInstance().registerPlatformCallbacks();
}
