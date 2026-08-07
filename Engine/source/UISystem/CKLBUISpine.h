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
#ifndef CKLBUISpine_h
#define CKLBUISpine_h

#include <map>
#include <string>
#include <vector>
#include <spine/AnimationState.h>
#include "CKLBUITask.h"

struct spAtlas;
struct spSkeletonData;
struct spAnimationStateData;
struct spAnimationState;
struct spSkeleton;
struct spTrackEntry;
struct spSlot;
struct spEvent;
class CKLBDynSprite;

class CKLBUISpine : public CKLBUITask
{
	friend class CKLBTaskFactory<CKLBUISpine>;

private:
	CKLBUISpine();
	virtual ~CKLBUISpine();

public:
	enum {
		CLASS_ID = 0x0008008a
	};

	u32 getClassID();
	static CKLBUISpine* create(CKLBUITask* parent, u32 order, float x, float y,
							  const char* assetName);
	void execute(u32 deltaT);

protected:
	bool initUI(CLuaState& lua);
	int commandUI(CLuaState& lua, int argc, int cmd);
	void dieUI();

private:
	void commandStop(CLuaState& lua);
	void commandPlay(CLuaState& lua);
	void commandSetAnimation(CLuaState& lua);
	void commandRegisterEvent(CLuaState& lua);
	static void animationListener(
		spAnimationState* state,
		spEventType type,
		spTrackEntry* entry,
		spEvent* event);
	void callEventCallback(const char* eventName, const char* stringValue);
	void callEventCallback(spEvent* event);

	bool init(CKLBUITask* parent, u32 order, float x, float y,
			  const char* assetName);
	bool initCore(u32 order, float x, float y, const char* assetName);
	void updateAttachments();
	void buildRegionSprite(
		spSlot* slot, void* attachment, void* renderCommand, int slotIndex);
	void updateRegionSprite(
		spSlot* slot, void* attachment, void* renderCommand);
	void buildRegionMesh(
		spSlot* slot, void* attachment, void* renderCommand, int slotIndex);
	void updateRegionMesh(
		spSlot* slot, void* attachment, void* renderCommand);

	struct SpineCommand {
		void (CKLBUISpine::*handler)(CLuaState&);
	};

	static PROP_V2 ms_propItems[];

	const char*					m_assetName;
	u32							m_order;
	spAtlas*					m_atlas;
	spSkeletonData*			m_skeletonData;
	spAnimationStateData*	m_animationStateData;
	spAnimationState*		m_animationState;
	spSkeleton*				m_skeleton;
	spTrackEntry*				m_track;
	std::vector<CKLBDynSprite*>*	m_slotSprites;
	SpineCommand				m_commands[4];
	s32							m_playing;
	std::map<std::string, std::string> m_eventCallbacks;
};

#endif // CKLBUISpine_h
