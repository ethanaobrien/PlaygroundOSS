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
#include <stdint.h>
#if !defined(PLAYGROUND_WEB)
#include "curl.h"
#endif

struct CurlTransferMetrics {
	double queueSeconds;
	double dnsSeconds;
	double connectSeconds;
	double firstByteSeconds;
	double totalSeconds;
	double downloadedBytes;
	double averageBytesPerSecond;
	long connectionCount;
	long redirectCount;
	uint64_t attemptNumber;
};

// Per-request libcurl state owned by the platform HTTP service.  The
// platform interface deliberately exposes only this handle type; engine HTTP
// code does not need to depend on libcurl's individual data structures.
class CurlObjectInternal {
public:
	static CurlObjectInternal* create();
	static void destroy(CurlObjectInternal* operation);
	static bool initializeLibrary();
	static void shutdownLibrary();
	static bool configureCookieStorage(const char* path);
	static bool flushCookieStorage(bool* changed = NULL);
	static bool clearCookieStorage();

#if defined(PLAYGROUND_WEB)
	CurlObjectInternal();
#else
	explicit CurlObjectInternal(CURL* curl)
	: m_curl(curl)
	, m_headers(NULL)
	, m_form(NULL)
	, m_formEnd(NULL)
	, m_postConfigured(false)
	, m_progressContext(NULL)
	, m_progressCallback(NULL)
	, m_abortPredicate(NULL)
	, m_abortContext(NULL)
	, m_configuredAtNanoseconds(0)
	, m_performStartedAtNanoseconds(0)
	, m_performCount(0)
	{}
#endif
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
	virtual CurlTransferMetrics getTransferMetrics() const;
	void setAbortPredicate(bool (*predicate)(void*), void* context);
	void configureTransportLimits(long maximumConnections,
	                             long connectTimeoutSeconds,
	                             long lowSpeedBytesPerSecond,
	                             long lowSpeedSeconds);

private:
	static int progressDispatch(void* context, double downloadTotal,
	                            double downloadNow, double uploadTotal,
	                            double uploadNow);

#if defined(PLAYGROUND_WEB)
	struct WebData;
	WebData*         m_web;
#else
	CURL*            m_curl;
	curl_slist*      m_headers;
	curl_httppost*   m_form;
	curl_httppost*   m_formEnd;
#endif
	bool             m_postConfigured;
	void*            m_progressContext;
	void*            m_progressCallback;
	bool             (*m_abortPredicate)(void*);
	void*            m_abortContext;
	uint64_t         m_configuredAtNanoseconds;
	uint64_t         m_performStartedAtNanoseconds;
	uint64_t         m_performCount;
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
