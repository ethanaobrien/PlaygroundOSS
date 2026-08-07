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
//
//  platformhandler.cpp
//

#include "CPFInterface.h"
#include "OSWidget.h"
#if defined(__ANDROID__)
#include "KLBOpenSLNewEngine.h"
#endif
;

IWidget::~IWidget() {}

IClientRequest::IClientRequest() {}
IClientRequest::~IClientRequest() {}

IPlatformRequest::IPlatformRequest()
: m_audio(NULL)
, m_bNoDefaultFont(false)
, m_audioFrameEnabled(true)
{}
IPlatformRequest::~IPlatformRequest() {}

bool IPlatformRequest::init() {
#if defined(__ANDROID__)
	m_audio = KLBOpenSLNewEngine::getInstance();
#else
	m_audio = getNewAudioImplementation();
#endif
	m_audio->init();
	return true;
}

void IPlatformRequest::shutdownAudioSystem() {
	m_audio->shutdown();
}

bool IPlatformRequest::isSafeAreaScreen() {
	return false;
}

void IPlatformRequest::getSafeAreaInset(float* insets) {
	insets[0] = 0.0f;
	insets[1] = 0.0f;
	insets[2] = 0.0f;
	insets[3] = 0.0f;
}

void IPlatformRequest::registerScriptSource(const char* /*source*/, int /*sourceSize*/, const char* /*sourceName*/) {
}

CPFInterface * CPFInterface::instance = NULL;

// CPFInterface は登録された CPFStrategy の method を呼び出すだけなので、
// Singleton 以外の部分については inline 定義される。
CPFInterface::CPFInterface()
: m_pClient     (NULL)
, m_pPlatform   (NULL) 
{
}

CPFInterface::~CPFInterface() 
{
}

CPFInterface&
CPFInterface::getInstance() {
    /*
    static CPFInterface instance;
    return instance;
     */
    if(!instance) {
        instance = new CPFInterface();
    }
    return *instance;
}


bool
CPFInterface::isBigEndian()
{
    u16     num = 0x7700;
    char *  ptr = (char *)&num;
    return (*ptr == 0x77);
}
