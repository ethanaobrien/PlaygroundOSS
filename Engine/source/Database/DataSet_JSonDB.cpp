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
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "DataSet_JSonDB.h"

BaseRealDataProducer::BaseRealDataProducer()
{
	m_firstStorage = NULL;
	m_lastStorage = NULL;
	m_records = NULL;
	m_currentRecord = NULL;
	m_lastRecord = NULL;
	m_savedFirstRecordID = INVALID_RECORD_ID;
	m_fieldCount = 0;
	m_ownsStorage = true;
	m_keyName = NULL;
	m_keyFieldIndex = -1;
}

BaseRealDataProducer::~BaseRealDataProducer()
{
	Record* record = m_records;
	if (record) {
		do {
			record = record->next;
		} while (record);
	}

	StorageBlock* block = m_firstStorage;
	while (block) {
		StorageBlock* next = block->next;
		KLBDELETE(block);
		block = next;
	}
	m_firstStorage = NULL;
	m_fieldCount = 0;
}

BaseRealDataProducer* BaseRealDataProducer::create()
{
	return KLBNEW(BaseRealDataProducer);
}

void BaseRealDataProducer::release(BaseRealDataProducer* producer)
{
	KLBDELETE(producer);
}

void BaseRealDataProducer::clear()
{
	Record* record = m_records;
	if (record) {
		do {
			record = record->next;
		} while (record);
	}

	StorageBlock* block = m_firstStorage;
	while (block) {
		StorageBlock* next = block->next;
		KLBDELETE(block);
		block = next;
	}
	m_firstStorage = NULL;
	m_fieldCount = 0;
}

void* BaseRealDataProducer::allocateStorage(size_t size)
{
	StorageBlock* block = m_lastStorage;
	if (!block) {
allocateBlock:
		block = KLBNEW(StorageBlock);
		memset(block, 0, sizeof(StorageBlock));
		m_lastStorage = block;
		if (m_firstStorage) m_firstStorage->next = block;
		m_firstStorage = block;
	}

	size_t cursor = block->used;
	if ((sizeof(StorageBlock) - sizeof(StorageBlock*) - cursor) < size) {
		goto allocateBlock;
	}

	u8* allocation = block->storage + cursor;
	u32 misalignment = static_cast<u32>(reinterpret_cast<size_t>(allocation)) & 7;
	size_t padding = 0;
	if (misalignment) padding = 8 - misalignment;
	allocation += padding;
	if ((allocation + size) > reinterpret_cast<u8*>(block + 1)) {
		goto allocateBlock;
	}

	block->used = cursor + padding + size;
	return allocation;
}

const char* BaseRealDataProducer::copyString(const char* source, u32 length)
{
	if (!source) return NULL;
	char* result = static_cast<char*>(allocateStorage(length + 1));
	memcpy(result, source, length);
	result[length] = 0;
	return result;
}

s32 BaseRealDataProducer::addField(const char* name, u32 length)
{
	klb_assertNull(m_fieldCount < MAX_FIELD_COUNT, "Reached maximum field count");
	m_fieldNames[m_fieldCount] = copyString(name, length);
	m_fieldNameLengths[m_fieldCount] = length;
	m_fieldTypes[m_fieldCount] = 0xff;
	return m_fieldCount++;
}

void BaseRealDataProducer::setFieldType(u32 index, u8 type)
{
	klb_assertNull(index < m_fieldCount, "Error in field index");
	m_fieldTypes[index] = type;
}

bool BaseRealDataProducer::reset()
{
	clear();
	return true;
}

s64 BaseRealDataProducer::insertRecord(u64 beforeRecordID)
{
	klb_assertNull(m_fieldCount, "Field must be defined");
	if (beforeRecordID >= static_cast<u64>(APPEND_RECORD_ID)) {
		Record* after = NULL;
		if (beforeRecordID == INVALID_RECORD_ID) {
			after = getLastRecord();
		} else if (beforeRecordID != APPEND_RECORD_ID) {
			Record* record = m_records;
			while (record && record->recordID != beforeRecordID) {
				after = record;
				record = record->next;
			}
			if (!after) after = getLastRecord();
		}
		return insertRecord(after);
	}
	klb_assertAlways("INVALID : not first, not eof, not a valid record id range.");
	return INVALID_RECORD_ID;
}

s64 BaseRealDataProducer::insertRecord(Record* after)
{
	Record* record = static_cast<Record*>(allocateStorage(sizeof(Record)));
	RecordValue* fields = static_cast<RecordValue*>(
		allocateStorage(m_fieldCount * sizeof(RecordValue))
	);
	if (after) {
		record->next = after->next;
		after->next = record;
	} else {
		record->next = m_records;
		m_records = record;
	}
	record->recordID = APPEND_RECORD_ID;
	record->fields = fields;
	m_lastRecord = record;
	m_currentFields = fields;
	return m_lastRecord->recordID;
}

s64 BaseRealDataProducer::appendRecord()
{
	return insertRecord(m_lastRecord);
}

BaseRealDataProducer::Record* BaseRealDataProducer::getLastRecord() const
{
	Record* record = m_lastRecord;
	while (record) {
		Record* next = record->next;
		if (!next) return record;
		record = next;
	}
	return NULL;
}

BaseRealDataProducer::Record* BaseRealDataProducer::findRecord(s64 recordID) const
{
	return findRecordInternal(recordID);
}

s64 BaseRealDataProducer::getCurrentRecordID() const
{
	return m_currentRecord ? m_currentRecord->recordID : INVALID_RECORD_ID;
}

