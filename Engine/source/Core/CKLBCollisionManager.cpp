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
#include "CKLBCollisionManager.h"
#include "CKLBDrawTask.h"
#include "CKLBUtility.h"
#include "CKLBScriptEnv.h"
#include <string.h>

enum {
	CM_SET_ACTIVE = 0,
	CM_ADD_OBJ,
	CM_ERASE_OBJ,
	CM_GET_OBJ_INFO
};

static IFactory::DEFCMD cmd[] = {
	{ "CM_SET_ACTIVE", CM_SET_ACTIVE },
	{ "CM_ADD_OBJ", CM_ADD_OBJ },
	{ "CM_ERASE_OBJ", CM_ERASE_OBJ },
	{ "CM_GET_OBJ_INFO", CM_GET_OBJ_INFO },
	{ NULL, 0 }
};

static CKLBTaskFactory<CKLBCollisionManager> factory(
	"CollisionManager", 135 | CLS_KLBUSERTASK, cmd);

static const cpFloat PI = 3.141592653589793;

CKLBCollisionManager::CKLBCollisionManager()
: CKLBLuaTask()
, m_space(NULL)
, m_active(false)
{
	m_screenHeight = (float)CKLBDrawResource::getInstance().height();

	m_objects[DYNAMIC_OBJECTS] = NULL;
	m_objects[STATIC_OBJECTS] = NULL;
	m_objectIndices[DYNAMIC_OBJECTS] = 0;
	m_objectIndices[STATIC_OBJECTS] = 0;
	m_eraseQueue = KLBNEW(std::list<EraseRequest>);
}

CKLBCollisionManager::~CKLBCollisionManager()
{
	for(s32 set = 0; set < OBJECT_SET_COUNT; ++set) {
		for(s32 index = 0; index < m_objectCapacities[set]; ++index) {
			CollisionObject* object = m_objects[set][index];
			if(object) {
				KLBDELETE(object);
			}
		}
		KLBDELETEA(m_objects[set]);
		m_objects[set] = NULL;
	}

	KLBDELETE(m_eraseQueue);
	m_eraseQueue = NULL;

	KLBDELETEA(m_callback);
	cpSpaceFree(m_space);
}

u32
CKLBCollisionManager::getClassID()
{
	return 135 | CLS_KLBUSERTASK;
}

void
CKLBCollisionManager::removeObject(CollisionObject* object)
{
	cpSpaceRemoveShape(object->space, object->shape);
	if (object->shape) {
		cpShapeFree(object->shape);
	}
	cpSpaceRemoveBody(object->space, object->body);
	if (object->body) {
		cpBodyFree(object->body);
	}
}

bool
CKLBCollisionManager::initScript(CLuaState& lua)
{
	s32 argc = lua.numArgs();
	klb_assertNull(argc >= 1, "Invalid Arguments");

	m_callback = NULL;
	m_callback = CKLBUtility::copyString(lua.getString(1));

	cpFloat gravity = -9.8;
	if(argc > 1 && !lua.isNil(2)) {
		gravity = lua.getDouble(2);
	}

	m_space = cpSpaceNew();
	cpVect gravityVector = { 0.0, gravity };
	cpSpaceSetGravity(m_space, gravityVector);

	if(!(argc > 2)) {
		m_objectCapacities[DYNAMIC_OBJECTS] = 64;
	} else {
		if(!lua.isNil(3)) {
			m_active = lua.getBool(3);
		}
		m_objectCapacities[DYNAMIC_OBJECTS] = 64;
		if(argc >= 4) {
			if(!lua.isNil(4)) {
				m_objectCapacities[DYNAMIC_OBJECTS] = lua.getInt(4);
			}
		}
	}

	m_objectCapacities[STATIC_OBJECTS] = 32;
	s32 staticCapacity = 32;
	if(argc >= 5) {
		if(lua.isNil(5)) {
			staticCapacity = m_objectCapacities[STATIC_OBJECTS];
		} else {
			staticCapacity = lua.getInt(5);
			m_objectCapacities[STATIC_OBJECTS] = staticCapacity;
		}
	}

	m_objects[DYNAMIC_OBJECTS] = KLBNEWA(
		CollisionObject*, m_objectCapacities[DYNAMIC_OBJECTS]);
	memset(m_objects[DYNAMIC_OBJECTS], 0,
		(size_t)m_objectCapacities[DYNAMIC_OBJECTS] * sizeof(CollisionObject*));
	m_objects[STATIC_OBJECTS] = KLBNEWA(CollisionObject*, staticCapacity);
	memset(m_objects[STATIC_OBJECTS], 0,
		(size_t)staticCapacity * sizeof(CollisionObject*));

	m_commands[CM_SET_ACTIVE] = &CKLBCollisionManager::cmdSetActive;
	m_commands[CM_ADD_OBJ] = &CKLBCollisionManager::cmdAddObject;
	m_commands[CM_ERASE_OBJ] = &CKLBCollisionManager::cmdEraseObject;
	m_commands[CM_GET_OBJ_INFO] = &CKLBCollisionManager::cmdGetObjInfo;

	return regist(NULL, P_NORMAL);
}

