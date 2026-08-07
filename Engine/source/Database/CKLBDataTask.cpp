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
#include "CKLBDataTask.h"
#include "CKLBUIForm.h"
#include "CKLBUIList.h"
#include "MultithreadedNetwork.h"

enum {
	DATATASK_ADDDATA = 1,
	DATATASK_ADDJSON,
	DATATASK_SETUSRVAL,
	DATATASK_GETUSRVAL,
	DATATASK_BINDNEXTFORM,
	DATATASK_BINDUILIST
};

static IFactory::DEFCMD cmd[] = {
	{ "DATATASK_ADDJSON", DATATASK_ADDJSON },
	{ "DATATASK_ADDDATA", DATATASK_ADDDATA },
	{ "DATATASK_SETUSRVAL", DATATASK_SETUSRVAL },
	{ "DATATASK_GETUSRVAL", DATATASK_GETUSRVAL },
	{ "DATATASK_BINDNEXTFORM", DATATASK_BINDNEXTFORM },
	{ "DATATASK_BINDUILIST", DATATASK_BINDUILIST },
	// Event codes handed to the script callback, and the two record markers
	// scripts pass back to place newly loaded rows.
	{ "EVENT_START", CKLBDataTask::DATA_EVENT_REQUEST },
	{ "EVENT_DATARECEIVED", CKLBDataTask::DATA_EVENT_INSERTED },
	{ "EVENT_TIMEOUT", CKLBDataTask::DATA_EVENT_FAILED },
	{ "DATA_EOF", (int)BaseRealDataProducer::INVALID_RECORD_ID },
	{ "DATA_FIRST", (int)BaseRealDataProducer::APPEND_RECORD_ID },
	{ NULL, 0 }
};

static CKLBTaskFactory<CKLBDataTask> factory(
	"DATA_Task", 0x00280056, cmd);

CKLBDataTask::CKLBDataTask()
: m_subscription(NULL)
, m_request(NULL)
, m_dataSource(NULL)
, m_callback(NULL)
{
}

CKLBDataTask::~CKLBDataTask()
{
}

u32
CKLBDataTask::getClassID()
{
	return 0x00280056;
}

CKLBDataTask*
CKLBDataTask::create(const char* callback, const char* keyFieldName)
{
	CKLBDataTask* task = KLBNEW(CKLBDataTask);
	if(!task) {
		return NULL;
	}
	if(!task->init(callback, keyFieldName)) {
		KLBDELETE(task);
		return NULL;
	}
	return task;
}

bool
CKLBDataTask::init(const char* callback, const char* keyFieldName)
{
	setStrC(m_callback, callback);
	bool hasCallback = m_callback != NULL;
	m_dataSource = KLBNEW(BaseRealDataProducer);
	m_dataSource->setKeyName(keyFieldName);
	if(hasCallback) {
		return regist(NULL, P_UIPREV);
	}
	return false;
}

bool
CKLBDataTask::initScript(CLuaState& lua)
{
	int argc = lua.numArgs();
	if(argc < 1 || argc > 2) {
		return false;
	}

	const char* callback = lua.getString(1);
	const char* keyFieldName = NULL;
	if(argc == 2 && !lua.isNil(2) && lua.isString(2)) {
		keyFieldName = lua.getString(2);
	}
	return init(callback, keyFieldName);
}

bool
CKLBDataTask::initScriptSecondary(CLuaState& /* lua */)
{
	return true;
}

void
CKLBDataTask::execute(u32 /* deltaT */)
{
	if(!m_request) {
		return;
	}

	bool complete = m_request->isDataComplete();
	s64 receivedSize = m_request->getSize();
	s32 state = m_request->getHttpState();

	if(complete) {
		if(receivedSize > 0 && state == 0 && m_request->getHttpStatus() == 200) {
			addJSON(m_request->getRecvResource(), receivedSize, m_callbackEvent);
		} else {
			CKLBScriptEnv::getInstance().call_eventDataTask(
				m_callback, this, DATA_EVENT_FAILED, 0);
		}

		NetworkManager::releaseConnection(m_request);
		m_request = NULL;
	}
}

