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
#include "MultithreadedNetwork.h"

#include <chrono>
#include <mutex>
#include <string>

namespace {

CURLSH* g_cookieShare = NULL;
CURL* g_cookieStorage = NULL;
std::mutex g_cookieLock;
std::mutex g_cookieStorageLock;
std::string g_cookiePath;
std::string g_cookieFingerprint;

bool captureCookieFingerprint(std::string& fingerprint)
{
	curl_slist* cookies = NULL;
	if (!g_cookieStorage ||
	    curl_easy_getinfo(g_cookieStorage, CURLINFO_COOKIELIST, &cookies) !=
	        CURLE_OK)
		return false;
	fingerprint.clear();
	for (curl_slist* cookie = cookies; cookie; cookie = cookie->next) {
		const char* value = cookie->data ? cookie->data : "";
		fingerprint.append(value);
		fingerprint.push_back('\n');
	}
	curl_slist_free_all(cookies);
	return true;
}

void lockCookieShare(CURL*, curl_lock_data, curl_lock_access, void*)
{
	g_cookieLock.lock();
}

void unlockCookieShare(CURL*, curl_lock_data, void*)
{
	g_cookieLock.unlock();
}

void releaseCookieState()
{
	std::lock_guard<std::mutex> lock(g_cookieStorageLock);
	if (g_cookieStorage) {
		if (!g_cookiePath.empty())
			curl_easy_setopt(g_cookieStorage, CURLOPT_COOKIELIST, "FLUSH");
		curl_easy_cleanup(g_cookieStorage);
		g_cookieStorage = NULL;
	}
	g_cookiePath.clear();
	g_cookieFingerprint.clear();
	if (g_cookieShare) {
		curl_share_cleanup(g_cookieShare);
		g_cookieShare = NULL;
	}
}

} // namespace

NetworkManager NetworkManager::s_manager;

NetworkManager::NetworkManager()
	: m_bStarted(false)
{
}

NetworkManager::~NetworkManager()
{
}

// TODO : Task destruction will have task executed at a given frame
// and all task are killed AT THE END OF FRAME.
// Make sure that we kill the network manager AFTER that.

/*static*/
bool 
NetworkManager::startNetworkManager() 
{
	s_manager.m_bPaused = false;
	if(s_manager.m_bStarted) {
		if(s_manager.m_eventLock) {
			WAKE_THREAD(s_manager.m_eventLock);
		}
	} else {
		// 1. Start Thread
		s_manager.m_lock 				= ALLOC_LOCK();
		s_manager.m_eventLock			= ALLOCEVENT_LOCK();
		s_manager.m_bShutDownComplete	= false;
		s_manager.m_thread				= NULL;
		s_manager.m_bShutDown			= false;

		s_manager.m_thread = CREATE_THREAD(threadFunc,&s_manager);
		s_manager.m_bStarted = true;
	}
	return s_manager.m_thread != 0;
}

/*static*/
void 
NetworkManager::stopNetworkManager(bool complete) 
{
	if (!complete) {
		s_manager.m_bPaused = true;
		releaseAllConnections();
		return;
	}

	s_manager.m_bShutDown = true;
	releaseAllConnections();
	
	// May be asleep
	if(s_manager.m_eventLock) {
		WAKE_THREAD(s_manager.m_eventLock);
		while (s_manager.m_bShutDownComplete == false) {
			// Wait other thread complete loop.
		}
	}

	// TODO clear all entries.
	klb_assertNull(s_manager.m_entries.empty(), "Remaining connection !?");

	if(s_manager.m_thread) {
		FREE_THREAD(s_manager.m_thread);
		s_manager.m_thread = NULL;
	}
	if(s_manager.m_lock) {
		FREE_LOCK(s_manager.m_lock);
		s_manager.m_lock = NULL;
	}
	if(s_manager.m_eventLock) {
		FREEEVENT_LOCK(s_manager.m_eventLock);
		s_manager.m_eventLock = NULL;
	}
}

void
NetworkManager::releaseAllConnections()
{
	LOCK(s_manager.m_lock);
	for(std::list<CKLBHTTPInterface*>::iterator it = s_manager.m_entries.begin(); it != s_manager.m_entries.end(); ++it) {
		(*it)->stop();
		s_manager.m_killEntries.push_back(*it);
	}
	s_manager.m_entries.clear();
	UNLOCK(s_manager.m_lock);
}

