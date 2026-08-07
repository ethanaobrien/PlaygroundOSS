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
#include "CKLBNetAPI.h"
#include "CKLBLuaEnv.h"
#include "CKLBUtility.h"
#include "CKLBJsonItem.h"
#include "CPFInterface.h"
#include "CKLBNetAPIKeyChain.h"

#include <time.h>
#include <ctype.h>

extern void KLBNetAPI_encodeBase64(const char* src, int len, char* dst, u32* outLen);
extern void KLBNetAPI_decodeBase64(const char* src, char* dst, u32* outLen);

bool
CKLBNetAPI::encryptThenBase64(const char* input, size_t inputLength,
							 const u8* key, size_t keyLength, char** output)
{
	int encryptedCapacity = (int)inputLength + 100;
	u8* encrypted = KLBNEWA(u8, encryptedCapacity);
	int encryptedLength = CPFInterface::getInstance().platform().encryptAES128CBC(
		encrypted, encryptedCapacity, input, inputLength, (const char*)key, keyLength);
	if(encryptedLength < 0) {
		KLBDELETEA(encrypted);
		return false;
	}

	size_t encodedCapacity = (((size_t)encryptedLength + 2) / 3) * 4 + 1;
	*output = KLBNEWA(char, encodedCapacity);
	u32 encodedLength;
	KLBNetAPI_encodeBase64((const char*)encrypted, encryptedLength, *output, &encodedLength);
	bool result = encodedLength != 0;
	KLBDELETEA(encrypted);
	return result;
}

bool
CKLBNetAPI::decryptFromBase64(const char* input, u8** output)
{
	CKLBNetAPIKeyChain& keyChain = CKLBNetAPIKeyChain::getInstance();
	*output = KLBNEWA(u8, keyChain.m_macKeyLength);

	char* decoded = NULL;
	if(!decodeBase64(input, strlen(input), &decoded)) return false;

	for(size_t index = 0; index < keyChain.m_macKeyLength; index++) {
		(*output)[index] = keyChain.m_payloadCipherKey[index] ^ decoded[index];
	}
	KLBDELETEA(decoded);
	return true;
}

// Allocates the plain buffer for a Base64 payload and returns the decoded
// byte count reported by the shared decoder.
s64
CKLBNetAPI::decodeBase64(const char* input, size_t inputLength, char** output)
{
	*output = KLBNEWA(char, inputLength + 1);
	u32 decodedLength;
	KLBNetAPI_decodeBase64(input, *output, &decodedLength);
	return decodedLength;
}

// Allocates the Base64 buffer for a binary payload and returns the encoded
// character count reported by the shared encoder.
s64
CKLBNetAPI::encodeBase64(const char* input, size_t inputLength, char** output)
{
	size_t encodedCapacity = ((inputLength + 2) / 3) * 4 + 1;
	*output = KLBNEWA(char, encodedCapacity);
	u32 encodedLength;
	KLBNetAPI_encodeBase64(input, inputLength, *output, &encodedLength);
	return encodedLength;
}

bool
CKLBNetAPI::publicKeyEncryptThenBase64(const u8* input, int inputLength,
									   char** output)
{
	char encrypted[1000];
	memset(encrypted, 0, sizeof(encrypted));
	int encryptedLength = CPFInterface::getInstance().platform().publicKeyEncrypt(
		(u8*)input, inputLength, (u8*)encrypted, sizeof(encrypted));
	if(encryptedLength <= 0) {
		return false;
	}
	return encodeBase64(encrypted, encryptedLength, output) != 0;
}

bool
CKLBNetAPI::decodeBase64ThenDecrypt(const char* input, size_t inputLength,
									const u8* key, size_t keyLength,
									char** output)
{
	char* decoded = NULL;
	s64 decodedLength = (s32)decodeBase64(input, inputLength, &decoded);
	if(!decodedLength) {
		return false;
	}

	s64 decryptedCapacity = decodedLength + 100;
	*output = KLBNEWA(char, decryptedCapacity);
	int decryptedLength = CPFInterface::getInstance().platform().decryptAES128CBC(
		(u8*)*output, decryptedCapacity, decoded, decodedLength,
		(const char*)key, keyLength);
	char* decrypted = *output;
	decrypted[decryptedLength] = 0;
	KLBDELETEA(decoded);
	return decryptedLength >= 0;
}