s64 BaseRealDataProducer::moveToRecord(s64 recordID)
{
	Record* record = m_currentRecord;
	s64 currentID = record ? record->recordID : INVALID_RECORD_ID;
	if (currentID != recordID) {
		record = m_records;
		while (record && record->recordID != recordID) {
			record = record->next;
		}
	}
	if (record) {
		m_lastRecord = record;
		m_currentFields = record->fields;
		return record->recordID;
	}
	return INVALID_RECORD_ID;
}

void BaseRealDataProducer::setAsInt(u32 index, s64 value)
{
	klb_assertNull(index < m_fieldCount, "Field index out of range");
	klb_assertNull(m_fieldTypes[index] == IDataSource::TYPE_INT, "Type does not match");
	if (m_currentFields) m_currentFields[index].integer = value;
}

void BaseRealDataProducer::setAsFloat(u32 index, float value)
{
	klb_assertNull(index < m_fieldCount, "Field index out of range");
	klb_assertNull(m_fieldTypes[index] == IDataSource::TYPE_FLOAT, "Type does not match");
	if (m_currentFields) m_currentFields[index].real = value;
}

void BaseRealDataProducer::setAsBool(u32 index, bool value)
{
	klb_assertNull(index < m_fieldCount, "Field index out of range");
	klb_assertNull(m_fieldTypes[index] == IDataSource::TYPE_BOOL, "Type does not match");
	if (m_currentFields) m_currentFields[index].boolean = value;
}

void BaseRealDataProducer::setAsString(u32 index, const char* value, size_t length)
{
	klb_assertNull(index < m_fieldCount, "Field index out of range");
	klb_assertNull(m_fieldTypes[index] == IDataSource::TYPE_STR, "Type does not match");
	if (!m_currentFields) return;

	if (!value) {
		m_currentFields[index].string = NULL;
		return;
	}

	StoredString* result = static_cast<StoredString*>(allocateStorage(length + 4));
	result->lengthHigh = static_cast<u8>(length >> 16);
	result->lengthMiddle = static_cast<u8>(length >> 8);
	result->lengthLow = static_cast<u8>(length);
	memcpy(result->text, value, length + 1);
	m_currentFields[index].string = result;
}

bool BaseRealDataProducer::hasFields() const
{
	return m_fieldCount != 0;
}

void BaseRealDataProducer::beginRecord()
{
	m_currentFields = m_pendingFields;
}

void BaseRealDataProducer::commitRecord()
{
	memcpy(m_lastRecord->fields, m_pendingFields,
		m_fieldCount * sizeof(RecordValue));
	m_currentFields = m_lastRecord->fields;
}

s64 BaseRealDataProducer::moveNextRecord()
{
	if (m_currentRecord) {
		m_currentRecord = m_currentRecord->next;
	} else {
		m_currentRecord = m_records;
	}
	return m_currentRecord ? m_currentRecord->recordID : INVALID_RECORD_ID;
}

s64 BaseRealDataProducer::moveToFirstRecord()
{
	return restoreFirstRecord();
}

s64 BaseRealDataProducer::selectRecord(s64 recordID)
{
	Record* record = m_currentRecord;
	s64 currentID = record ? record->recordID : INVALID_RECORD_ID;
	if (currentID == recordID) {
		recordID = INVALID_RECORD_ID;
		if (record) recordID = record->recordID;
	} else {
		record = m_records;
		while (record && record->recordID != recordID) {
			record = record->next;
		}
		if (!record) recordID = INVALID_RECORD_ID;
	}
	m_currentRecord = record;
	return recordID;
}

s32 BaseRealDataProducer::getFieldIndex(const char* fieldName, size_t length) const
{
	klb_assertNull(fieldName, "Invalid name");
	if (length == 0xffffffffU) length = strlen(fieldName);

	for (u32 index = 0; index < m_fieldCount; ++index) {
		if ((length == m_fieldNameLengths[index]) &&
			(strncmp(m_fieldNames[index], fieldName, length) == 0)) {
			return index;
		}
	}
	return -1;
}

