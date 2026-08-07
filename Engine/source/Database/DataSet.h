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
#ifndef INCLUDE_DATASET_H
#define INCLUDE_DATASET_H

#include "BaseType.h"

enum EMOVECODE {
	MOVE_SUCCEED,
	MOVE_EOF,
	MOVE_UNFETCH,
};

typedef void*	IDataRecord;

class IDataSource;
class IDataSourceUpdateNotifier;
class BaseRealDataProducer;
class JSonDataParser;
class CKLBDataTask;
struct yajl_handle_t;

class DataSourceSubscription {
public:
	typedef void (*UpdateCallback)(void* context, s64 firstRecord, s64 lastRecord, u32 updateType);

	DataSourceSubscription();
	~DataSourceSubscription();
	void setNotifier(bool subscribe, IDataSourceUpdateNotifier* notifier);
	void notify(s64 afterRecordID, s64 firstRecordID, u32 recordCount);

	void configure(UpdateCallback callback, void* context) {
		m_callback = callback;
		m_context  = context;
	}

private:
	friend class CKLBDataTask;
	friend class CKLBUIList;

	void dispatch(s64 afterRecordID, s64 firstRecordID, u32 recordCount) {
		if(m_context && m_callback) {
			m_callback(m_context, afterRecordID, firstRecordID, recordCount);
		}
	}

	IDataSourceUpdateNotifier*	m_notifier;
	UpdateCallback				m_callback;
	void*						m_context;
};

class IDataSourceUpdateNotifier {
public:
	virtual BaseRealDataProducer* getDataSource() = 0;
	virtual void setSubscription(bool subscribe, DataSourceSubscription* subscription) = 0;
};

class IDataSource {
public:
	static const int	TYPE_INT	= 0;
	static const int	TYPE_STR	= 1;
	static const int	TYPE_BOOL	= 2;
	static const int	TYPE_FLOAT	= 3;
	static const int	TYPE_NIL	= 4;
	static const int	TYPE_BLOB	= 5;
	
	static	IDataSource*	
						doQuery					(const char* query);
	static	void		releaseQuery			(IDataSource* pQuery);
	
	// -1 : Dynamic.
	virtual	s32			getTotalRecordCount		() = 0;
	
	// Record loaded until now.
	virtual s32			getCurrentRecordCount	() = 0;
	
	// Request record if UNFETCH
	virtual bool		fetchRecords			(u32 count) = 0;
	// Result of fetching (polling from user)
	virtual bool		receivedUpdate			() = 0;
	
	virtual	EMOVECODE	moveTo					(u32 record) = 0;
	virtual	EMOVECODE	moveNext				() = 0;
	virtual	EMOVECODE	movePrevious			() = 0;

	virtual	IDataRecord getRecord				() = 0;
	
	virtual u32			getFieldCount			() = 0;
	virtual	const char*	getFieldName			(u32 index, u32& length) = 0;
	virtual u32			getFieldType			(u32 index) = 0;
	virtual u32			getFieldIndex			(const char* fieldName) = 0;

	// Access to one record.
	virtual	s32			getAsInt				(IDataRecord rec, u32 index) = 0;
	virtual const char*	getAsString				(IDataRecord rec, u32 index, u32& length) = 0;
	virtual bool		getAsBool				(IDataRecord rec, u32 index) = 0;
	virtual float		getAsFloat				(IDataRecord rec, u32 index) = 0;
    
    virtual ~IDataSource() {};
};

// The live data path uses a compact, append-only record producer. Records and
// their fields are allocated from linked 16 KiB blocks, so record pointers stay
// valid while parsers append additional rows.
class DataRecords {
public:
	virtual ~DataRecords() {}
};

class BaseRealDataProducer : public DataRecords {
public:
	static const u32 MAX_FIELD_COUNT = 16;
	static const s64 INVALID_RECORD_ID = -1;
	static const s64 APPEND_RECORD_ID = -2;

	struct StoredString {
		u8 lengthHigh;
		u8 lengthMiddle;
		u8 lengthLow;
		char text[1];
	};

	union RecordValue {
		s64 integer;
		const StoredString* string;
		float real;
		bool boolean;
	};

	struct Record {
		Record* next;
		RecordValue* fields;
		s64 recordID;
	};

	struct StorageBlock {
		StorageBlock* next;
		size_t used;
		u8 storage[0x3ff0];
	};

	BaseRealDataProducer();
	virtual ~BaseRealDataProducer();
	static BaseRealDataProducer* create();
	static void release(BaseRealDataProducer* producer);
	static BaseRealDataProducer* openLegacyJson(const u8* source, u32 sourceLength);