bool
CKLBNetAPI::buildLoginRequest(const char* loginKey,
							  const char* loginPassword,
							  char** output)
{
	CKLBNetAPIKeyChain& keyChain = CKLBNetAPIKeyChain::getInstance();
	const u8* payloadCipherKey = keyChain.m_payloadCipherKey;
	int payloadCipherKeyLength = keyChain.m_payloadCipherKeyLength;

	char* encodedKey = NULL;
	if(!publicKeyEncryptThenBase64(payloadCipherKey, payloadCipherKeyLength,
								   &encodedKey)) {
		return false;
	}

	char requestBuffer[1000];
	sprintf(requestBuffer, "%s, %s, %s, %s, %s",
			m_connection.url, m_connection.applicationID,
			m_connection.consumerKey, m_bundleVersion, m_region);
	char* integrityInfo =
		CPFInterface::getInstance().platform().getDeviceIntegrityInfo(
			requestBuffer);
	char* encodedIntegrity = NULL;
	if(!encodeBase64(integrityInfo, strlen(integrityInfo),
					 &encodedIntegrity)) {
		KLBDELETEA(encodedKey);
		KLBDELETEA(encodedIntegrity);
		return false;
	}

	// The buffer is value initialized so the JSON body can be assembled by
	// appending to an empty string.
	int authDataCapacity = strlen(encodedIntegrity) + 1024;
	char* authData = new char[authDataCapacity]();
	strcat(authData, "{");
	sprintf(authData + strlen(authData), "\"%d\":\"%s\",", 1, loginKey);
	sprintf(authData + strlen(authData), "\"%d\":\"%s\",", 2, loginPassword);
	sprintf(authData + strlen(authData), "\"%d\":\"%s\",", 3, encodedIntegrity);
	strcpy(authData + strlen(authData) - 1, "}");

	char* encryptedAuthData = NULL;
	if(!encryptThenBase64(authData, strlen(authData),
						 payloadCipherKey, payloadCipherKeyLength,
						 &encryptedAuthData)) {
		KLBDELETEA(encodedKey);
		KLBDELETEA(encodedIntegrity);
		KLBDELETEA(encryptedAuthData);
		KLBDELETEA(authData);
		return false;
	}

	int requestCapacity = strlen(encryptedAuthData) + 1024;
	*output = KLBNEWA(char, requestCapacity);
	sprintf(*output,
			"request_data={\"dummy_token\":\"%s\",\"auth_data\":\"%s\"}",
			encodedKey, encryptedAuthData);
	KLBDELETEA(encodedKey);
	KLBDELETEA(encodedIntegrity);
	KLBDELETEA(encryptedAuthData);
	KLBDELETEA(authData);
	return true;
}

;
enum {
	// Command Values定義
	NETAPI_STARTUP,
	NETAPI_LOGIN,
	NETAPI_LOGOUT,
	NETAPI_SEND,
	NETAPI_CANCEL,
	NETAPI_CANCEL_ALL,
};

static IFactory::DEFCMD cmd[] = {
	{"NETAPI_STARTUP",			NETAPI_STARTUP		},
	{"NETAPI_LOGIN",				NETAPI_LOGIN		},
	{"NETAPI_LOGOUT",			NETAPI_LOGOUT		},
	{"NETAPI_SEND",				NETAPI_SEND		},
	{"NETAPI_CANCEL",			NETAPI_CANCEL	},
	{"NETAPI_CANCEL_ALL",		NETAPI_CANCEL_ALL },

	//
	// Callback constants
	//
	{"NETAPIMSG_SESSION_CANCELED",	NETAPIMSG_CONNECTION_CANCELED },
	{"NETAPIMSG_CONNECTION_FAILED",	NETAPIMSG_CONNECTION_FAILED },
	{"NETAPIMSG_INVITE_FAILED",		NETAPIMSG_INVITE_FAILED },
	{"NETAPIMSG_STARTUP_FAILED",	NETAPIMSG_STARTUP_FAILED },
	{"NETAPIMSG_SERVER_ERROR",		NETAPIMSG_SERVER_ERROR		},
	{"NETAPIMSG_SERVER_TIMEOUT",	NETAPIMSG_SERVER_TIMEOUT	},
	{"NETAPIMSG_LOGIN_SUCCESS",		NETAPIMSG_LOGIN_SUCCESS },
	{"NETAPIMSG_LOGIN_FAILED",		NETAPIMSG_LOGIN_FAILED },
	{"NETAPIMSG_REQUEST_SUCCESS",	NETAPIMSG_REQUEST_SUCCESS	},
	{"NETAPIMSG_REQUEST_FAILED",	NETAPIMSG_REQUEST_FAILED },
	{"NETAPIMSG_STARTUP_SUCCESS",	NETAPIMSG_STARTUP_SUCCESS },
	{"NETAPIMSG_INVITE_SUCCESS",	NETAPIMSG_INVITE_SUCCESS },
	{"NETAPIMSG_UNKNOWN",			NETAPIMSG_UNKNOWN },
	{0, 0}
};

static CKLBTaskFactory<CKLBNetAPI> factory("HTTP_API", CLS_KLBNETAPI, cmd);

enum {
	ARG_CALLBACK	= 1,
	ARG_REQUIRE     = ARG_CALLBACK,
};

CKLBNetAPI::Session::Session(int requestID_, int state_, int timeout_)
{
	requestID    = requestID_;
	state        = (State)state_;
	timeout      = timeout_;
	canceled     = false;
	started      = false;
	checkVersion = false;
	elapsed      = 0;
	connection   = NetworkManager::createConnection();
}

CKLBNetAPI::Session::~Session()
{
	NetworkManager::releaseConnection(connection);
	connection = NULL;
}


CKLBNetAPI::CKLBNetAPI()
: CKLBLuaTask           ()
, m_callback            (NULL)
, m_connection          ()
, m_language            (NULL)
, m_os                  (NULL)
, m_osVersion           (NULL)
, m_timeZone            (NULL)
, m_platformType        (0)
, m_bundleVersion       (NULL)
, m_nonce               (0)
, m_requestSerial       (0)
, m_authorizeToken      (NULL)
, m_userID              (NULL)
, m_loginKey            (NULL)
, m_loginPassword       (NULL)
{
}

CKLBNetAPI::~CKLBNetAPI() 
{
	// Done in Die()
}

u32 
CKLBNetAPI::getClassID() 
{
	return CLS_KLBNETAPI;
}

