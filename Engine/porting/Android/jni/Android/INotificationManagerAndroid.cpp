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

#include "NotificationManager.h"
#include "CAndroidRequest.h"
#include "CJNI.h"

class INotificationManagerAndroid : public INotificationManager
{
public:
	explicit INotificationManagerAndroid(CKLBNotificationManager* owner)
	: INotificationManager(owner) {}

	virtual void setLocalNotificationWithAlarm(const char* tag, int tagIndex,
		const char* message, int identifier, const char* sound);
	virtual void cancelLocalNotification(const char* tag, int tagIndex);
	virtual void requestPermission();
	virtual bool getEnableNotification();
	virtual void getRemoteToken(char* buffer, int bufferLength);
	virtual void onActivityResume();
};

INotificationManager*
INotificationManager::create(CKLBNotificationManager* owner)
{
	klb_assertNull(!s_instance, "Only one NotificationManager is allowed !");
	s_instance = KLBNEWC(INotificationManagerAndroid, (owner));
	return s_instance;
}

void
INotificationManager::notify(u32 callbackIndex, int parameter,
	const char* message)
{
	m_owner->queueNotification(callbackIndex, parameter, message);
}

void
INotificationManagerAndroid::setLocalNotificationWithAlarm(const char* tag,
	int tagIndex, const char* message, int identifier, const char* sound)
{
	jvalue value;
	CAndroidRequest::getInstance()->callJavaMethod(NULL, value,
		"setLocalNotificationWithAlarm", 'V', "SISIS",
		tag, tagIndex, message, identifier, sound);
}

void
INotificationManagerAndroid::cancelLocalNotification(const char* tag,
	int tagIndex)
{
	jvalue value;
	CAndroidRequest::getInstance()->callJavaMethod(NULL, value,
		"cancelLocalNotification", 'V', "SI", tag, tagIndex);
}

void
INotificationManagerAndroid::requestPermission()
{
	jvalue value;
	CAndroidRequest::getInstance()->callJavaMethod(NULL, value,
		"requirePermission", 'V', "I", 2);
}

bool
INotificationManagerAndroid::getEnableNotification()
{
	jvalue value;
	CAndroidRequest::getInstance()->callJavaMethod(NULL, value,
		"getEnableNotification", 'Z', "");
	return value.z != 0;
}

void
INotificationManagerAndroid::getRemoteToken(char* buffer, int bufferLength)
{
	jvalue value;
	CAndroidRequest::getInstance()->callJavaMethod(NULL, value,
		"getKeyChain", 'S', "SS", "remoteToken", "remoteToken");

	JNIEnv* env = CJNI::getJNIEnv();
	const char* source = env->GetStringUTFChars(
		static_cast<jstring>(value.l), NULL);
	int index = 0;
	while(source[index] && index < bufferLength - 1) {
		buffer[index] = source[index];
		++index;
	}
	buffer[index] = '\0';
	env->ReleaseStringUTFChars(static_cast<jstring>(value.l), source);
}

void
INotificationManagerAndroid::onActivityResume()
{
	notify(2, 0, "");
}