int
CKLBCollisionManager::commandScript(CLuaState& lua)
{
	klb_assertNull(lua.numArgs() >= 2, "Invalid Arguments");
	s32 command = lua.getInt(2);
	klb_assertNull((u32)command < 4, "Invalid Arguments");
	return (this->*m_commands[command])(lua);
}

int
CKLBCollisionManager::cmdSetActive(CLuaState& lua)
{
	klb_assertNull(lua.numArgs() == 3, "Invalid Arguments");
	m_active = lua.getBool(3);
	return 0;
}

int
CKLBCollisionManager::cmdAddObject(CLuaState& lua)
{
	s32 argc = lua.numArgs();
	klb_assertNull(argc >= 11, "Invalid Arguments");

	s32 shapeKind = lua.getInt(3);
	klb_assertNull(shapeKind < 2, "Unsupported type");
	bool isStatic = lua.getBool(4);
	cpFloat width = (cpFloat)lua.getInt(5);
	cpFloat height = (cpFloat)lua.getInt(6);
	cpFloat x = (cpFloat)lua.getInt(7);
	float screenHeight = m_screenHeight;
	cpFloat y = (cpFloat)lua.getInt(8);
	cpFloat angle = (cpFloat)lua.getFloat(9);
	cpFloat mass = lua.getDouble(10);
	cpFloat friction = lua.getDouble(11);
	cpFloat rotation = -angle;
	y = (cpFloat)screenHeight - y;

	s32* capacity;
	CollisionObject** objects = m_objects[DYNAMIC_OBJECTS];
	s32* cursor = &m_objectIndices[DYNAMIC_OBJECTS];
	capacity = &m_objectCapacities[DYNAMIC_OBJECTS];
	if(isStatic) {
		objects = m_objects[STATIC_OBJECTS];
		cursor = &m_objectIndices[STATIC_OBJECTS];
		capacity = &m_objectCapacities[STATIC_OBJECTS];
	}

	s32 objectCapacity = *capacity;
	if(objectCapacity > 0) {
		s32 index = *cursor;
		s32 attempts = 0;
		do {
			if(!objects[index]) {
				CollisionObject* object;
				if(shapeKind == 0) {
					object = KLBNEWC(CollisionObject, (index + 1, m_space,
						isStatic, x, y, width, height, rotation, mass, friction));
				} else if(shapeKind == 1) {
					object = KLBNEWC(CollisionObject, (index + 1, m_space,
						isStatic, x, y, width * 0.5, rotation, mass, friction));
				} else {
					klb_assertAlways("impossible to reach here");
					return 0;
				}

				s32 slot = *cursor;
				objects[slot] = object;
				lua.retInt(slot + 1);
				*cursor = *cursor + 1;
				return 1;
			}
			++attempts;
			index = (index + 1) % objectCapacity;
			*cursor = index;
		} while(attempts < objectCapacity);
	}

	klb_assertNull(false, "There are too many objects !");
	return 0;
}

int
CKLBCollisionManager::cmdEraseObject(CLuaState& lua)
{
	klb_assertNull(lua.numArgs() == 4, "Invalid Arguments");
	s32 index = lua.getInt(3);
	s32 objectIndex = index - 1;
	bool isStatic = lua.getBool(4);

	s32 capacity = isStatic
		? m_objectCapacities[STATIC_OBJECTS]
		: m_objectCapacities[DYNAMIC_OBJECTS];
	klb_assertNull(index <= capacity, "invalid ojbect index");
	EraseRequest request;
	request.index = objectIndex;
	request.isStatic = isStatic;
	m_eraseQueue->push_back(request);
	return 0;
}

