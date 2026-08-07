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
#ifndef MULTITHREADED_NETWORK_KLB
#define MULTITHREADED_NETWORK_KLB

#include "CKLBHTTPInterface.h"
#include <list>
#include "curl.h"

// Per-request libcurl state owned by the platform HTTP service.  The
// platform interface deliberately exposes only this handle type; engine HTTP
// code does not need to depend on libcurl's individual data structures.
class CurlObjectInternal {
public:
	static CurlObjectInternal* create();
	static void destroy(CurlObjectInternal* operation);
	static bool initializeLibrary();
	static void shutdownLibrary();

	explicit CurlObjectInternal(CURL* curl)
	: m_curl(curl)
	, m_headers(NULL)
	, m_form(NULL)
	, m_formEnd(NULL)
	, m_postConfigured(false)
	{}
	~CurlObjectInternal() {}

	virtual void reset();
	virtual void cleanup();
	virtual int perform();
	virtual void freeFormHeaders();
	virtual void appendHeader(const char* header);
	virtual void setPostFields();
	virtual void setPostData(long contentLength, const void* data);
	virtual void addFormData(const char* name, long contentLength, const void* data);
	virtual void setupConnection(const char* url, const char* proxy, void* callbackContext,
	                             void* progressCallback, void* headerCallback, void* writeCallback);
	virtual long getHttpCode();

private:
	CURL*            m_curl;
	curl_slist*      m_headers;
	curl_httppost*   m_form;
	curl_httppost*   m_formEnd;
	bool             m_postConfigured;
};

#define LOCK(a)					CPFInterface::getInstance().platform().mutexLock(a)
#define UNLOCK(a)				CPFInterface::getInstance().platform().mutexUnlock(a)
#define ALLOC_LOCK()			CPFInterface::getInstance().platform().allocMutex()
#define FREE_LOCK(a)			CPFInterface::getInstance().platform().freeMutex(a)
#define ALLOCEVENT_LOCK()		CPFInterface::getInstance().platform().allocEventLock()
#define FREEEVENT_LOCK(a)		CPFInterface::getInstance().platform().freeEventLock(a)
#define CREATE_THREAD(a,b)		CPFInterface::getInstance().platform().createThread(a, b)
#define FREE_THREAD(a)			CPFInterface::getInstance().platform().deleteThread(a)
#define WAKE_THREAD(a)			CPFInterface::getInstance().platform().eventWakeup(a)
#define SLEEP_THREAD(a)			CPFInterface::getInstance().platform().eventSleep(a)

/*!
* \class NetworkManager
* \brief Network Manager
* 
* 
*/
class NetworkManager {
public:
	static bool 				startNetworkManager	();
	static void 				stopNetworkManager	(bool complete);
	static CKLBHTTPInterface*	createConnection	();
	static void					releaseConnection	(CKLBHTTPInterface* connection);
	static void					wakeUp				();
private:
		   s32					workThread			();
	static s32					threadFunc			(void* pThread, void* data);
	static void					releaseAllConnections();
	static NetworkManager		s_manager;
	
	NetworkManager();
	~NetworkManager();
	
	void* 				m_lock;
	void*				m_eventLock;
	std::list<CKLBHTTPInterface*> m_entries;
	std::list<CKLBHTTPInterface*> m_killEntries;
	bool				m_bShutDown;
	void*				m_thread;
	bool				m_bStarted;
	bool				m_bPaused;

	volatile
	bool				m_bShutDownComplete;
	
};

#endif