bool
CKLBDataTask::addData(const char* url, s64 beforeRecordID)
{
	// 非同期取得の完了時に execute() が使う挿入位置を覚えておく。
	m_callbackEvent = static_cast<u32>(beforeRecordID);
	bool result = false;

	if(!strncmp("http://", url, 4)) {
		if(!m_request) {
			m_request = NetworkManager::createConnection();
			if(m_request) {
				m_request->httpGET(url, false, NULL);
				CKLBScriptEnv::getInstance().call_eventDataTask(
					m_callback, this, DATA_EVENT_REQUEST, 0);
			}
		}
	} else if(!strncmp("file://", url, 4) || !strncmp("asset://", url, 5)) {
		IPlatformRequest& platform = CPFInterface::getInstance().platform();
		IReadStream* stream = platform.openReadStream(url, platform.useEncryption(), 14);
		if(stream) {
			if(stream->getStatus() == IReadStream::NORMAL) {
				u8* source = KLBNEWA(u8, stream->getSize());
				if(stream->readBlock(source, stream->getSize())) {
					CKLBScriptEnv::getInstance().call_eventDataTask(
						m_callback, this, DATA_EVENT_REQUEST, 0);
					if(addJSON(source, stream->getSize(), beforeRecordID) != -1) {
						delete[] source;
						delete stream;
						return true;
					}
				}
				delete[] source;
			}
			delete stream;
		}
	} else {
		klb_assert(strncmp("db://", url, 4), "NOT IMPLEMENTED");
	}

	return result;
}

s32
CKLBDataTask::addJSON(const u8* source, size_t sourceLength, s64 beforeRecordID)
{
	JSonDataParser* parser = KLBNEW(JSonDataParser);
	u32 parsedCount;
	s32 count = -1;
	if(parser->parse(source, sourceLength, m_dataSource,
					 beforeRecordID, &parsedCount) && m_subscription) {
		s64 firstRecordID = m_dataSource->restoreFirstRecord();
		count = static_cast<s32>(parsedCount);
		m_subscription->dispatch(beforeRecordID, firstRecordID, parsedCount);
		if(count >= 0) {
			CKLBScriptEnv::getInstance().call_eventDataTask(
				m_callback, this, DATA_EVENT_INSERTED, count);
		}
	}
	return count;
}