/*
 * Commit the key of the record that was most recently assembled.
 *
 * A JSON object does not need to present its key field first.  Parsing
 * therefore completes the row before this method resolves the key column.
 * The schema stores field names and lengths in parallel arrays, while the
 * record stores only its compact value array.
 *
 * The key-name pointer belongs to the producer's arena.  Its lifetime is the
 * same as the schema and every committed record, so resolving the index does
 * not require a temporary copy or an ownership transfer.
 *
 * Resolution is lazy.  The first completed record searches the schema and
 * caches the matching column in m_keyFieldIndex.  Later records reuse that
 * index, avoiding repeated string comparisons for every row in the document.
 *
 * Field-name lengths are retained when the schema is created.  A candidate
 * must have both the same length and the same bytes as the configured key;
 * the length check also prevents a matching prefix from being accepted.
 *
 * A missing key column is a malformed producer schema rather than a record
 * with a special identifier.  The assertion keeps that failure at the schema
 * boundary, before an invalid column can be used to index the value array.
 *
 * RecordValue preserves the parsed integer without narrowing.  The selected
 * value is copied to Record::recordID, which is the identifier used by record
 * traversal, insertion, subscription, and restoration operations.
 *
 * The first committed identifier is retained separately.  Consumers may move
 * the current-record cursor while data is in use, so m_savedFirstRecordID is
 * the stable value used when the producer must restore its initial position.
 *
 * INVALID_RECORD_ID is also the initialization sentinel for the saved value.
 * Testing the sentinel before assignment means later commits never overwrite
 * the identity of the first published record.
 *
 * The method deliberately updates only record metadata.  Field storage has
 * already been populated by the parser callbacks, and the insertion links
 * have already been established by commitRecord().
 *
 * Keeping these responsibilities separate makes the record lifecycle clear:
 * beginRecord() selects pending storage, value callbacks populate the fields,
 * commitRecord() publishes the row, and commitRecordKey() publishes its key.
 *
 * Key lookup follows the same name representation as getFieldIndex(), but it
 * remains local here because the cached index is part of commit state.  The
 * search uses size_t for both the key length and iteration count, matching the
 * arena-owned string metadata and the target ABI.
 *
 * Only the first lookup can fail.  Once m_keyFieldIndex is cached, every row
 * shares the established schema and proceeds directly to the integer value.
 *
 * A producer with no configured key does not call this method.  The parser
 * invokes it only for keyed record sets after the row has been committed.
 *
 * The resolved column is required to carry IDataSource::TYPE_INT by the schema
 * construction path.  Reading the integer union member here is consequently
 * a typed operation, not a reinterpretation of arbitrary parser storage.
 *
 * Record IDs remain signed at the IDataSource boundary so the two negative
 * navigation sentinels retain their documented meaning.  The stored integer
 * is copied without conversion and compared through that public contract.
 *
 * This routine performs no allocation.  All names, values, and record nodes
 * were allocated from the producer arena earlier in the parse.
 *
 * The cached field index and saved first identifier are reset when clear()
 * releases the arena, ensuring a subsequent document resolves its own schema.
 *
 * In short, this is the transition from a structurally complete JSON row to a
 * keyed database record visible through IDataSource.
 *
 * Separating the cached key index from the saved first identifier is
 * important: the former describes schema, while the latter describes data.
 * A valid schema can be reused across every row, but the first identifier is
 * captured exactly once for the lifetime of a loaded record set.
 *
 * This also keeps cursor restoration independent of physical record links.
 * Even if records are inserted around the current position, the saved ID can
 * be resolved again through the normal traversal interface.
 * The record itself remains the single owner of its published identifier.
 *
 * The binary's neighboring assertion-line anchors retain this documentation
 * region even though comments themselves do not contribute executable bytes.
 */
void BaseRealDataProducer::commitRecordKey()
{
	if (m_keyFieldIndex == -1) {
		size_t length = strlen(m_keyName);
		klb_assertNull(m_keyName, "Invalid name");
		s32 keyFieldIndex = -1;
		size_t fieldCount = m_fieldCount;
		for (size_t index = 0; index < fieldCount; ++index) {
			if ((length == m_fieldNameLengths[index]) &&
				(strncmp(m_fieldNames[index], m_keyName, length) == 0)) {
				keyFieldIndex = index;
				break;
			}
		}
		m_keyFieldIndex = keyFieldIndex;
		klb_assert(m_keyFieldIndex != -1, "Key field '%s' not found for record ID", m_keyName);
	}

	u32 keyFieldIndex = m_keyFieldIndex;
	s64 recordID = m_lastRecord->fields[keyFieldIndex].integer;
	m_lastRecord->recordID = recordID;
	if (m_savedFirstRecordID == INVALID_RECORD_ID) {
		m_savedFirstRecordID = recordID;
	}
}

/*
 * Typed record access
 *
 * The producer exposes the schema through IDataSource while keeping the
 * concrete RecordValue array private.  Each getter validates the requested
 * column against the established field type before reading the corresponding
 * union member from the current record.
 *
 * m_currentRecord is a traversal cursor.  It is independent of the parser's
 * m_currentFields write cursor, so reads always observe a committed row rather
 * than a pending JSON object.
 *
 * Integer values retain their full signed 64-bit representation.  Floating
 * values use the parser's stored float representation, and booleans use the
 * dedicated boolean member.
 *
 * The fatal current-record check belongs to the typed getter because callers
 * may change traversal position independently.  Index and schema checks use
 * the no-source-context invariant form shared by the producer.
 *
 * Keeping one small getter per field type makes the contract explicit and
 * allows callers to select the correct representation from getFieldType().
 *
 * String access differs slightly: it decodes the compact three-byte length
 * stored beside the arena-owned text and returns both pointer and size.
 *
 * None of the getters transfer ownership.  Returned strings and all scalar
 * storage remain valid until the producer is cleared.
 *
 * These accessors intentionally do not advance the record cursor; traversal
 * remains the responsibility of the move and select operations.
 *
 * Each result is copied by value from the selected committed record.
 *
 * Assertion-line anchors in the shipped translation unit preserve this
 * accessor documentation boundary.
 */
s64 BaseRealDataProducer::getAsInt(u32 index) const
{
	klb_assertNull(index < m_fieldCount, "Field index out of range");
	klb_assertNull(m_fieldTypes[index] == IDataSource::TYPE_INT, "Type does not match");
	klb_assert(m_currentRecord, "No record to read");
	return m_currentRecord->fields[index].integer;
}

// Read the current record's floating-point field.
// The schema check above the load preserves the typed IDataSource contract.
// The returned value is copied from committed record storage.
float BaseRealDataProducer::getAsFloat(u32 index) const
{
	klb_assertNull(index < m_fieldCount, "Field index out of range");
	klb_assertNull(m_fieldTypes[index] == IDataSource::TYPE_FLOAT, "Type does not match");
	klb_assert(m_currentRecord, "No record to read");
	return m_currentRecord->fields[index].real;
}

// Read the current record's boolean field.
// Boolean storage remains distinct from the integer representation.
// The returned value reflects the currently selected committed row.
bool BaseRealDataProducer::getAsBool(u32 index) const
{
	klb_assertNull(index < m_fieldCount, "Field index out of range");
	klb_assertNull(m_fieldTypes[index] == IDataSource::TYPE_BOOL, "Type does not match");
	klb_assert(m_currentRecord, "No record to read");
	return m_currentRecord->fields[index].boolean;
}