	void clear();
	void* allocateStorage(size_t size);
	const char* copyString(const char* source, u32 length);
	s32 addField(const char* name, u32 length);
	void setFieldType(u32 index, u8 type);
	bool reset();
	s64 insertRecord(u64 beforeRecordID);
	s64 insertRecord(Record* after);
	s64 appendRecord();
	Record* getLastRecord() const;
	Record* findRecord(s64 recordID) const;
	s64 getCurrentRecordID() const;
	s64 moveToRecord(s64 recordID);
	void setAsInt(u32 index, s64 value);
	void setAsFloat(u32 index, float value);
	void setAsBool(u32 index, bool value);
	void setAsString(u32 index, const char* value, size_t length);
	bool hasFields() const;
	void beginRecord();
	void commitRecord();
	s64 moveNextRecord();
	s64 moveToFirstRecord();
	s64 selectRecord(s64 recordID);
	s32 getFieldIndex(const char* fieldName, size_t length = 0xffffffffU) const;
	s64 getAsInt(u32 index) const;
	float getAsFloat(u32 index) const;
	bool getAsBool(u32 index) const;
	const char* getAsString(u32 index, u32& length) const;
	u32 getFieldType(u32 index) const;
	s32 getKeyFieldIndex() const;
	void commitRecordKey();
	void setKeyName(const char* keyName);

private:
	friend class JSonDataParser;
	friend class CKLBDataTask;

	Record* findRecordInternal(s64 recordID) const {
		Record* record = m_currentRecord;
		s64 currentID = record ? record->recordID : INVALID_RECORD_ID;
		if(currentID == recordID) return record;

		record = m_records;
		while(record) {
			if(record->recordID == recordID) return record;
			record = record->next;
		}
		return NULL;
	}

	s64 restoreFirstRecord() {
		s64 recordID = m_savedFirstRecordID;
		m_savedFirstRecordID = INVALID_RECORD_ID;
		m_currentRecord = findRecordInternal(recordID);
		return recordID;
	}

	bool m_ownsStorage;
	const char* m_keyName;
	s32 m_keyFieldIndex;
	StorageBlock* m_firstStorage;
	StorageBlock* m_lastStorage;
	Record* m_records;
	Record* m_currentRecord;
	Record* m_lastRecord;
	RecordValue* m_currentFields;
	s64 m_savedFirstRecordID;
	const char* m_fieldNames[MAX_FIELD_COUNT];
	u8 m_fieldTypes[MAX_FIELD_COUNT];
	u16 m_fieldNameLengths[MAX_FIELD_COUNT];
	RecordValue m_pendingFields[MAX_FIELD_COUNT];
	u8 m_fieldCount;
};

// YAJL adapter for the live, typed record producer. The parser owns only
// transient schema, nesting, and insertion state; record storage remains with
// BaseRealDataProducer.
class JSonDataParser {
public:
	JSonDataParser();
	~JSonDataParser();

	static int read_start_map(void* context, unsigned int size);
	static int read_null(void* context);
	static int read_boolean(void* context, int value);
	static int read_int(void* context, long long value);
	static int read_double(void* context, double value);
	static int read_string(void* context, const unsigned char* value,
						   size_t length, int constantPool);
	static int read_map_key(void* context, const unsigned char* name,
						   size_t length, int constantPool);
	static int read_end_map(void* context);
	static int read_start_array(void* context, unsigned int size);
	static int read_end_array(void* context);

	bool parse(const u8* source, size_t sourceLength,
			   BaseRealDataProducer* producer, s64 beforeRecordID,
			   u32* recordCount);

private:
	enum ParseState {
		PARSE_IDLE,
		PARSE_RECORD,
		PARSE_NESTED
	};

	int readStartMap(unsigned int size);
	int readNull();
	int readBoolean(int value);
	int readInt(long long value);
	int readDouble(double value);
	int readString(const unsigned char* value, size_t length, int constantPool);
	int readMapKey(const unsigned char* name, size_t length, int constantPool);
	int readEndMap();
	int readStartArray(unsigned int size);
	int readEndArray();
	void setCurrentFieldType(u8 type) {
		BaseRealDataProducer* producer = m_producer;
		u32 fieldIndex = m_fieldIndex;
		klb_assertNull(producer->m_fieldCount > fieldIndex,
			"Error in field index");
		producer->m_fieldTypes[fieldIndex] = type;
	}

	EMOVECODE					m_result;
	const char*					m_keyFieldName;
	u32						m_currentRecordIndex;
	u32						m_mapDepth;
	u32						m_arrayDepth;
	yajl_handle_t*				m_parser;
	u32						m_fieldIndex;
	bool						m_schemaComplete;
	bool						m_insertByID;
	BaseRealDataProducer*		m_producer;
	s64						m_beforeRecordID;
	ParseState					m_state;
	u32						m_recordCount;
};

#endif // INCLUDE_DATASET_H