/*static*/
CKLBHTTPInterface* 
NetworkManager::createConnection() 
{
	CKLBHTTPInterface* connection = KLBNEW(CKLBHTTPInterface);
	if (connection) {
		LOCK(s_manager.m_lock);
		s_manager.m_entries.push_back(connection);
		UNLOCK(s_manager.m_lock);
	}
	return connection;
}

/*static*/
void
NetworkManager::wakeUp()
{
	WAKE_THREAD(s_manager.m_eventLock);
}

/*static*/
void 
NetworkManager::releaseConnection(CKLBHTTPInterface* connection) 
{
	if (!connection) { return; }

	LOCK(s_manager.m_lock);
	s32 expectedCount = s_manager.m_entries.size();
	expectedCount--;
	connection->stop();
	s_manager.m_entries.remove(connection);
	s_manager.m_killEntries.push_back(connection);
	klb_assert(s_manager.m_entries.size() == expectedCount, "Error");
	UNLOCK(s_manager.m_lock);
	
	// May be asleep
	WAKE_THREAD(s_manager.m_eventLock);
}

/*static*/ 
s32 
NetworkManager::threadFunc(void* /*pThread*/, void* data) 
{
	return ((NetworkManager*)data)->workThread();
}

s32 
NetworkManager::workThread() 
{
	SLEEP_THREAD(m_eventLock); // First time wait.
	
	while (!m_bShutDown) {
		if(!m_bPaused) {
			LOCK(m_lock);
			std::list<CKLBHTTPInterface*>::iterator it = m_killEntries.begin();
			while(it != m_killEntries.end()) {
				if((*it)->httpRECV()) {
					KLBDELETE(*it);
					it = m_killEntries.erase(it);
				} else {
					++it;
				}
			}
			UNLOCK(m_lock);
		}
		SLEEP_THREAD(m_eventLock);
	}
	
	m_bShutDownComplete = true;
	return 1;
}

CurlObjectInternal*
CurlObjectInternal::create()
{
	CURL* curl = curl_easy_init();
	if (curl && g_cookieShare) {
		curl_easy_setopt(curl, CURLOPT_SHARE, g_cookieShare);
		// Sharing the COOKIE data does not itself activate libcurl's cookie
		// engine on a new easy handle.  An empty COOKIEFILE enables response
		// parsing without loading a second on-disk jar.
		curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
	}
	return curl ? new CurlObjectInternal(curl) : NULL;
}

void
CurlObjectInternal::destroy(CurlObjectInternal* operation)
{
	delete operation;
}

bool
CurlObjectInternal::initializeLibrary()
{
	if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
		return false;
	g_cookieShare = curl_share_init();
	if (!g_cookieShare ||
	    curl_share_setopt(g_cookieShare, CURLSHOPT_SHARE,
	                       CURL_LOCK_DATA_COOKIE) != CURLSHE_OK ||
	    curl_share_setopt(g_cookieShare, CURLSHOPT_LOCKFUNC,
	                       lockCookieShare) != CURLSHE_OK ||
	    curl_share_setopt(g_cookieShare, CURLSHOPT_UNLOCKFUNC,
	                       unlockCookieShare) != CURLSHE_OK) {
		releaseCookieState();
		curl_global_cleanup();
		return false;
	}
	return true;
}

void
CurlObjectInternal::shutdownLibrary()
{
	releaseCookieState();
	curl_global_cleanup();
}

bool
CurlObjectInternal::configureCookieStorage(const char* path)
{
	if (!g_cookieShare || !path || !path[0])
		return false;
	std::lock_guard<std::mutex> lock(g_cookieStorageLock);
	if (g_cookieStorage) {
		curl_easy_cleanup(g_cookieStorage);
		g_cookieStorage = NULL;
	}
	g_cookiePath = path;
	g_cookieStorage = curl_easy_init();
	if (!g_cookieStorage)
		return false;
	if (curl_easy_setopt(g_cookieStorage, CURLOPT_SHARE, g_cookieShare) !=
	        CURLE_OK ||
	    curl_easy_setopt(g_cookieStorage, CURLOPT_COOKIEFILE,
	                     g_cookiePath.c_str()) != CURLE_OK ||
	    curl_easy_setopt(g_cookieStorage, CURLOPT_COOKIEJAR,
	                     g_cookiePath.c_str()) != CURLE_OK ||
	    curl_easy_setopt(g_cookieStorage, CURLOPT_COOKIELIST, "RELOAD") !=
	        CURLE_OK) {
		curl_easy_cleanup(g_cookieStorage);
		g_cookieStorage = NULL;
		g_cookiePath.clear();
		return false;
	}
	return captureCookieFingerprint(g_cookieFingerprint);
}