const char* BaseRealDataProducer::getAsString(u32 index, u32& length) const
{
	klb_assertNull(index < m_fieldCount, "Field index out of range");
	klb_assertNull(m_fieldTypes[index] == IDataSource::TYPE_STR, "Type does not match");

	const StoredString* string = m_currentRecord->fields[index].string;
	if (string) {
		length = (string->lengthHigh << 16) |
			(string->lengthMiddle << 8) | string->lengthLow;
		return string->text;
	}
	length = 0;
	return NULL;
}

u32 BaseRealDataProducer::getFieldType(u32 index) const
{
	klb_assertNull(index < m_fieldCount, "Field index out of range");
	return m_fieldTypes[index];
}

s32 BaseRealDataProducer::getKeyFieldIndex() const
{
	return m_keyFieldIndex;
}

void BaseRealDataProducer::setKeyName(const char* keyName)
{
	if (!keyName) {
		m_keyName = NULL;
		return;
	}

	size_t length = strlen(keyName) + 1;
	char* copy = static_cast<char*>(allocateStorage(length));
	memcpy(copy, keyName, length);
	m_keyName = copy;
}

JSonDataParser::JSonDataParser()
{
}

JSonDataParser::~JSonDataParser()
{
}

int JSonDataParser::read_start_map(void* context, unsigned int size)
{
	return static_cast<JSonDataParser*>(context)->readStartMap(size);
}

int JSonDataParser::read_null(void* context)
{
	static_cast<JSonDataParser*>(context)->readNull();
	return 1;
}

int JSonDataParser::read_boolean(void* context, int value)
{
	static_cast<JSonDataParser*>(context)->readBoolean(value);
	return 1;
}

int JSonDataParser::read_int(void* context, long long value)
{
	static_cast<JSonDataParser*>(context)->readInt(value);
	return 1;
}

int JSonDataParser::read_double(void* context, double value)
{
	static_cast<JSonDataParser*>(context)->readDouble(value);
	return 1;
}

int JSonDataParser::read_string(void* context, const unsigned char* value,
								size_t length, int constantPool)
{
	static_cast<JSonDataParser*>(context)->readString(value, length, constantPool);
	return 1;
}

int JSonDataParser::read_map_key(void* context, const unsigned char* name,
								 size_t length, int constantPool)
{
	return static_cast<JSonDataParser*>(context)->readMapKey(
		name, length, constantPool
	);
}

int JSonDataParser::read_end_map(void* context)
{
	static_cast<JSonDataParser*>(context)->readEndMap();
	return 1;
}

int JSonDataParser::read_start_array(void* context, unsigned int size)
{
	return static_cast<JSonDataParser*>(context)->readStartArray(size);
}

int JSonDataParser::read_end_array(void* context)
{
	return static_cast<JSonDataParser*>(context)->readEndArray();
}

int JSonDataParser::readStartMap(unsigned int)
{
	if (m_mapDepth++ == 0) {
		++m_recordCount;
		if (m_schemaComplete) {
			bool& insertByID = m_insertByID;
			BaseRealDataProducer* producer = m_producer;
			s64 recordID;
			if (insertByID) {
				insertByID = false;
				recordID = producer->insertRecord(m_beforeRecordID);
			} else {
				recordID = producer->insertRecord(producer->m_lastRecord);
			}
			return recordID != BaseRealDataProducer::INVALID_RECORD_ID;
		}
		m_producer->m_currentFields = m_producer->m_pendingFields;
	} else {
		m_state = PARSE_NESTED;
	}
	return 1;
}

int JSonDataParser::readNull()
{
	if ((m_state == PARSE_RECORD) && !m_schemaComplete) {
		setCurrentFieldType(IDataSource::TYPE_STR);
	}
	m_producer->setAsString(m_fieldIndex, NULL, 0);
	return 1;
}

int JSonDataParser::readBoolean(int value)
{
	if ((m_state == PARSE_RECORD) && !m_schemaComplete) {
		setCurrentFieldType(IDataSource::TYPE_BOOL);
	}
	m_producer->setAsBool(m_fieldIndex, value != 0);
	return 1;
}

int JSonDataParser::readInt(long long value)
{
	if ((m_state == PARSE_RECORD) && !m_schemaComplete) {
		setCurrentFieldType(IDataSource::TYPE_INT);
	}
	m_producer->setAsInt(m_fieldIndex, value);
	return 1;
}

int JSonDataParser::readDouble(double value)
{
	if ((m_state == PARSE_RECORD) && !m_schemaComplete) {
		setCurrentFieldType(IDataSource::TYPE_FLOAT);
	}
	m_producer->setAsFloat(m_fieldIndex, static_cast<float>(value));
	return 1;
}

int JSonDataParser::readString(const unsigned char* value, size_t length, int)
{
	if ((m_state == PARSE_RECORD) && !m_schemaComplete) {
		setCurrentFieldType(IDataSource::TYPE_STR);
	}
	m_producer->setAsString(m_fieldIndex,
		reinterpret_cast<const char*>(value), length);
	return 1;
}

int JSonDataParser::readMapKey(const unsigned char* name, size_t length,
							   int constantPool)
{
	if (m_state != PARSE_RECORD) {
		return 0;
	}
	if (!m_schemaComplete) {
		// スキーマ構築中のキーは、そのまま新しいフィールドになる。
		m_fieldIndex = m_producer->addField(
			reinterpret_cast<const char*>(name), length
		);
		return 1;
	}
	// 定数プールに解決済みのフィールド番号があればそれを使う。
	s32 fieldIndex = -1;
	if (constantPool >= 0) {
		fieldIndex = bjson_getCPCacheID(m_parser, constantPool);
	}
	if (fieldIndex != -1) {
		m_fieldIndex = fieldIndex;
		return 1;
	}
	fieldIndex = m_producer->getFieldIndex(
		reinterpret_cast<const char*>(name), length
	);
	m_fieldIndex = fieldIndex;
	if (constantPool >= 0) {
		// 名前検索の結果を定数プールへ覚えさせる。
		bjson_setCPCacheID(m_parser, constantPool, fieldIndex);
	}
	return 1;
}

