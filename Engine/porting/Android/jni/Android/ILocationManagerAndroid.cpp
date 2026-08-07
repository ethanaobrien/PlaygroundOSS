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
#include "ILocationManagerAndroid.h"
#include "CAndroidRequest.h"

void
ILocationManagerAndroid::requireLocation()
{
    jvalue result;
    CAndroidRequest::getInstance()->callJavaMethod(
        NULL, result, "getLocation", 'V', "");
}

bool
ILocationManagerAndroid::stopLocation()
{
    jvalue result;
    CAndroidRequest::getInstance()->callJavaMethod(
        NULL, result, "stopGetLocation", 'Z', "");
    return result.z != JNI_FALSE;
}

int
ILocationManagerAndroid::getPermissionStatus()
{
    jvalue result;
    CAndroidRequest::getInstance()->callJavaMethod(
        NULL, result, "getPermissionStatus", 'I', "");
    return result.i;
}

void
ILocationManagerAndroid::requirePermission()
{
    jvalue result;
    CAndroidRequest::getInstance()->callJavaMethod(
        NULL, result, "requirePermission", 'V', "I", 1);
}

// The platform half of the location singleton.  Core declares create() in
// CKLBLocationManager.h and calls it from CKLBLocationManager::initScript;
// without this definition the engine does not link.
ILocationManager*
ILocationManager::create(CKLBLocationManager* owner)
{
	klb_assertNull(!s_instance, "Only one LocationManager is allowed !");
	s_instance = KLBNEWC(ILocationManagerAndroid, (owner));
	return s_instance;
}