bool
CurlObjectInternal::flushCookieStorage(bool* changed)
{
	std::lock_guard<std::mutex> lock(g_cookieStorageLock);
	if (changed)
		*changed = false;
	std::string fingerprint;
	if (!captureCookieFingerprint(fingerprint))
		return false;
	if (fingerprint == g_cookieFingerprint)
		return true;
	if (curl_easy_setopt(g_cookieStorage, CURLOPT_COOKIELIST, "FLUSH") !=
	    CURLE_OK)
		return false;
	g_cookieFingerprint.swap(fingerprint);
	if (changed)
		*changed = true;
	return true;
}

bool
CurlObjectInternal::clearCookieStorage()
{
	std::lock_guard<std::mutex> lock(g_cookieStorageLock);
	const bool cleared = g_cookieStorage &&
	       curl_easy_setopt(g_cookieStorage, CURLOPT_COOKIELIST, "ALL") ==
	           CURLE_OK &&
	       curl_easy_setopt(g_cookieStorage, CURLOPT_COOKIELIST, "FLUSH") ==
	           CURLE_OK;
	if (cleared)
		g_cookieFingerprint.clear();
	return cleared;
}

void
CurlObjectInternal::reset()
{
	freeFormHeaders();
	m_formEnd = NULL;
	m_postConfigured = false;
	m_performCount = 0;
}

void
CurlObjectInternal::cleanup()
{
	curl_easy_cleanup(m_curl);
}

int
CurlObjectInternal::perform()
{
	++m_performCount;
	m_performStartedAtNanoseconds = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	int result = curl_easy_perform(m_curl);
	char* effectiveURL = NULL;
	curl_easy_getinfo(m_curl, CURLINFO_EFFECTIVE_URL, &effectiveURL);
	if(result) {
		fprintf(stderr, "network request failed: %s (%d): %s\n",
			effectiveURL ? effectiveURL : "<unknown URL>", result,
			curl_easy_strerror((CURLcode)result));
	} else {
		long responseCode = 0;
		curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &responseCode);
		if(responseCode >= 400) {
			fprintf(stderr, "network request returned HTTP %ld: %s\n",
				responseCode, effectiveURL ? effectiveURL : "<unknown URL>");
		}
	}
	return result;
}

void
CurlObjectInternal::freeFormHeaders()
{
	if (m_form) {
		curl_formfree(m_form);
		m_form = NULL;
	}
	if (m_headers) {
		curl_slist_free_all(m_headers);
		m_headers = NULL;
	}
}

void
CurlObjectInternal::appendHeader(const char* header)
{
	m_headers = curl_slist_append(m_headers, header);
}

void
CurlObjectInternal::setPostFields()
{
	curl_easy_setopt(m_curl, CURLOPT_HTTPPOST, m_form);
	m_postConfigured = true;
}

void
CurlObjectInternal::setPostData(long contentLength, const void* data)
{
	curl_easy_setopt(m_curl, CURLOPT_POSTFIELDSIZE, contentLength);
	curl_easy_setopt(m_curl, CURLOPT_COPYPOSTFIELDS, data);
	m_postConfigured = true;
}

void
CurlObjectInternal::addFormData(const char* name, long contentLength, const void* data)
{
	curl_formadd(&m_form, &m_formEnd,
	             CURLFORM_COPYNAME, name,
	             CURLFORM_CONTENTSLENGTH, contentLength,
	             CURLFORM_COPYCONTENTS, data,
	             CURLFORM_END);
}

