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
#ifndef CKLBDATATASK_H
#define CKLBDATATASK_H

#include "CKLBLuaTask.h"
#include "DataSet.h"

class CKLBHTTPInterface;

// Data tasks expose their live record producer through a secondary notifier
// interface and can append records received by their asynchronous request.
class CKLBDataTask : public CKLBLuaTask, public IDataSourceUpdateNotifier {
public:
	CKLBDataTask();
	virtual ~CKLBDataTask();
	virtual u32 getClassID();
	static CKLBDataTask* create(const char* callback, const char* keyFieldName);
	virtual bool initScript(CLuaState& lua);
	virtual bool initScriptSecondary(CLuaState& lua);
	virtual int commandScript(CLuaState& lua);
	virtual void execute(u32 deltaT);
	virtual void die();
	virtual BaseRealDataProducer* getDataSource();
	virtual void setSubscription(bool subscribe, DataSourceSubscription* subscription);

	// Reported to the script callback, so the values are part of the Lua API.
	enum DataEvent {
		DATA_EVENT_REQUEST	= 1,
		DATA_EVENT_INSERTED	= 2,
		DATA_EVENT_FAILED	= 3
	};

private:
	bool init(const char* callback, const char* keyFieldName);
	// Loads a data source named by scheme. Local schemes are read and parsed
	// immediately; "http://" starts an asynchronous request whose records are
	// appended by execute(). Returns true only when records were added here.
	bool addData(const char* url, s64 beforeRecordID);
	s32 addJSON(const u8* source, size_t sourceLength, s64 beforeRecordID);

	enum UserValueType {
		USER_VALUE_NIL		= 0,
		USER_VALUE_NUMBER	= 3,
		USER_VALUE_STRING	= 6
	};

	// A user slot retains the Lua-compatible scalar payload together with
	// storage for string values. Only the active representation is interpreted.
	struct UserValue {
		const char* name;
		UserValueType type;
		union Payload {
			float	number;
			char*	string;
			s64		integer;
			bool	boolean;
		} value;
		bool changed;
	};

	UserValue					m_userValues[5];
	DataSourceSubscription*		m_subscription;
	CKLBHTTPInterface*			m_request;
	BaseRealDataProducer*		m_dataSource;
	u32							m_callbackEvent;
	const char*					m_callback;
};

#endif
