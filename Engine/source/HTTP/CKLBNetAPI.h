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
#ifndef CKLBNetAPI_h
#define CKLBNetAPI_h

#include "CKLBLuaTask.h"
#include "CKLBHTTPInterface.h"
#include "CKLBJsonItem.h"
#include "CKLBUtility.h"
#include "MultithreadedNetwork.h"
#include <list>

// regionを指定しなかった場合のデフォルトregion値(ISO 3166-1)
#define DEFAULT_REGION "840"    // 北米アメリカ合衆国

//// 指定しそうなデフォルト値をコメントアウトして書いておく。
// #define DEFAULT_REGION "392"    // 日本


enum {
	// メッセージ値定義
	NETAPIMSG_CONNECTION_CANCELED	= -999,	// セッションはキャンセルされた
	NETAPIMSG_CONNECTION_FAILED		= -500,	// 接続に失敗した
	NETAPIMSG_INVITE_FAILED			= -200,
	NETAPIMSG_SERVER_TIMEOUT		= -4,	// サーバとの通信がタイムアウトした
	NETAPIMSG_REQUEST_FAILED		= -3,
	NETAPIMSG_LOGIN_FAILED			= -2,
	NETAPIMSG_SERVER_ERROR			= -1,
	NETAPIMSG_UNKNOWN				= 0,
	NETAPIMSG_LOGIN_SUCCESS			= 2,
	NETAPIMSG_REQUEST_SUCCESS		= 3,	// リクエスト成功ステータス
	NETAPIMSG_STARTUP_SUCCESS		= 100,
	NETAPIMSG_STARTUP_FAILED		= -100,
	NETAPIMSG_INVITE_SUCCESS		= 200,
};

// Native側からAPIタスクにコマンドを発行するためのsingleton.
// 
class CKLBNetAPI;

/*!
* \class CKLBNetAPI
* \brief Net API class.
* 
* CKLBNetAPI is responsible Network communications.
*/
class CKLBNetAPI : public CKLBLuaTask
{
	friend class CKLBTaskFactory<CKLBNetAPI>;
private:
	CKLBNetAPI();
	virtual ~CKLBNetAPI();

	bool init(	CKLBTask* pTask,
				const char* url,
				const char* clientVersion,
				const char* consumerKey,
				const char* applicationID,
				const char* callback,
				const char* language,
				const char* region,
				const char* versionUpdateCallback);
public:
	virtual u32 getClassID();
	static CKLBNetAPI* create(	CKLBTask* pParentTask,
								const char* url,
								const char* clientVersion,
								const char* consumerKey,
								const char* applicationID,
								const char* callback,
								const char* language,
								const char* region,
								const char* versionUpdateCallback);
	void execute(u32 deltaT);
	void die();

	bool initScript(CLuaState& lua);
	int commandScript(CLuaState& lua);
private:
	// Script callback shared by every request lifecycle result.
	const char			*	m_callback;

	struct ConnectionSetup {
		const char*	versionUpdateCallback;
		const char*	url;
		const char*	consumerKey;
		const char*	clientVersion;
		const char*	applicationID;
	};
	ConnectionSetup		m_connection;

	const char*			m_region;
	const char*			m_language;
	const char*			m_os;
	const char*			m_osVersion;
	const char*			m_timeZone;
	int					m_platformType;
	const char*			m_bundleVersion;
	u32					m_nonce;
	u32					m_requestSerial;
	const char**			m_headerContext;
	const char*			m_authorizeToken;
	const char*			m_userID;
	const char*			m_loginKey;
	const char*			m_loginPassword;
	char				m_authorizeHeader[1024];
	int					m_authorizePrefixLength;

	struct Session {
		enum State {
			STATE_INITIAL_HANDSHAKE,
			STATE_STARTUP,
			STATE_LOGIN_HANDSHAKE,
			STATE_LOGIN_REQUEST,
			STATE_LOGIN_RESPONSE,
			STATE_API_REQUEST
		};

		Session(int requestID, int state, int timeout);

		int					requestID;
		State				state;
		CKLBHTTPInterface*	connection;
		u32					timeout;
		u32					elapsed;
		bool				started;
		bool				checkVersion;
		bool				canceled;

		~Session();
	};
	std::list<Session*>	m_activeRequests;
	std::list<Session*>	m_pendingRequests;

private:
	bool lua_callback(int uniq, int msg, int status, CKLBJsonItem * pRoot, int dataSize);
	bool isCanceled(Session* request);
	bool matchesClientVersion(const char* serverVersion,
							  const char* clientVersion,
							  size_t comparisonLength);
	char* copyServerVersion(const char* serverVersion);
	bool processRequest(Session* request, u32 deltaT);
	bool startRequest(Session* request, const char* path, int command);
	bool prepareAuthorizationHeader(int command, const char* token);
	bool installRequestHeaders(CKLBHTTPInterface* connection);
	s64 encodeBase64(const char* input, size_t inputLength, char** output);
	s64 decodeBase64(const char* input, size_t inputLength, char** output);
	bool publicKeyEncryptThenBase64(const u8* input, int inputLength,
									char** output);
	bool encryptThenBase64(const char* input, size_t inputLength,
						   const u8* key, size_t keyLength, char** output);
	bool decodeBase64ThenDecrypt(const char* input, size_t inputLength,
								 const u8* key, size_t keyLength,
								 char** output);
	bool decryptFromBase64(const char* input, u8** output);
	bool buildLoginRequest(const char* loginKey, const char* loginPassword,
						   char** output);
	bool installUserIDHeader();
	bool cancel(int requestID);
	void cancelRequests();
	Session* newSession(Session::State state, int timeout);
	bool enqueueLogin(const char* loginKey, const char* loginPassword,
					  int timeout, int* requestID, Session::State state);
	bool enqueueSend(const char* endPoint, const char* json, int timeout,
					 bool checkVersion, int* requestID, bool absolute,
					 const char* specialKey, bool hasSessionMACKey);

	CKLBJsonItem * getJsonTree(const char * json_string, u32 dataLen);
	bool get_token(CKLBJsonItem * pRoot);

public:
	bool cancel		();
};

#endif // CKLBNetAPI_h