int JSonDataParser::readEndMap()
{
	s32 mapDepth = --m_mapDepth;
	if (m_state == PARSE_RECORD) {
		if (!m_schemaComplete) {
			BaseRealDataProducer* producer = m_producer;
			if (m_insertByID) {
				m_insertByID = false;
				producer->insertRecord(m_beforeRecordID);
			} else {
				producer->insertRecord(producer->m_lastRecord);
			}
			m_producer->commitRecord();
			m_schemaComplete = true;
		}
		m_producer->commitRecordKey();
		mapDepth = m_mapDepth;
	}
	if (((mapDepth + static_cast<s32>(m_arrayDepth)) == 1) &&
		(m_state == PARSE_NESTED)) {
		m_state = PARSE_RECORD;
	}
	return 1;
}

int JSonDataParser::readStartArray(unsigned int)
{
	if (m_state != PARSE_RECORD) {
		if (m_state == PARSE_IDLE) {
			m_state = PARSE_RECORD;
		}
	} else {
		m_state = PARSE_NESTED;
	}
	++m_arrayDepth;
	return 1;
}

int JSonDataParser::readEndArray()
{
	s32 arrayDepth = --m_arrayDepth;
	if (((arrayDepth + static_cast<s32>(m_mapDepth)) == 2) &&
		(m_state == PARSE_NESTED)) {
		m_state = PARSE_RECORD;
	}
	return 1;
}

bool JSonDataParser::parse(const u8* source, size_t sourceLength,
						   BaseRealDataProducer* producer, s64 beforeRecordID,
						   u32* recordCount)
{
	if (!source || !sourceLength || !producer) return false;

	static yajl_callbacks callbacks = {
		read_null,
		read_boolean,
		read_int,
		read_double,
		NULL,
		read_string,
		read_start_map,
		read_map_key,
		read_end_map,
		read_start_array,
		read_end_array
	};

	yajl_handle parser = yajl_alloc(&callbacks, NULL, this);
	if (!parser) return false;

	m_parser = parser;
	m_producer = producer;
	m_beforeRecordID = beforeRecordID;
	m_insertByID = true;
	m_state = PARSE_IDLE;
	m_schemaComplete = producer->hasFields();
	m_arrayDepth = 0;
	m_fieldIndex = 0;
	m_mapDepth = 0;
	*recordCount = 0;
	m_recordCount = 0;

	yajl_config(parser, yajl_allow_comments, 1);
	yajl_status status = yajl_parse(parser, source, sourceLength);
	bool result = false;
	if (status == yajl_status_ok) {
		status = yajl_complete_parse(parser);
		if (status == yajl_status_ok) {
			*recordCount = m_recordCount;
			result = true;
		}
	}
	yajl_free(parser);
	return result;
}

DataSourceSubscription::DataSourceSubscription()
: m_notifier(NULL)
, m_callback(NULL)
, m_context(NULL)
{
}

DataSourceSubscription::~DataSourceSubscription()
{
}

void DataSourceSubscription::notify(s64 afterRecordID, s64 firstRecordID,
									u32 recordCount)
{
	dispatch(afterRecordID, firstRecordID, recordCount);
}

void DataSourceSubscription::setNotifier(bool subscribe, IDataSourceUpdateNotifier* notifier)
{
	if (subscribe) {
		if (m_notifier == notifier) return;
		if (m_notifier) {
			IDataSourceUpdateNotifier* previous = m_notifier;
			m_notifier = NULL;
			previous->setSubscription(false, NULL);
		}
		m_notifier = notifier;
		if (notifier) {
			notifier->setSubscription(true, this);
		}
	} else if (m_notifier) {
		IDataSourceUpdateNotifier* previous = m_notifier;
		m_notifier = NULL;
		previous->setSubscription(false, NULL);
	}
}
#include "CKLBDataTask.cpp"

JSonDB::JSonDB()
:m_startRecord			(NULL)
,mStringRecordBuffer	(NULL)
{
}

JSonDB::~JSonDB() {
	clean();
}

void JSonDB::clean() {
	RecordListHeader* pRecord = m_startRecord;
	while (pRecord) {
		RecordListHeader* pNextRecord = pRecord->pNextRecord;
		KLBDELETE(pRecord->fields);
		KLBDELETE(pRecord);
		pRecord = pNextRecord;
	}
	m_startRecord = NULL;

	KLBDELETEA(mStringRecordBuffer);
	mStringRecordBuffer = NULL;
}