void
CKLBNetAPI::execute(u32 deltaT)
{
	if(!m_pendingRequests.empty()) {
		m_activeRequests.insert(m_activeRequests.end(),
								m_pendingRequests.begin(), m_pendingRequests.end());
		m_pendingRequests.clear();
	}

	std::list<Session*>::iterator request = m_activeRequests.begin();
	while(request != m_activeRequests.end()) {
		if(processRequest(*request, deltaT)) {
			++request;
		} else {
			KLBDELETE(*request);
			request = m_activeRequests.erase(request);
		}
	}

	request = m_activeRequests.begin();
	while(request != m_activeRequests.end()) {
		if((*request)->canceled) {
			lua_callback((*request)->requestID, NETAPIMSG_CONNECTION_CANCELED,
						 -1, NULL, 0);
			KLBDELETE(*request);
			request = m_activeRequests.erase(request);
		} else {
			++request;
		}
	}
}

bool
CKLBNetAPI::cancel(int requestID)
{
	std::list<Session*>::iterator request;
	for(request = m_activeRequests.begin();
		request != m_activeRequests.end(); ++request) {
		if((*request)->requestID == requestID) {
			(*request)->canceled = true;
			return true;
		}
	}
	for(request = m_pendingRequests.begin();
		request != m_pendingRequests.end(); ++request) {
		if((*request)->requestID == requestID) {
			(*request)->canceled = true;
			return true;
		}
	}
	return false;
}

void
CKLBNetAPI::cancelRequests()
{
	std::list<Session*>::iterator request;
	for(request = m_activeRequests.begin(); request != m_activeRequests.end(); ++request) {
		(*request)->canceled = true;
	}
	for(request = m_pendingRequests.begin(); request != m_pendingRequests.end(); ++request) {
		(*request)->canceled = true;
	}
}

bool
CKLBNetAPI::matchesClientVersion(const char* serverVersion,
								 const char* clientVersion,
								 size_t comparisonLength)
{
	while(*clientVersion == ' ') {
		clientVersion++;
	}
	size_t clientLength = strlen(clientVersion);
	if(comparisonLength) {
		clientLength = comparisonLength;
	}

	if(strncmp(serverVersion, clientVersion, clientLength)) {
		return false;
	}
	unsigned char terminator = serverVersion[clientLength];
	return terminator == 0 || terminator == '\n' || terminator == '\r'
		|| comparisonLength != 0;
}

char*
CKLBNetAPI::copyServerVersion(const char* serverVersion)
{
	int length = 0;
	while(serverVersion[length] && serverVersion[length] != '\n'
		  && serverVersion[length] != '\r') {
		length++;
	}
	char* version = KLBNEWA(char, length + 1);
	strncpy(version, serverVersion, length);
	version[length] = 0;
	return version;
}

bool
CKLBNetAPI::isCanceled(Session* request)
{
	if(!request->canceled) {
		return true;
	}
	lua_callback(request->requestID, NETAPIMSG_CONNECTION_CANCELED,
				 -1, NULL, 0);
	return false;
}