int
CKLBCollisionManager::cmdGetObjInfo(CLuaState& lua)
{
	s32 argc = lua.numArgs();
	klb_assertNull(argc >= 3, "Invalid Arguments");
	bool isDynamic = !lua.getBool(3);

	CollisionObject** objects;
	s32 capacity;
	if(isDynamic) {
		objects = m_objects[DYNAMIC_OBJECTS];
		capacity = m_objectCapacities[DYNAMIC_OBJECTS];
	} else {
		objects = m_objects[STATIC_OBJECTS];
		capacity = m_objectCapacities[STATIC_OBJECTS];
	}
	if(argc >= 4) {
		s32 index = lua.getInt(4);
		klb_assertNull(index <= capacity, "invalid ojbect index");
		CollisionObject* object = objects[index - 1];
		if(object) {
			cpVect position = cpBodyGetPosition(object->body);
			cpFloat angle = (cpBodyGetAngle(object->body) * -180.0) / PI;
			s32 y = (s32)(m_screenHeight - (float)(s32)position.y);
			s32 x = (s32)position.x;
			CKLBScriptEnv::getInstance().call_eventPhysicsObject(
				m_callback, !isDynamic, object->identifier, x, y, angle);
		}
	} else {
		for(s32 index = 0; index < capacity; ++index) {
			CollisionObject* object = objects[index];
			if(object) {
				cpVect position = cpBodyGetPosition(object->body);
				cpFloat angle = (cpBodyGetAngle(object->body) * -180.0) / PI;
				s32 y = (s32)(m_screenHeight - (float)(s32)position.y);
				s32 x = (s32)position.x;
				CKLBScriptEnv::getInstance().call_eventPhysicsObject(
					m_callback, !isDynamic, object->identifier, x, y, angle);
			}
		}
	}
	return 0;
}

CKLBCollisionManager::CollisionObject::CollisionObject(s32 identifier,
	cpSpace* space, bool isStatic, cpFloat x, cpFloat y, cpFloat radius,
	cpFloat angle, cpFloat mass, cpFloat friction)
{
	this->identifier = identifier;
	this->shapeKind = 1;
	this->space = space;
	this->isStatic = isStatic;
	this->mass = mass;
	this->friction = friction;
	cpVect origin = { 0.0, 0.0 };
	this->bounds = cpBBNewForCircle(origin, radius);
	cpFloat moment = cpMomentForCircle(mass, 0.0, radius, origin);
	this->body = isStatic ? cpBodyNewStatic() : cpBodyNew(mass, moment);
	this->shape = cpCircleShapeNew(this->body, radius, origin);
	cpShapeSetFriction(this->shape, friction);

	cpVect position = { x, y };
	cpBodySetPosition(this->body, position);
	cpBodySetAngle(this->body, angle * PI / 180.0);
	cpSpaceAddBody(this->space, this->body);
	cpSpaceAddShape(this->space, this->shape);
}

CKLBCollisionManager::CollisionObject::CollisionObject(s32 identifier,
	cpSpace* space, bool isStatic, cpFloat x, cpFloat y,
	cpFloat width, cpFloat height, cpFloat angle, cpFloat mass,
	cpFloat friction)
{
	this->identifier = identifier;
	this->shapeKind = 0;
	this->space = space;
	this->isStatic = isStatic;
	this->mass = mass;
	this->friction = friction;
	cpVect origin = { 0.0, 0.0 };
	this->bounds = cpBBNewForExtents(origin, width * 0.5, height * 0.5);

	cpFloat moment = cpMomentForBox2(mass, this->bounds);
	this->body = isStatic ? cpBodyNewStatic() : cpBodyNew(mass, moment);
	this->shape = cpBoxShapeNew2(this->body, this->bounds, 0.0);
	cpShapeSetFriction(this->shape, friction);

	cpVect position = { x, y };
	cpBodySetPosition(this->body, position);
	cpBodySetAngle(this->body, angle * PI / 180.0);
	cpSpaceAddBody(this->space, this->body);
	cpSpaceAddShape(this->space, this->shape);
}

void
CKLBCollisionManager::execute(u32 deltaT)
{
	if(!m_eraseQueue->empty()) {
		std::list<EraseRequest>::iterator iterator = m_eraseQueue->begin();
		while(iterator != m_eraseQueue->end()) {
			CollisionObject** objects =
				!iterator->isStatic
					? m_objects[DYNAMIC_OBJECTS]
					: m_objects[STATIC_OBJECTS];
			s32 index = iterator->index;
			CollisionObject* object = objects[index];
			if(object) {
				KLBDELETE(object);
				index = iterator->index;
			}
			objects[index] = NULL;
			++iterator;
		}
		m_eraseQueue->clear();
	}

	cpFloat seconds = (cpFloat)deltaT / 1000.0;
	if(m_space && m_active) {
		cpSpaceStep(m_space, seconds);
	}
}

void
CKLBCollisionManager::die()
{
}
