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

#include "CKLBMotionManager.h"
#include "CAndroidRequest.h"

class IMotionManagerAndroid : public IMotionManager
{
public:
	virtual ~IMotionManagerAndroid();
	virtual void start();
	virtual void stop();
	virtual float getAzimuth();
	virtual float getElevation();
};

IMotionManager*
IMotionManager::getInstance()
{
	if(!s_instance) {
		s_instance = new IMotionManagerAndroid();
	}
	return s_instance;
}

IMotionManagerAndroid::~IMotionManagerAndroid()
{
}

void
IMotionManagerAndroid::start()
{
	jvalue value;
	CAndroidRequest::getInstance()->callJavaMethod(NULL, value,
		"startObservation", 'V', "");
}

void
IMotionManagerAndroid::stop()
{
	jvalue value;
	CAndroidRequest::getInstance()->callJavaMethod(NULL, value,
		"stopObservation", 'V', "");
}

float
IMotionManagerAndroid::getAzimuth()
{
	jvalue value;
	CAndroidRequest::getInstance()->callJavaMethod(NULL, value,
		"getTheta", 'F', "");
	return value.f;
}

float
IMotionManagerAndroid::getElevation()
{
	jvalue value;
	CAndroidRequest::getInstance()->callJavaMethod(NULL, value,
		"getPhi", 'F', "");
	return value.f;
}