bool
CKLBNetAPI::processRequest(Session* request, u32 deltaT)
{
	if(!isCanceled(request)) {
		return false;
	}

	if(!request->started) {
		lua_callback(request->requestID, NETAPIMSG_CONNECTION_FAILED,
					 -1, NULL, 0);
		return false;
	}

	request->elapsed += deltaT;
	if(!request->connection->isDataComplete()) {
		if((s32)request->timeout > 0 && (s32)request->elapsed > (s32)request->timeout) {
			lua_callback(request->requestID, NETAPIMSG_SERVER_TIMEOUT,
						 -1, NULL, 0);
			return false;
		}
		return true;
	}

	if(request->connection->m_update) {
		cancelRequests();
		CKLBLuaEnv::getInstance().intoMaintenance();
		return false;
	}
	if(request->connection->m_maintenance) {
		cancelRequests();
		CKLBLuaEnv::getInstance().intoUpdate();
		return false;
	}

	u8* body = request->connection->getRecvResource();
	int connectionState = request->connection->getHttpState();
	int dataSize = 0;

	if(!body || connectionState != 0) {
		int httpStatus = request->connection->getHttpStatus();
		int requestID = request->requestID;
		int result = (((u32)(httpStatus - 500) < 100u) || httpStatus == 204)
			? NETAPIMSG_REQUEST_SUCCESS : NETAPIMSG_SERVER_ERROR;
		lua_callback(requestID, result, httpStatus, NULL, connectionState);
		return false;
	}

	body = request->connection->getRecvResource();
	if(m_connection.versionUpdateCallback && request->connection->m_pServerVersion) {
			const char* serverVersion = request->connection->m_pServerVersion;
			size_t comparisonLength = 0;
			if(request->checkVersion) {
				size_t serverVersionLength = strlen(serverVersion);
				size_t index = 0;
				for(;;) {
					comparisonLength = 0;
					if(index >= serverVersionLength) break;
					char character = serverVersion[index++];
					comparisonLength = index;
					if(character == '.') break;
				}
			}

			if(!matchesClientVersion(serverVersion,
									 m_connection.clientVersion,
									 comparisonLength)) {
				char* version = copyServerVersion(serverVersion);
				cancelRequests();
				CKLBScriptEnv::getInstance().call_netAPI_versionUp(
					m_connection.versionUpdateCallback, this,
					m_connection.clientVersion, version);
				KLBDELETEA(version);
				return false;
			}
	}
	if(body) dataSize = (int)request->connection->getSize();

	bool keepRequest = true;
	CKLBJsonItem* root = NULL;
	switch(request->state) {
	case Session::STATE_INITIAL_HANDSHAKE:
		{
			root = getJsonTree((const char*)body, dataSize);
			if(root && get_token(root)) {
				KLBDELETE(root);
				root = NULL;
				startRequest(request, "login/startUp", 0);
				request->elapsed = 0;
				request->state = Session::STATE_STARTUP;
				break;
			}
			lua_callback(request->requestID, NETAPIMSG_SERVER_ERROR,
						 request->connection->getHttpStatus(), root, 0);
			keepRequest = false;
			break;
		}

	case Session::STATE_STARTUP:
		{
			root = getJsonTree((const char*)body, dataSize);
			int httpStatus = request->connection->getHttpStatus();
			if(root && httpStatus == 200) {
				lua_callback(request->requestID, NETAPIMSG_STARTUP_SUCCESS, 200, root, 0);
			} else {
				lua_callback(request->requestID,
						 root ? NETAPIMSG_STARTUP_FAILED : NETAPIMSG_SERVER_ERROR,
						 httpStatus, root, 0);
			}
			keepRequest = false;
			break;
		}

	case Session::STATE_LOGIN_HANDSHAKE:
		{
			root = getJsonTree((const char*)body, dataSize);
			if(root && get_token(root)) {
				KLBDELETE(root);
				root = NULL;
				request->state = Session::STATE_LOGIN_REQUEST;
				request->elapsed = 0;
				break;
			}
			lua_callback(request->requestID, NETAPIMSG_SERVER_ERROR,
						 request->connection->getHttpStatus(), root, 0);
			keepRequest = false;
			break;
		}

	case Session::STATE_LOGIN_REQUEST:
		{
			IPlatformRequest& platform = CPFInterface::getInstance().platform();
			if(!platform.readyDevID()) {
				platform.logging(
				"could not retrieve device id. proceed anyway\n");
			}
		}
		startRequest(request, "login/login", 0);
		request->state = Session::STATE_LOGIN_RESPONSE;
		request->elapsed = 0;
		break;

	case Session::STATE_LOGIN_RESPONSE:
		{
			root = getJsonTree((const char*)body, dataSize);
			int result = NETAPIMSG_SERVER_ERROR;
			if(root) {
				result = NETAPIMSG_LOGIN_FAILED;
				if(get_token(root) && request->connection->getHttpStatus() == 200) {
					installUserIDHeader();
					result = NETAPIMSG_LOGIN_SUCCESS;
				}
			}
			lua_callback(request->requestID, result,
						 request->connection->getHttpStatus(), root, 0);
			keepRequest = false;
			break;
		}

	case Session::STATE_API_REQUEST:
		{
			root = getJsonTree((const char*)body, dataSize);
			int httpStatus = request->connection->getHttpStatus();
			bool hasRoot = (root != NULL);
			int result = (httpStatus == 200) ? NETAPIMSG_REQUEST_SUCCESS
						 : (hasRoot ? NETAPIMSG_REQUEST_FAILED : NETAPIMSG_SERVER_ERROR);
			lua_callback(request->requestID, result, httpStatus, root, 0);
			if(hasRoot) {
				KLBDELETE(root);
			}
			return false;
		}

	}

	if(root) KLBDELETE(root);
	return keepRequest;
}

void
CKLBNetAPI::die()
{
	std::list<Session*>::iterator request;
	for(request = m_activeRequests.begin(); request != m_activeRequests.end(); ++request) {
		KLBDELETE(*request);
	}
	m_activeRequests.clear();
	for(request = m_pendingRequests.begin(); request != m_pendingRequests.end(); ++request) {
		KLBDELETE(*request);
	}
	m_pendingRequests.clear();

	KLBDELETEA(m_connection.url);
	KLBDELETEA(m_bundleVersion);
	KLBDELETEA(m_connection.clientVersion);
	KLBDELETEA(m_connection.consumerKey);
	KLBDELETEA(m_connection.applicationID);
	KLBDELETEA(m_callback);
	KLBDELETEA(m_language);
	KLBDELETEA(m_region);
	KLBDELETEA(m_connection.versionUpdateCallback);
	KLBDELETEA(m_os);
	KLBDELETEA(m_osVersion);
	KLBDELETEA(m_timeZone);
	KLBDELETEA(m_authorizeToken);
	KLBDELETEA(m_userID);
	KLBDELETEA(m_loginKey);
	KLBDELETEA(m_loginPassword);

	// The first two header entries are static strings and the authorization
	// entry aliases m_authorizeHeader. The remaining populated entries own the
	// formatted strings assembled for the connection.
	for(u32 index = 2; index < 14; index++) {
		if(index != 11) KLBDELETEA(m_headerContext[index]);
	}
	KLBDELETEA(m_headerContext);
}

CKLBNetAPI*
CKLBNetAPI::create( CKLBTask* pParentTask,
					const char* url,
					const char* clientVersion,
					const char* consumerKey,
					const char* applicationID,
					const char* callback,
					const char* language,
					const char* region,
					const char* versionUpdateCallback)
{
	CKLBNetAPI* pTask = KLBNEW(CKLBNetAPI);
    if(!pTask) { return NULL; }

	if(!pTask->init(pParentTask, url, clientVersion, consumerKey,
					applicationID, callback, language, region,
					versionUpdateCallback)) {
		KLBDELETE(pTask);
		return NULL;
	}
	return pTask;
}

