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
	return curl_global_init(CURL_GLOBAL_ALL) == CURLE_OK;
}

void
CurlObjectInternal::shutdownLibrary()
{
	curl_global_cleanup();
}

void
CurlObjectInternal::reset()
{
	freeFormHeaders();
	m_formEnd = NULL;
	m_postConfigured = false;
}

void
CurlObjectInternal::cleanup()
{
	curl_easy_cleanup(m_curl);
}

int
CurlObjectInternal::perform()
{
	return curl_easy_perform(m_curl);
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
	curl_easy_setopt(m_curl, CURLOPT_PROGRESSDATA, callbackContext);
	curl_easy_setopt(m_curl, CURLOPT_PROGRESSFUNCTION, progressCallback);
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