/*static*/ int JSonDB::read_start_map		(void * ctx, unsigned int size)
{ return ((JSonDB*)ctx)->readStartMap(size); }
/*static*/ int JSonDB::read_null			(void * ctx)
{ return ((JSonDB*)ctx)->readNull(); }
/*static*/ int JSonDB::read_boolean			(void * ctx, int boolean)
{ return ((JSonDB*)ctx)->readBoolean(boolean); }
/*static*/ int JSonDB::read_int				(void * ctx, long long integerVal)
{ return ((JSonDB*)ctx)->readInt(integerVal); }
/*static*/ int JSonDB::read_double			(void * ctx, double doubleVal)
{ return ((JSonDB*)ctx)->readDouble(doubleVal); }
/*static*/ int JSonDB::read_string			(void * ctx, const unsigned char * stringVal, size_t stringLen, int cte_pool)
{ return ((JSonDB*)ctx)->readString(stringVal, stringLen,cte_pool); }
/*static*/ int JSonDB::read_map_key			(void * ctx, const unsigned char * stringVal, size_t stringLen, int cte_pool)
{ return ((JSonDB*)ctx)->readMapKey(stringVal, stringLen,cte_pool); }
/*static*/ int JSonDB::read_end_map			(void * ctx)
{ return ((JSonDB*)ctx)->readEndMap(); }
/*static*/ int JSonDB::read_start_array		(void * ctx, unsigned int size)
{ return ((JSonDB*)ctx)->readStartArray(size); }
/*static*/ int JSonDB::read_end_array		(void * ctx)
{ return ((JSonDB*)ctx)->readEndArray(); }

/*static*/
IDataSource* JSonDB::openDB(const u8* source, u32 sourceLength) {
	JSonDB* newDB = KLBNEW(JSonDB);
	if (!newDB->readDBInternal(source, sourceLength)) {
		KLBDELETE(newDB);
		newDB = NULL;
	}
	return newDB;
}

BaseRealDataProducer* BaseRealDataProducer::openLegacyJson(const u8* source, u32 sourceLength)
{
	IDataSource* legacy = JSonDB::openDB(source, sourceLength);
	if (!legacy) return NULL;

	BaseRealDataProducer* producer = create();
	u32 fieldCount = legacy->getFieldCount();
	if (fieldCount > MAX_FIELD_COUNT) {
		release(producer);
		KLBDELETE(legacy);
		return NULL;
	}

	for (u32 index = 0; index < fieldCount; ++index) {
		u32 length;
		const char* name = legacy->getFieldName(index, length);
		producer->addField(name, length);
		producer->setFieldType(index, legacy->getFieldType(index));
	}

	if (legacy->moveTo(0) != MOVE_EOF) {
		s64 recordID = 0;
		do {
			IDataRecord record = legacy->getRecord();
			producer->appendRecord();
			producer->m_lastRecord->recordID = recordID++;
			for (u32 index = 0; index < fieldCount; ++index) {
				switch (producer->getFieldType(index)) {
				case IDataSource::TYPE_INT:
					producer->setAsInt(index, legacy->getAsInt(record, index));
					break;
				case IDataSource::TYPE_STR:
					{
						u32 length;
						const char* value = legacy->getAsString(record, index, length);
						producer->setAsString(index, value, length);
					}
					break;
				case IDataSource::TYPE_BOOL:
					producer->setAsBool(index, legacy->getAsBool(record, index));
					break;
				case IDataSource::TYPE_FLOAT:
					producer->setAsFloat(index, legacy->getAsFloat(record, index));
					break;
				default:
					break;
				}
			}
		} while (legacy->moveNext() != MOVE_EOF);
	}

	producer->m_currentRecord = producer->m_records;
	producer->m_currentFields = producer->m_currentRecord ? producer->m_currentRecord->fields : NULL;
	KLBDELETE(legacy);
	return producer;
}


bool JSonDB::readDBInternal(const u8* source, u32 sourceLength) {
	bool res = false;
	m_baseRecordHeader.fields		= &m_field[0];
	m_baseRecordHeader.pNextRecord	= NULL;
	m_arrayCnt		= 0;
	m_record		= 0;
	m_recordEntry	= 0;
	m_stringAlloc	= 0;
	m_startRecord	= NULL;
	m_currRecIdx	= 0;
	m_mapCnt		= 0;
	m_res			= MOVE_UNFETCH;

	mStringRecordBuffer	= KLBNEWA(char, sourceLength);
	mStringRecordCount  = 0;
	m_StringRecordBufferSize = sourceLength;

	if (mStringRecordBuffer) {
		static yajl_callbacks callbacks = {  
			JSonDB::read_null,  
			JSonDB::read_boolean,  
			JSonDB::read_int,  
			JSonDB::read_double,  
			NULL,  
			JSonDB::read_string,  
			JSonDB::read_start_map,  
			JSonDB::read_map_key,  
			JSonDB::read_end_map,  
			JSonDB::read_start_array,  
			JSonDB::read_end_array
		};

		memcpy(mStringRecordBuffer, source, sourceLength);
		yajl_handle hand;
		/* generator config */  
//		yajl_gen g;				// My Context. 2012.12.06  使用していなかったのでコメントアウト
		yajl_status stat;  
  
//		g = yajl_gen_alloc(NULL); // 2012.12.06  使用していなかったのでコメントアウト

		/* ok.  open file.  let's read and parse */  
		hand = yajl_alloc(&callbacks, NULL, this);
		if (hand) {
			this->parserCtx = hand;

			/* and let's allow comments by default */  
			yajl_config(hand, yajl_allow_comments, 1);

			stat = yajl_parse(hand, (const unsigned char*)mStringRecordBuffer, sourceLength);

			if (stat == yajl_status_ok) {
				stat = yajl_complete_parse(hand);
				if (stat == yajl_status_ok) {
					res = true;
				}
			}
		}
	}

	if (!res) {
		clean();
	}
	m_recordPtr		= m_startRecord;
	return res;
}

int JSonDB::readNull()  
{
	if (m_record == 1) {
		// Set data type to entry -> String for null, only type tolerated as we do not tolerate sub object / array for now.
		setFieldType(m_entryIdx, TYPE_STR);
	}
	setString(m_entryIdx, NULL, 0);
    return 1;
}
  
int JSonDB::readBoolean(int boolean)  
{
	if (m_record == 1) {
		// Set data type to entry.
		setFieldType(m_entryIdx, TYPE_BOOL);
	}

	setBool(m_entryIdx, boolean);
	return 1;  
}  