bool
CKLBNetAPI::init(	CKLBTask* pTask,
					const char* url,
					const char* clientVersion,
					const char* consumerKey,
					const char* applicationID,
					const char* callback,
					const char* language,
					const char* region,
					const char* versionUpdateCallback)
{
	m_connection.url = CKLBUtility::copyString(url);
	m_connection.clientVersion = CKLBUtility::copyString(clientVersion);
	m_connection.consumerKey = CKLBUtility::copyString(consumerKey);
	m_connection.applicationID = CKLBUtility::copyString(applicationID);
	m_callback = callback ? CKLBUtility::copyString(callback) : NULL;
	m_region = CKLBUtility::copyString(region);
	m_language = CKLBUtility::copyString(language);
	m_connection.versionUpdateCallback =
		versionUpdateCallback
			? CKLBUtility::copyString(versionUpdateCallback)
			: NULL;

	IPlatformRequest& platform = CPFInterface::getInstance().platform();
	const char** platformInfo =
		CKLBUtility::splitString(platform.getPlatform(), ';');
	m_os = CKLBUtility::copyString(platformInfo[0]);
	m_osVersion = CKLBUtility::copyString(platformInfo[1]);
	m_timeZone = CKLBUtility::copyString(platformInfo[2]);
	CKLBUtility::deleteSplitString(platformInfo);
	m_bundleVersion = CKLBUtility::copyString(platform.getBundleVersion());

	CKLBNetAPIKeyChain& keyChain = CKLBNetAPIKeyChain::getInstance();
	keyChain.setRegion(region);
	keyChain.setBundleVersion(m_bundleVersion);
	keyChain.setClient(clientVersion);
	keyChain.setConsumernKey(consumerKey);
	keyChain.setAppID(applicationID);
	keyChain.setLanguage(m_language);

	if(m_connection.url) {
		if(m_bundleVersion && m_connection.clientVersion
		&& m_connection.consumerKey && m_connection.applicationID && m_os
		&& m_osVersion && m_timeZone && m_language) {
			m_headerContext = KLBNEWA(const char*, 15);

			m_platformType = 0;
			if(strstr(m_os, "iOS")) {
				m_platformType = 1;
			} else if(strstr(m_os, "Android")) {
				m_platformType = 2;
			}

			for(int index = 0; index < 14; index++) {
				m_headerContext[index] = NULL;
			}
			m_headerContext[0] = "API-Model: straightforward";
			m_headerContext[1] = "Debug: 1";
			m_headerContext[14] = NULL;

			char header[4096];
#define SET_HEADER(index, format, value) \
		do { \
			sprintf(header, format, value); \
			m_headerContext[index] = CKLBUtility::copyString(header); \
		} while(0)
		SET_HEADER(2, "Bundle-Version: %s", m_bundleVersion);
		SET_HEADER(3, "Client-Version: %s", m_connection.clientVersion);
		SET_HEADER(4, "OS-Version: %s", m_osVersion);
		SET_HEADER(5, "OS: %s", m_os);
		SET_HEADER(6, "Platform-Type: %d", m_platformType);
		SET_HEADER(7, "Application-ID: %s", m_connection.applicationID);
		SET_HEADER(8, "Time-Zone: %s", m_timeZone);
		SET_HEADER(9, "LANG: %s", m_language);
		SET_HEADER(10, "Region: %s", m_region);
#undef SET_HEADER

			// Registration only happens when every formatted connection
			// header was assembled; otherwise the whole context is released.
			u32 headerIndex;
			for(headerIndex = 2; headerIndex <= 10; headerIndex++) {
				if(!m_headerContext[headerIndex]) break;
			}
			if(headerIndex > 10) {
				strcpy(m_authorizeHeader, "Authorize: ");
				m_authorizePrefixLength = strlen(m_authorizeHeader);
				return regist(pTask, P_INPUT);
			}

			for(headerIndex = 2; headerIndex <= 10; headerIndex++) {
				KLBDELETEA(m_headerContext[headerIndex]);
			}
			KLBDELETEA(m_headerContext);
		}
		KLBDELETEA(m_connection.url);
	}

	KLBDELETEA(m_bundleVersion);
	KLBDELETEA(m_connection.clientVersion);
	KLBDELETEA(m_connection.consumerKey);
	KLBDELETEA(m_connection.applicationID);
	KLBDELETEA(m_callback);
	KLBDELETEA(m_language);
	KLBDELETEA(m_region);
	KLBDELETEA(m_connection.versionUpdateCallback);
	KLBDELETEA(m_os);
	KLBDELETEA(m_osVersion);
	KLBDELETEA(m_timeZone);
	return false;
}

bool
CKLBNetAPI::initScript(CLuaState& lua)
{
	int argc = lua.numArgs();
	if((u32)(argc - 5) >= 5) return false;

	const char* url = lua.getString(1);
	const char* clientVersion = lua.getString(3);
	const char* consumerKey = lua.getString(2);
	const char* applicationID = lua.getString(4);
	const char* callback = lua.getString(5);
	const char* language = lua.getString(7);
	const char* region = (argc >= 8) ? lua.getString(8) : DEFAULT_REGION;
	const char* versionUpdateCallback =
		(argc >= 9) ? lua.getString(9) : NULL;
	return init(NULL, url, clientVersion, consumerKey, applicationID,
				callback, language, region, versionUpdateCallback);
}

CKLBJsonItem *
CKLBNetAPI::getJsonTree(const char * json_string, u32 dataLen)
{
	CKLBJsonItem * pRoot = CKLBJsonItem::ReadJsonData((const char *)json_string, dataLen);

	return pRoot;
}