void
CurlObjectInternal::setupConnection(const char* url, const char* proxy, void* callbackContext,
	                           void* progressCallback, void* headerCallback, void* writeCallback)
{
	curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, m_headers);
	if (!m_postConfigured) {
		curl_easy_setopt(m_curl, CURLOPT_HTTPGET, 1L);
	}
	curl_easy_setopt(m_curl, CURLOPT_URL, url);
	curl_easy_setopt(m_curl, CURLOPT_PROXY, proxy);
	curl_easy_setopt(m_curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(m_curl, CURLOPT_NOSIGNAL, 1L);
	m_progressContext = callbackContext;
	m_progressCallback = progressCallback;
	m_configuredAtNanoseconds = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	curl_easy_setopt(m_curl, CURLOPT_PROGRESSDATA, this);
	curl_easy_setopt(m_curl, CURLOPT_PROGRESSFUNCTION,
	                 &CurlObjectInternal::progressDispatch);
	curl_easy_setopt(m_curl, CURLOPT_HEADERDATA, callbackContext);
	curl_easy_setopt(m_curl, CURLOPT_HEADERFUNCTION, headerCallback);
	curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, callbackContext);
	curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, writeCallback);
	curl_easy_setopt(m_curl, CURLOPT_ACCEPT_ENCODING, "gzip,deflate");
}

long
CurlObjectInternal::getHttpCode()
{
	long httpCode;
	curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &httpCode);
	return httpCode;
}

CurlTransferMetrics
CurlObjectInternal::getTransferMetrics() const
{
	CurlTransferMetrics metrics = {};
	if (m_performStartedAtNanoseconds >= m_configuredAtNanoseconds) {
		metrics.queueSeconds = static_cast<double>(
			m_performStartedAtNanoseconds - m_configuredAtNanoseconds) / 1000000000.0;
	}
	curl_easy_getinfo(m_curl, CURLINFO_NAMELOOKUP_TIME, &metrics.dnsSeconds);
	curl_easy_getinfo(m_curl, CURLINFO_CONNECT_TIME, &metrics.connectSeconds);
	curl_easy_getinfo(m_curl, CURLINFO_STARTTRANSFER_TIME, &metrics.firstByteSeconds);
	curl_easy_getinfo(m_curl, CURLINFO_TOTAL_TIME, &metrics.totalSeconds);
	curl_easy_getinfo(m_curl, CURLINFO_SIZE_DOWNLOAD, &metrics.downloadedBytes);
	curl_easy_getinfo(m_curl, CURLINFO_SPEED_DOWNLOAD,
	                  &metrics.averageBytesPerSecond);
	curl_easy_getinfo(m_curl, CURLINFO_NUM_CONNECTS, &metrics.connectionCount);
	curl_easy_getinfo(m_curl, CURLINFO_REDIRECT_COUNT, &metrics.redirectCount);
	metrics.attemptNumber = m_performCount;
	return metrics;
}

void
CurlObjectInternal::setAbortPredicate(bool (*predicate)(void*), void* context)
{
	m_abortPredicate = predicate;
	m_abortContext = context;
}

void
CurlObjectInternal::configureTransportLimits(long maximumConnections,
	                                          long connectTimeoutSeconds,
	                                          long lowSpeedBytesPerSecond,
	                                          long lowSpeedSeconds)
{
	curl_easy_setopt(m_curl, CURLOPT_MAXCONNECTS, maximumConnections);
	curl_easy_setopt(m_curl, CURLOPT_CONNECTTIMEOUT, connectTimeoutSeconds);
	curl_easy_setopt(m_curl, CURLOPT_LOW_SPEED_LIMIT, lowSpeedBytesPerSecond);
	curl_easy_setopt(m_curl, CURLOPT_LOW_SPEED_TIME, lowSpeedSeconds);
	curl_easy_setopt(m_curl, CURLOPT_TCP_KEEPALIVE, 1L);
}

int
CurlObjectInternal::progressDispatch(void* context, double downloadTotal,
	                                  double downloadNow, double uploadTotal,
	                                  double uploadNow)
{
	CurlObjectInternal* operation = static_cast<CurlObjectInternal*>(context);
	if (operation->m_abortPredicate &&
	    operation->m_abortPredicate(operation->m_abortContext))
		return 1;
	if (!operation->m_progressCallback)
		return 0;
	typedef int (*ProgressCallback)(void*, double, double, double, double);
	ProgressCallback callback =
		reinterpret_cast<ProgressCallback>(operation->m_progressCallback);
	return callback(operation->m_progressContext, downloadTotal, downloadNow,
	                uploadTotal, uploadNow);
}