int
CKLBDataTask::commandScript(CLuaState& lua)
{
	int argc = lua.numArgs();
	if(argc < 2) {
		lua.retBool(false);
		return 1;
	}
	switch(lua.getInt(2))
	{
	default:
		lua.retBool(false);
		break;
	case DATATASK_ADDDATA:
		{
			s64 beforeRecordID = 0;
			if(lua.isString(4)) {
				const char* text = lua.getString(4);
				beforeRecordID = (*text == '-')
					? CKLBUtility::stringNum64(text)
					: CKLBUtility::stringNum(text);
			} else if(lua.isNum(4)) {
				s32 special = lua.getInt(4);
				if(special == BaseRealDataProducer::INVALID_RECORD_ID) {
					beforeRecordID = BaseRealDataProducer::INVALID_RECORD_ID;
				} else if(special == BaseRealDataProducer::APPEND_RECORD_ID) {
					beforeRecordID = BaseRealDataProducer::APPEND_RECORD_ID;
				} else {
					beforeRecordID = (s64)(u64)lua.getDouble(4);
				}
			}
			const char* url = lua.getString(3);
			lua.retBool(addData(url, beforeRecordID));
		}
		break;
	case DATATASK_ADDJSON:
		{
			const char* json = lua.getString(3);
			size_t jsonLength = strlen(json);
			CKLBScriptEnv::getInstance().call_eventDataTask(
				m_callback, this, DATA_EVENT_REQUEST, 0);
			s64 beforeRecordID = 0;
			if(lua.isString(4)) {
				beforeRecordID = CKLBUtility::stringNum(lua.getString(4));
			} else if(lua.isNum(4)) {
				s32 special = lua.getInt(4);
				if(special == BaseRealDataProducer::INVALID_RECORD_ID) {
					beforeRecordID = BaseRealDataProducer::INVALID_RECORD_ID;
				} else if(special == BaseRealDataProducer::APPEND_RECORD_ID) {
					beforeRecordID = BaseRealDataProducer::APPEND_RECORD_ID;
				} else {
					beforeRecordID = (s64)(u64)lua.getDouble(4);
				}
			}
			addJSON((const u8 *)json, jsonLength, beforeRecordID);
		}
		break;
	case DATATASK_SETUSRVAL:
		{
			bool result = false;
			if(argc == 4) {
				s32 index = lua.getInt(3);
				klb_assert(index < 5, "INVALID INDEX");

				UserValue& slot = m_userValues[index];
				if(slot.type == USER_VALUE_STRING) {
					KLBDELETEA(slot.value.string);
					slot.value.string = NULL;
				}

				if(lua.isString(4)) {
					slot.type = USER_VALUE_STRING;
					slot.value.string = (char *)CKLBUtility::copyString(lua.getString(4));
				} else if(lua.isNum(4)) {
					slot.type = USER_VALUE_NUMBER;
					slot.value.number = lua.getFloat(4);
				} else {
					klb_assert(lua.getType(4) == LUA_TNIL, "INVALID Property Type");
					slot.type = USER_VALUE_NIL;
				}
				result = true;
			}
			lua.retBool(result);
		}
		break;
	case DATATASK_GETUSRVAL:
		{
			if(argc == 3) {
				s32 index = lua.getInt(3);
				klb_assert(index < 5, "INVALID INDEX");

				UserValue& slot = m_userValues[index];
				if(slot.type == USER_VALUE_STRING) {
					lua.retString(slot.value.string);
				} else if(slot.type == USER_VALUE_NUMBER) {
					lua.retFloat(slot.value.number);
				} else if(slot.type == USER_VALUE_NIL) {
					lua.retNil();
				} else {
					lua.retBool(false);
				}
			} else {
				lua.retBool(false);
			}
		}
		break;
	case DATATASK_BINDNEXTFORM:
		// 次に生成されるフォームがこのデータタスクを参照する。
		CKLBUIForm::setDataTask(this);
		lua.retBool(true);
		break;
	case DATATASK_BINDUILIST:
		{
			bool result = false;
			if(argc == 3) {
				CKLBUIList* list = (CKLBUIList *)lua.getScriptPtr(3);
				if(list) {
					list->bindDataTask(this);
					result = true;
				}
			}
			lua.retBool(result);
		}
		break;
	}
	return 1;
}

BaseRealDataProducer*
CKLBDataTask::getDataSource()
{
	return m_dataSource;
}

void
CKLBDataTask::setSubscription(bool subscribe, DataSourceSubscription* subscription)
{
	if(subscribe) {
		if(m_subscription == subscription) return;
		if(m_subscription) {
			setSubscription(false, NULL);
		}
		m_subscription = subscription;
		if(subscription) {
			IDataSourceUpdateNotifier* notifier = this;
			if(subscription->m_notifier != notifier) {
				if(subscription->m_notifier) {
					IDataSourceUpdateNotifier* previous = subscription->m_notifier;
					subscription->m_notifier = NULL;
					previous->setSubscription(false, NULL);
				}
				subscription->m_notifier = notifier;
				notifier->setSubscription(true, subscription);
			}
		}
	} else if(m_subscription) {
		DataSourceSubscription* previous = m_subscription;
		m_subscription = NULL;
		if(previous->m_notifier) {
			IDataSourceUpdateNotifier* notifier = previous->m_notifier;
			previous->m_notifier = NULL;
			notifier->setSubscription(false, NULL);
		}
	}
}

void
CKLBDataTask::die()
{
	if(CKLBUIForm::getDataTask() == this) {
		CKLBUIForm::setDataTask(NULL);
	}

	setSubscription(false, NULL);

	if(m_request) {
		NetworkManager::releaseConnection(m_request);
		m_request = NULL;
	}

	KLBDELETE(m_dataSource);
	m_dataSource = NULL;

	KLBDELETEA(m_callback);
	m_callback = NULL;

	for(u32 i = 0; i < 5; ++i) {
		if(m_userValues[i].type == USER_VALUE_STRING) {
			KLBDELETEA(m_userValues[i].value.string);
			m_userValues[i].value.string = NULL;
		}
	}
}