bool
CKLBNetAPI::get_token(CKLBJsonItem* root)
{
	CKLBJsonItem* response = root->searchChild("response_data");
	if(!response || response->getType() != CKLBJsonItem::J_MAP) return false;

	CKLBJsonItem* dummyToken = response->searchChild("dummy_token");
	if(dummyToken && dummyToken->getType() == CKLBJsonItem::J_STRING) {
		CKLBNetAPIKeyChain& keyChain = CKLBNetAPIKeyChain::getInstance();
		u8* decoded;
		if(!decryptFromBase64(dummyToken->getString(), &decoded)) {
			return false;
		}
		u8* decodedBytes = decoded;
		if(keyChain.m_macKeyLength == sizeof(keyChain.m_requestMACKey)) {
			memcpy(keyChain.m_requestMACKey, decodedBytes, keyChain.m_macKeyLength);
		}
		KLBDELETEA(decodedBytes);
	}

	CKLBJsonItem* token = response->searchChild("authorize_token");
	if(!token || token->getType() != CKLBJsonItem::J_STRING) {
		return false;
	}
	const char* tokenString = token->getString();
	if(!tokenString) return false;

	KLBDELETEA(m_authorizeToken);
	m_authorizeToken = CKLBUtility::copyString(tokenString);
	CKLBNetAPIKeyChain::getInstance().setToken(m_authorizeToken);

	CKLBJsonItem* userID = response->searchChild("user_id");
	if(userID) {
		if(userID->getType() == CKLBJsonItem::J_STRING) {
			m_userID = CKLBUtility::copyString(userID->getString());
		} else if(userID->getType() == CKLBJsonItem::J_INT) {
			char value[64];
			CKLBUtility::numStringS64(value, userID->getInt64());
			m_userID = CKLBUtility::copyString(value);
		}
	}
	return true;
}

bool
CKLBNetAPI::installUserIDHeader()
{
	char userIDHeader[128];
	sprintf(userIDHeader, "User-ID: %s", m_userID);

	CKLBNetAPIKeyChain::getInstance().setUserID(m_userID);
	const char* header = CKLBUtility::copyString(userIDHeader);
	if(!header) return false;

	KLBDELETEA(m_headerContext[12]);
	m_headerContext[12] = header;
	return true;
}

bool
CKLBNetAPI::startRequest(Session* request, const char* path, int command)
{
	const char* form[2];
	char requestBody[1024];
	char deviceParameter[1024];
	requestBody[0] = 0;
	deviceParameter[0] = 0;

	if(CPFInterface::getInstance().platform().getDevID(requestBody, 512)) {
		sprintf(deviceParameter, ",\"devtoken\":\"%s\"", requestBody);
	}

	CKLBNetAPIKeyChain& keyChain = CKLBNetAPIKeyChain::getInstance();
	const u8* requestMACKey = keyChain.m_requestMACKey;
	size_t requestMACKeyLength = keyChain.m_macKeyLength;
	char* encryptedLoginKey = NULL;
	char* encryptedPassword = NULL;
	if(!encryptThenBase64(m_loginKey, strlen(m_loginKey),
						 requestMACKey, requestMACKeyLength,
						 &encryptedLoginKey)) {
		return false;
	}
	if(!encryptThenBase64(m_loginPassword, strlen(m_loginPassword),
						 requestMACKey, requestMACKeyLength,
						 &encryptedPassword)) {
		return false;
	}

	sprintf(requestBody,
			"request_data={\"login_key\":\"%s\",\"login_passwd\":\"%s\"%s}",
			encryptedLoginKey, encryptedPassword, deviceParameter);
	KLBDELETEA(encryptedLoginKey);
	KLBDELETEA(encryptedPassword);
	form[0] = requestBody;
	form[1] = NULL;
	request->connection->reuse();
	request->connection->setForm(form);
	prepareAuthorizationHeader(command, m_authorizeToken);
	installRequestHeaders(request->connection);

	sprintf(requestBody, "%s/%s", m_connection.url, path);
	request->started = request->connection->httpPOST(requestBody, false);
	return request->started;
}

bool
CKLBNetAPI::prepareAuthorizationHeader(int command, const char* token)
{
	(void)command;
	time_t requestTime;
	time(&requestTime);
	m_nonce++;

	const char* authorization[6];
	char consumerKey[128];
	char timeStamp[128];
	char nonce[128];
	char tokenEntry[256];
	sprintf(consumerKey, "consumerKey=%s", m_connection.consumerKey);
	sprintf(timeStamp, "timeStamp=%d", (int)requestTime);
	sprintf(nonce, "nonce=%d", m_nonce);

	int count = 3;
	if(token) {
		sprintf(tokenEntry, "token=%s", token);
		authorization[count++] = tokenEntry;
	}
	authorization[0] = consumerKey;
	authorization[1] = timeStamp;
	authorization[2] = "version=1.1";
	authorization[count++] = nonce;
	authorization[count] = NULL;

	CKLBUtility::URLencode(
		m_authorizeHeader + m_authorizePrefixLength,
		sizeof(m_authorizeHeader) - m_authorizePrefixLength,
		authorization);
	m_headerContext[11] = m_authorizeHeader;
	return true;
}

