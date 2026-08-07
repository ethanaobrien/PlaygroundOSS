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
#ifndef BacktraceExtension_h
#define BacktraceExtension_h

#include <jni.h>

class BacktraceExtension
{
public:
	static BacktraceExtension& getInstance();

	void beforeAssertFunction(const char* message);
	void addExtMsg(const char* key, const char* value, bool sendImmediately);
	void sendException(const char* message);
	void leaveBreadcrumb(const char* message);

private:
	BacktraceExtension();
	virtual ~BacktraceExtension();

public:
	virtual void registerPlatformCallbacks();

private:
	BacktraceExtension(const BacktraceExtension&);
	BacktraceExtension& operator=(const BacktraceExtension&);

	static const char* s_javaClassName;
	jclass m_javaClass;
};

#endif // BacktraceExtension_h
