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
#include "BacktraceExtension.h"
#include "CAndroidRequest.h"
#include "CKLBLuaEnv.h"

#include "KLBBase64.h"

static BacktraceExtension* s_backtraceExtension = NULL;
const char* BacktraceExtension::s_javaClassName =
	"extension/klb/Backtrace/PFInterface";

BacktraceExtension::BacktraceExtension()
{
	m_javaClass = CAndroidRequest::getInstance()->getJavaClass(s_javaClassName, true);
}

BacktraceExtension::~BacktraceExtension()
{
}

BacktraceExtension&
BacktraceExtension::getInstance()
{
	if(!s_backtraceExtension) {
		s_backtraceExtension = new BacktraceExtension();
	}
	return *s_backtraceExtension;
}

void
BacktraceExtension::registerPlatformCallbacks()
{
}

void
BacktraceExtension::beforeAssertFunction(const char* message)
{
	jvalue result;
	CAndroidRequest::getInstance()->callJavaMethod(m_javaClass, result,
		"beforeAssertFunction", 'V', "S", message);

	char luaDump[1024];
	CKLBLuaEnv::getInstance().dumpLuaStack(luaDump, sizeof(luaDump), true);
	CAndroidRequest::getInstance()->callJavaMethod(m_javaClass, result,
		"sendLuaDump", 'V', "S", luaDump);
}

void
BacktraceExtension::addExtMsg(const char* key, const char* value, bool sendImmediately)
{
	jvalue result;
	if(sendImmediately) {
		size_t keyLength = strlen(key);
		u32 encodedKeyLength = keyLength * 2;
		char* encodedKey = KLBNEWA(char, encodedKeyLength);
		KLBNetAPI_encodeBase64(key, keyLength, encodedKey, &encodedKeyLength);

		size_t valueLength = strlen(value);
		u32 encodedValueLength = valueLength * 2;
		char* encodedValue = KLBNEWA(char, encodedValueLength);
		KLBNetAPI_encodeBase64(value, valueLength, encodedValue, &encodedValueLength);

		CAndroidRequest::getInstance()->callJavaMethod(m_javaClass, result,
			"stockAdditionalInfo", 'V', "SS", encodedKey, encodedValue);
		KLBDELETEA(encodedKey);
		KLBDELETEA(encodedValue);
	} else {
		CAndroidRequest::getInstance()->callJavaMethod(m_javaClass, result,
			"stockAdditionalInfo", 'V', "SS", key, value);
	}
}

void
BacktraceExtension::sendException(const char* message)
{
	jvalue result;
	CAndroidRequest::getInstance()->callJavaMethod(m_javaClass, result,
		"sendException", 'V', "S", message);
}

void
BacktraceExtension::leaveBreadcrumb(const char* message)
{
	jvalue result;
	CAndroidRequest::getInstance()->callJavaMethod(m_javaClass, result,
		"leaveBreadcrumb", 'V', "S", message);
}
