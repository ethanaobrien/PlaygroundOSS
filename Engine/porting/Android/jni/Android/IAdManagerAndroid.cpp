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

#include "AdManager.h"
#include "CAndroidRequest.h"
#include "CJNI.h"
#include "PackageDefine.h"

class IAdManagerAndroid : public IAdManager
{
public:
	explicit IAdManagerAndroid(CKLBAdManager* owner) : IAdManager(owner) {}
	virtual ~IAdManagerAndroid() {}
	virtual void preloadAd(bool rewarded, const char* placement);
	virtual void showAd();
};

extern IAdManager* g_adManager;

IAdManager*
IAdManager::getInstance(CKLBAdManager* owner)
{
	klb_assertNull(!g_adManager, "Only one LocationManager is allowed !");
	g_adManager = new IAdManagerAndroid(owner);
	return g_adManager;
}

void
IAdManagerAndroid::preloadAd(bool rewarded, const char* placement)
{
	jclass javaClass = CAndroidRequest::getInstance()->getJavaClass(
		"extension/klb/Firebase/PFInterface", true);
	jvalue result;
	CAndroidRequest::getInstance()->callJavaMethod(javaClass, result,
		"PreloadRewardedAd", 'V', "ZS", rewarded, placement);
}

void
IAdManagerAndroid::showAd()
{
	jclass javaClass = CAndroidRequest::getInstance()->getJavaClass(
		"extension/klb/Firebase/PFInterface", true);
	jvalue result;
	CAndroidRequest::getInstance()->callJavaMethod(javaClass, result,
		"ShowRewardedAd", 'V', "");
}

extern "C" JNIEXPORT void JNICALL APP_FUNC(onAdMobCallback)
  (JNIEnv* env, jobject obj, jint command, jint parameter, jstring j_message)
{
	const char* message = env->GetStringUTFChars(j_message, NULL);
	IAdManager::getInstance()->onAdResult(command, parameter, message);
}