bool
CKLBNetAPI::installRequestHeaders(CKLBHTTPInterface* connection)
{
	const char* headers[17];
	int count = 0;
	for(int index = 0; index < 14; index++) {
		if(m_headerContext[index]) {
			headers[count++] = m_headerContext[index];
		}
	}

	char bundleID[64] = "X-BUNDLE-ID: ";
	int length = strlen(bundleID);
	strncat(bundleID, CPFInterface::getInstance().platform().getBundleId(),
			(int)sizeof(bundleID) - length - 1);
	bundleID[sizeof(bundleID) - 1] = 0;
	headers[count++] = bundleID;

	char* requestID = CPFInterface::getInstance().platform().createRequestIdHeader();
	headers[count++] = requestID;
	headers[count] = NULL;
	bool result = connection->setHeader(headers);
	KLBDELETEA(requestID);
	return result;
}

// Allocates the next identified request session and queues it for the
// following execute() pass.
CKLBNetAPI::Session*
CKLBNetAPI::newSession(Session::State state, int timeout)
{
	int nextRequestID = ++m_requestSerial;
	Session* request = KLBNEWC(Session, (nextRequestID, state, timeout));
	m_pendingRequests.push_back(request);
	return request;
}

bool
CKLBNetAPI::enqueueLogin(const char* loginKey, const char* loginPassword,
						 int timeout, int* requestID, Session::State state)
{
	KLBDELETEA(m_loginKey);
	KLBDELETEA(m_loginPassword);
	m_loginKey = CKLBUtility::copyString(loginKey);
	m_loginPassword = CKLBUtility::copyString(loginPassword);

	Session* request = newSession(state, timeout);
	request->checkVersion = true;
	request->connection->reuse();

	const char* authorization[5];
	char consumerKey[128];
	char timeStamp[128];
	char nonce[128];
	const char* form[2];
	char url[1024];
	time_t requestTime;
	char* requestBody = NULL;
	if(!buildLoginRequest(m_loginKey, m_loginPassword, &requestBody)) {
		return false;
	}
	form[0] = requestBody;
	form[1] = NULL;
	request->connection->setForm(form);
	KLBDELETEA(requestBody);

	sprintf(url, "%s/login/authkey", m_connection.url);

	time(&requestTime);
	m_nonce++;

	sprintf(consumerKey, "consumerKey=%s", m_connection.consumerKey);
	sprintf(timeStamp, "timeStamp=%d", (int)requestTime);
	sprintf(nonce, "nonce=%d", m_nonce);
	authorization[0] = consumerKey;
	authorization[1] = timeStamp;
	authorization[2] = "version=1.1";
	authorization[3] = nonce;
	authorization[4] = NULL;
	CKLBUtility::URLencode(
		m_authorizeHeader + m_authorizePrefixLength,
		sizeof(m_authorizeHeader) - m_authorizePrefixLength,
		authorization);
	m_headerContext[11] = m_authorizeHeader;

	installRequestHeaders(request->connection);
	request->started = request->connection->httpPOST(
		url, false, NULL, false);
	*requestID = request->requestID;
	return true;
}

bool
CKLBNetAPI::enqueueSend(const char* endPoint, const char* json, int timeout,
						bool checkVersion, int* requestID, bool absolute,
						const char* specialKey, bool hasSessionMACKey)
{
	Session* request = newSession(Session::STATE_API_REQUEST, timeout);
	request->checkVersion = checkVersion;
	request->connection->reuse();

	size_t jsonLength = strlen(json);
	char* body = KLBNEWA(char, jsonLength + 14);
	sprintf(body, "%s%s", "request_data=", json);
	{
		const char* form[] = { body, NULL };
		request->connection->setForm(form);
	}
	KLBDELETEA(body);

	char url[1024];
	if(absolute) {
		memcpy(url, endPoint, strlen(endPoint) + 1);
	} else {
		sprintf(url, "%s%s", m_connection.url, endPoint);
	}
	prepareAuthorizationHeader(NETAPI_SEND, m_authorizeToken);
	installRequestHeaders(request->connection);
	request->started = request->connection->httpPOST(
		url, false, specialKey, hasSessionMACKey);
	*requestID = request->requestID;
	return request != NULL;
}