int JSonDB::readInt(long long integerVal)  
{
	if (m_record == 1) {
		// Set data type to entry.
		setFieldType(m_entryIdx, TYPE_INT);
	}

	setInt(m_entryIdx, (s32)integerVal);
    return 1;
}
  
int JSonDB::readDouble(double doubleVal)  
{	
	if (m_record == 1) {
		// Set data type to entry.
		setFieldType(m_entryIdx, TYPE_FLOAT);
	}

	setFloat(m_entryIdx, (float)doubleVal);
	return 1;
}
  
int JSonDB::readString(const unsigned char * stringVal, size_t stringLen, int /*cte_pool*/)  
{
	if (m_record == 1) {
		// Set data type to entry.
		setFieldType(m_entryIdx, TYPE_STR);
	}

	setString(m_entryIdx, stringVal, stringLen);
	return 1;
}

/*static*/
int JSonDB::readMapKey(const unsigned char * stringVal, size_t stringLen, int cte_pool)  
{
	if (m_record == 1) {
		if (m_recordEntry < MAX_FIELD_PER_RECORD) {
			// Store name and index.
			m_entryIdx = addField(stringVal, stringLen);
			m_recordEntry++;
		} else {
			klb_assertAlways("Limit is 64 field / record");
			return 0;
		}
	} else {
		if (cte_pool > -1) {
			int id = bjson_getCPCacheID((yajl_handle)this->parserCtx, cte_pool);
			if (id != -1) {
				m_entryIdx = id;
				return 1;
			}
		}

		// remap key to index.
		m_entryIdx = getFieldID(stringVal, stringLen);
		if (m_entryIdx == -1) {
			klb_assertAlways("Record structure changed between records.");
		}

		if (cte_pool > -1) {
			bjson_setCPCacheID((yajl_handle)this->parserCtx, cte_pool, m_entryIdx);
		}
	}
	return 1;
}

int JSonDB::readStartMap(unsigned int /*size*/) {
	if (m_mapCnt == 0) {
		if (allocateRecord()) {
			m_record++;
		} else {
			return 0;
		}
	}
	return 1;
}

int JSonDB::readEndMap()  
{
	if (m_record == 1) {
		if (!allocateRecord()) {
			return 0;
		}
		copyFromTempToRecord();
		m_startRecord = m_currRecord;
	}
	return 1;  
}

int JSonDB::readStartArray(unsigned int /*size*/)
{
	if (m_arrayCnt == 0) {
		// First level
	} else {
		klb_assertAlways("Do not support nested arrays in DB for now");
	}
	m_arrayCnt++;
	return 1;
}

/*static*/ 
int JSonDB::readEndArray()  {
	m_arrayCnt--;
	return 1;
}

s32	 JSonDB::addField(const unsigned char* str, s32 strLen) {
	klb_assert((m_stringAlloc + strLen+1) < DB_MAX_STRING_POOL_SIZE, "Field Name String pool too small.");
	memcpy(&m_stringBuffer[m_stringAlloc], str, strLen);
	m_fieldName[m_recordEntry] = (u16)m_stringAlloc;
	m_stringBuffer[m_stringAlloc + strLen] = 0; // End string.
	m_stringAlloc += strLen + 1;
	return m_recordEntry;
}

void JSonDB::setFieldType	(s32 field, u8 type) {
	klb_assert(((u32)field) < MAX_FIELD_PER_RECORD, "Invalid field index");
	m_fieldType[field] = type;
}

bool JSonDB::allocateRecord	() {
	if (m_record != 0) {
		RecordListHeader* pRecord = (RecordListHeader*)KLBNEWA(u8, sizeof(RecordListHeader) + 4 + (m_recordEntry * sizeof(Field)));
		if (pRecord) {
			// Init new record.
			pRecord->pNextRecord	= NULL;
			pRecord->fields			= (Field*)&pRecord[1];	// Trick : use field after record header in memory.

			// Connect to link list and go to next element.
			m_currRecord->pNextRecord	= pRecord;
			m_currRecord				= pRecord;
		} else {
			return false;
		}
	} else {
		m_currRecord = &m_baseRecordHeader;
	}
	return true;
}

void JSonDB::copyFromTempToRecord() {
	// Move data to first allocated record
	memcpy(m_currRecord->fields, this->m_field,m_recordEntry * sizeof(Field));
}

void JSonDB::setInt			(s32 idx, s32 value) {
	klb_assert(m_fieldType[idx]==TYPE_INT,"Type does not match");
	klb_assert(((u32)idx) < m_recordEntry, "Field index out of range");
	m_currRecord->fields[idx].v.i = value;
}

void JSonDB::setFloat		(s32 idx, float value) {
	klb_assert(m_fieldType[idx]==TYPE_FLOAT,"Type does not match");
	klb_assert(((u32)idx) < m_recordEntry, "Field index out of range");
	m_currRecord->fields[idx].v.f = value;
}

void JSonDB::setBool		(s32 idx, s32 value) {
	klb_assert(m_fieldType[idx]==TYPE_BOOL,"Type does not match");
	klb_assert(((u32)idx) < m_recordEntry, "Field index out of range");
	m_currRecord->fields[idx].v.b = value ? true : false;
}

s32 JSonDB::getTotalRecordCount		() {
	return m_record;
}

s32 JSonDB::getCurrentRecordCount	() {
	return m_record;
}

bool JSonDB::fetchRecords			(u32 /*count*/) {
	// All fetched
	return true;
}

bool JSonDB::receivedUpdate			() {
	// Data always available
	return true;
}

