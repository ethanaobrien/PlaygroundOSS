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
#ifndef CKLBCollisionManager_h
#define CKLBCollisionManager_h

#include <list>
#include "CKLBLuaTask.h"

struct cpSpace;
struct cpShape;
struct cpBody;

typedef double cpFloat;

struct cpVect {
	cpFloat x;
	cpFloat y;
};

struct cpBB {
	cpFloat l;
	cpFloat b;
	cpFloat r;
	cpFloat t;
};

inline cpBB cpBBNewForExtents(cpVect center, cpFloat halfWidth, cpFloat halfHeight)
{
	cpBB bounds = {
		center.x - halfWidth,
		center.y - halfHeight,
		center.x + halfWidth,
		center.y + halfHeight
	};
	return bounds;
}

inline cpBB cpBBNewForCircle(cpVect center, cpFloat radius)
{
	return cpBBNewForExtents(center, radius, radius);
}

extern "C" {
	cpFloat cpMomentForCircle(cpFloat mass, cpFloat innerRadius,
		cpFloat outerRadius, cpVect offset);
	cpFloat cpMomentForBox2(cpFloat mass, cpBB box);
	cpBody* cpBodyNew(cpFloat mass, cpFloat moment);
	cpBody* cpBodyNewStatic();
	cpShape* cpCircleShapeNew(cpBody* body, cpFloat radius, cpVect offset);
	cpShape* cpBoxShapeNew2(cpBody* body, cpBB box, cpFloat radius);
	void cpShapeSetFriction(cpShape* shape, cpFloat friction);
	void cpBodySetPosition(cpBody* body, cpVect position);
	void cpBodySetAngle(cpBody* body, cpFloat angle);
	cpVect cpBodyGetPosition(const cpBody* body);
	cpFloat cpBodyGetAngle(const cpBody* body);
	cpBody* cpSpaceAddBody(cpSpace* space, cpBody* body);
	cpShape* cpSpaceAddShape(cpSpace* space, cpShape* shape);
	void cpSpaceRemoveShape(cpSpace* space, cpShape* shape);
	void cpShapeFree(cpShape* shape);
	void cpSpaceRemoveBody(cpSpace* space, cpBody* body);
	void cpBodyFree(cpBody* body);
	cpSpace* cpSpaceNew();
	void cpSpaceSetGravity(cpSpace* space, cpVect gravity);
	void cpSpaceStep(cpSpace* space, cpFloat seconds);
	void cpSpaceFree(cpSpace* space);
}

class CKLBCollisionManager : public CKLBLuaTask
{
	friend class CKLBTaskFactory<CKLBCollisionManager>;
public:
	struct CollisionObject {
		CollisionObject() {}

		CollisionObject(s32 identifier, cpSpace* space, bool isStatic,
			cpFloat x, cpFloat y, cpFloat radius, cpFloat angle,
			cpFloat mass, cpFloat friction);
		CollisionObject(s32 identifier, cpSpace* space, bool isStatic,
			cpFloat x, cpFloat y, cpFloat width, cpFloat height,
			cpFloat angle, cpFloat mass, cpFloat friction);

		~CollisionObject() {
			CKLBCollisionManager::removeObject(this);
		}

		int			identifier;
		int			shapeKind;
		cpBB		bounds;
		cpSpace*	space;
		cpShape*	shape;
		cpBody*		body;
		bool		isStatic;
		cpFloat		mass;
		cpFloat		friction;
	};

protected:
	CKLBCollisionManager();
	virtual ~CKLBCollisionManager();

public:
	static void removeObject(CollisionObject* object);

	u32 getClassID();
	bool initScript(CLuaState& lua);
	int commandScript(CLuaState& lua);
	void execute(u32 deltaT);
	void die();

private:
	enum ObjectSet {
		DYNAMIC_OBJECTS = 0,
		STATIC_OBJECTS,
		OBJECT_SET_COUNT
	};

	struct EraseRequest {
		s32		index;
		bool	isStatic;
	};

	typedef int (CKLBCollisionManager::*CommandHandler)(CLuaState& lua);

	int cmdSetActive(CLuaState& lua);
	int cmdAddObject(CLuaState& lua);
	int cmdEraseObject(CLuaState& lua);
	int cmdGetObjInfo(CLuaState& lua);

	cpSpace*			m_space;
	bool				m_active;
	CommandHandler		m_commands[4];
	const char*			m_callback;
	CollisionObject**	m_objects[OBJECT_SET_COUNT];
	s32					m_objectIndices[OBJECT_SET_COUNT];
	s32					m_objectCapacities[OBJECT_SET_COUNT];
	std::list<EraseRequest>*	m_eraseQueue;
	float				m_screenHeight;
};

#endif // CKLBCollisionManager_h