int
CKLBNetAPI::commandScript(CLuaState& lua)
{
	int argc = lua.numArgs();
	if(argc < 2) {
		lua.retBoolean(false);
		return 1;
	}

	int cmd = lua.getInt(2);
	switch(cmd) {
	case NETAPI_STARTUP:
		{
			if((u32)(argc - 4) >= 5) {
				lua.retBoolean(false);
				break;
			}

			const char* loginKey = lua.getString(3);
			const char* loginPassword = lua.getString(4);
			int timeout = lua.getInt(6);
			size_t requestKeyLength;
			const u8* requestKey = (const u8*)lua.getString(7, &requestKeyLength);
			size_t payloadKeyLength;
			const u8* payloadKey = (const u8*)lua.getString(8, &payloadKeyLength);
			CKLBNetAPIKeyChain& keyChain = CKLBNetAPIKeyChain::getInstance();
			if(requestKeyLength != keyChain.m_macKeyLength
			|| payloadKeyLength != keyChain.m_payloadCipherKeyLength) {
				lua.retBoolean(false);
				break;
			}
			if((size_t)(int)requestKeyLength == requestKeyLength) {
				memcpy(keyChain.m_requestMACKey, requestKey, requestKeyLength);
			}
			if((int)payloadKeyLength == keyChain.m_payloadCipherKeyLength) {
				memcpy(keyChain.m_payloadCipherKey, payloadKey,
					   keyChain.m_payloadCipherKeyLength);
			}

			int requestID;
			if(enqueueLogin(loginKey, loginPassword, timeout, &requestID,
							Session::STATE_INITIAL_HANDSHAKE)) {
				lua.retInt(requestID);
			} else {
				lua.retBoolean(false);
			}
		}
		break;

	case NETAPI_LOGIN:
		{
			if((argc & ~3) != 4) {
				lua.retBoolean(false);
				break;
			}

			const char* loginKey = lua.getString(3);
			const char* loginPassword = lua.getString(4);
			int timeout = lua.getInt(5);
			size_t requestKeyLength;
			const u8* requestKey = (const u8*)lua.getString(6, &requestKeyLength);
			size_t payloadKeyLength;
			const u8* payloadKey = (const u8*)lua.getString(7, &payloadKeyLength);
			CKLBNetAPIKeyChain& keyChain = CKLBNetAPIKeyChain::getInstance();
			if(requestKeyLength != keyChain.m_macKeyLength
			|| payloadKeyLength != keyChain.m_payloadCipherKeyLength) {
				lua.retBoolean(false);
				break;
			}
			if((size_t)(int)requestKeyLength == requestKeyLength) {
				memcpy(keyChain.m_requestMACKey, requestKey, requestKeyLength);
			}
			if((int)payloadKeyLength == keyChain.m_payloadCipherKeyLength) {
				memcpy(keyChain.m_payloadCipherKey, payloadKey,
					   keyChain.m_payloadCipherKeyLength);
			}

			int requestID;
			if(enqueueLogin(loginKey, loginPassword, timeout, &requestID,
							Session::STATE_LOGIN_HANDSHAKE)) {
				lua.retInt(requestID);
			} else {
				lua.retBoolean(false);
			}
		}
		break;

	case NETAPI_LOGOUT:
		lua.retBoolean(false);
		break;

	case NETAPI_CANCEL:
		{
			if(argc != 3) {
				lua.retBoolean(false);
				break;
			}
			int requestID = lua.getInt(3);
			bool found = false;
			std::list<Session*>::iterator request;
			for(request = m_activeRequests.begin();
				request != m_activeRequests.end(); ++request) {
				if((*request)->requestID == requestID) {
					(*request)->canceled = true;
					found = true;
					break;
				}
			}
			if(!found) {
				for(request = m_pendingRequests.begin();
					request != m_pendingRequests.end(); ++request) {
					if((*request)->requestID == requestID) {
						(*request)->canceled = true;
						found = true;
						break;
					}
				}
			}
			lua.retBoolean(found);
		}
		break;

	case NETAPI_CANCEL_ALL:
		if(argc != 2) {
			lua.retBoolean(false);
			break;
		}
		{
			std::list<Session*>::iterator request;
			for(request = m_activeRequests.begin();
				request != m_activeRequests.end(); ++request) {
				(*request)->canceled = true;
			}
			for(request = m_pendingRequests.begin();
				request != m_pendingRequests.end(); ++request) {
				(*request)->canceled = true;
			}
			lua.retBoolean(true);
		}
		break;

	case NETAPI_SEND:
		{
			if((u32)(argc - 3) >= 8) {
				lua.retBoolean(false);
				break;
			}

			const char* endPoint = "/api";
			int timeout = 0;
			bool checkVersion = false;
			const char* specialKey = NULL;
			bool absolute = false;
			bool hasSessionMACKey = false;
			if(argc >= 4) {
				if(lua.getType(4) != LUA_TNIL) {
					endPoint = lua.getString(4);
				}
				if(argc >= 5) {
					timeout = lua.getInt(5);
					if(argc >= 6) {
						if(lua.getType(6) != LUA_TNIL) {
							checkVersion = lua.getBool(6);
						}
						if(argc >= 7) {
							if(lua.getType(7) != LUA_TNIL) {
								absolute = lua.getBool(7);
							}
							if(argc >= 8) {
								if(lua.getType(8) != LUA_TNIL) {
									specialKey = lua.getString(8);
								}
								if(argc >= 9 && lua.getType(9) != LUA_TNIL) {
									size_t keyLength;
									const u8* key = (const u8*)lua.getString(9, &keyLength);
									CKLBNetAPIKeyChain& keyChain =
										CKLBNetAPIKeyChain::getInstance();
									if(key && keyLength == keyChain.m_macKeyLength) {
										hasSessionMACKey = true;
										if((size_t)(int)keyLength == keyLength) {
											memcpy(keyChain.m_sessionMACKey, key, keyLength);
										}
									}
								}
							}
						}
					}
				}
			}

			CKLBUtility::JSON_REPLACE replacements[2];
			time_t requestTime;
			char timeStamp[64];
			time(&requestTime);
			sprintf(timeStamp, "%ld", (long)requestTime);
			replacements[0].key = "timeStamp";
			replacements[0].value = timeStamp;
			replacements[1].key = NULL;
			replacements[1].value = NULL;
			size_t jsonLength;
			lua.retValue(3);
			const char* json = CKLBUtility::lua2json(lua, jsonLength, replacements);
			lua.pop(1);

			int requestID;
			bool result = enqueueSend(endPoint, json, timeout, checkVersion,
									  &requestID, absolute, specialKey,
									  hasSessionMACKey);
			if(result) {
				lua.retInt(requestID);
			} else {
				lua.retBoolean(false);
			}
			KLBDELETEA(json);
		}
		break;

	default:
		lua.retBoolean(false);
		break;
	}
	return 1;
}

bool
CKLBNetAPI::lua_callback(int uniq, int msg, int status, CKLBJsonItem * pRoot, int dataSize)
{
	return CKLBScriptEnv::getInstance().call_netAPI_callback(m_callback, this, uniq, msg, status, pRoot, dataSize);
}