const char*	JSonDB::getFieldName(u32 index, u32& len) {
	klb_assert(index < m_recordEntry, "Field index out of range");
	len = strlen(&m_stringBuffer[m_fieldName[index]]);
	return &m_stringBuffer[m_fieldName[index]];
}

s32 JSonDB::getFieldID(const unsigned char* str, s32 strLen) {
	klb_assert((m_stringAlloc + strLen+1) < DB_MAX_STRING_POOL_SIZE, "Field Name String pool too small for temp usage.");

	// Copy and create temp string (do NOT modify m_stringAlloc : no allocation really done)
	memcpy(&m_stringBuffer[m_stringAlloc], str, strLen);
	m_stringBuffer[m_stringAlloc + strLen] = 0; // End string.

	return this->getFieldIndex(&m_stringBuffer[m_stringAlloc]);
}

void JSonDB::setString		(s32 idx, const unsigned char* str, s32 strLen) { 
	klb_assert(m_fieldType[idx]==TYPE_STR,"Type does not match");
	klb_assert(((u32)idx) < m_recordEntry, "Field index out of range");
	klb_assert((mStringRecordCount+strLen) < m_StringRecordBufferSize, "String buffer full");

	if (str && strLen) {
		memcpy(&mStringRecordBuffer[mStringRecordCount], str, strLen);
		mStringRecordBuffer[mStringRecordCount + strLen] = 0; // End string.

		m_currRecord->fields[idx].v.str = mStringRecordCount;
		mStringRecordCount += strLen + 1;
	} else {
		m_currRecord->fields[idx].v.str = -1;
	}
}

const char*	JSonDB::getAsString	(IDataRecord rec, u32 index, u32& len) {
	klb_assert(m_fieldType[index]==TYPE_STR,"Type does not match");
	klb_assert(index < m_recordEntry, "Field index out of range");
	RecordListHeader* pRecord = (RecordListHeader*)rec;
	const char* res;
	if (pRecord->fields[index].v.str != -1) {
		res = &mStringRecordBuffer[pRecord->fields[index].v.str];
		len = strlen(res);
	} else {
		res = NULL;
		len = 0;
	}
	return res;
}

EMOVECODE JSonDB::moveTo(u32 rec) {
	RecordListHeader* p = m_startRecord;
	u32 counter = 0;
	while (p) {
		if (counter == rec) {
			m_res			= MOVE_SUCCEED;
			m_recordPtr		= p;
			m_currRecIdx	= rec;
			return m_res;
		}
	}

	m_res		= MOVE_EOF;
	m_recordPtr = p;
	m_currRecIdx= m_record;
	return m_res;
}

EMOVECODE JSonDB::moveNext() {
	if (m_currRecIdx < m_record) {
		m_currRecIdx++;
		m_recordPtr = m_recordPtr->pNextRecord;
	} else {
		m_recordPtr = NULL;
	}

	m_res = (m_recordPtr != NULL) ? MOVE_SUCCEED : MOVE_EOF;
	return m_res;
}

EMOVECODE JSonDB::movePrevious() {
	if (m_currRecIdx > 0) {
		m_currRecIdx--;
		moveTo(m_currRecIdx);
	} else {
		m_recordPtr = NULL;
	}
	m_res = (m_recordPtr != NULL) ? MOVE_SUCCEED : MOVE_EOF;
	return m_res;
}

IDataRecord JSonDB::getRecord() {
	if (m_res == MOVE_SUCCEED) {
		return (IDataRecord)m_recordPtr;
	} else {
		return NULL;
	}
}

u32	JSonDB::getFieldCount() {
	return this->m_recordEntry;
}

u32	JSonDB::getFieldType(u32 index) {
	klb_assert(((u32)index) < MAX_FIELD_PER_RECORD, "Invalid field index");
	return m_fieldType[index];
}

u32	JSonDB::getFieldIndex(const char* fieldName) {
	for (u32 idx = 0; idx < m_recordEntry; idx++) {
		if (strcmp(fieldName, &this->m_stringBuffer[m_fieldName[idx]]) == 0) {
			return idx;
		}
	}
	return 0xFFFFFFFF;
}

s32	JSonDB::getAsInt		(IDataRecord rec, u32 index) {
	klb_assert(m_fieldType[index]==TYPE_INT,"Type does not match");
	klb_assert(index < m_recordEntry, "Field index out of range");
	RecordListHeader* pRecord = (RecordListHeader*)rec;
	return pRecord->fields[index].v.i;
}

bool JSonDB::getAsBool		(IDataRecord rec, u32 index) {
	klb_assert(m_fieldType[index]==TYPE_BOOL,"Type does not match");
	klb_assert(index < m_recordEntry, "Field index out of range");
	RecordListHeader* pRecord = (RecordListHeader*)rec;
	return pRecord->fields[index].v.b;
}

float JSonDB::getAsFloat	(IDataRecord rec, u32 index) {
	klb_assert(m_fieldType[index]==TYPE_FLOAT,"Type does not match");
	klb_assert(index < m_recordEntry, "Field index out of range");
	RecordListHeader* pRecord = (RecordListHeader*)rec;
	return pRecord->fields[index].v.f;
}

/*
static const char* testStr = "[ { \"hello\": 1, \"valuebool\": true, \"nilfield\": null, \"strfield\": \"hello world\" }, { \"hello\": 2, \"valuebool\": false, \"nilfield\": \"string guys\", \"strfield\": \"hello world 2\" } ]";

class DInitializer {
public:
	DInitializer();
};

// Global variable force constructor exec.
DInitializer _gInit;

DInitializer::DInitializer() {
	IDataSource* pDBObj = JSonDB::openDB((const u8*)testStr, strlen(testStr));
}
*/
