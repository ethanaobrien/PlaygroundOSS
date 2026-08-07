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
#include "CKLBUISpine.h"
#include "CKLBDataHandler.h"
#include "CKLBScriptEnv.h"
#include "CKLBUtility.h"
#include "CKLBRendering.h"
#include "TextureManagement.h"
#include <sstream>
#include <spine/spine.h>
#include <spine/extension.h>

namespace {
struct SpinePageAsset {
	u32 handle;
	CKLBImageAsset* asset;
};

static CKLBImageAsset*
getAttachmentImage(void* rendererObject)
{
	spAtlasRegion* region =
		static_cast<spAtlasRegion*>(rendererObject);
	SpinePageAsset* pageAsset =
		static_cast<SpinePageAsset*>(region->page->rendererObject);
	return pageAsset->asset;
}

static u32
packSpineColor(const spSkeleton* skeleton, const spSlot* slot)
{
	u8 red = static_cast<u8>(
		skeleton->color.r * slot->color.r * 255.0f);
	u8 green = static_cast<u8>(
		skeleton->color.g * slot->color.g * 255.0f);
	u8 blue = static_cast<u8>(
		skeleton->color.b * slot->color.b * 255.0f);
	s32 alpha = static_cast<s32>(
		skeleton->color.a * slot->color.a * 255.0f);
	return (alpha << 24)
		| (blue << 16)
		| (green << 8)
		| red;
}
}

extern "C" void
_spAtlasPage_createTexture(spAtlasPage* page, const char* path)
{
	u32 handle = 0;
	CKLBImageAsset* image = static_cast<CKLBImageAsset*>(
		CKLBUtility::loadAssetScript(path, &handle, NULL, false));
	SpinePageAsset* pageAsset = new SpinePageAsset;
	pageAsset->handle = handle;
	pageAsset->asset = image;
	page->rendererObject = pageAsset;
	CKLBTextureAsset* texture = image->getTexture();
	page->width = texture->m_width;
	page->height = texture->m_height;
}

extern "C" void
_spAtlasPage_disposeTexture(spAtlasPage* page)
{
	SpinePageAsset* pageAsset =
		static_cast<SpinePageAsset*>(page->rendererObject);
	if (pageAsset) {
		CKLBDataHandler::releaseHandle(pageAsset->handle);
		delete pageAsset;
	}
}

extern "C" char*
_spUtil_readFile(const char* path, int* length)
{
	IPlatformRequest& platform = CPFInterface::getInstance().platform();
	IReadStream* stream =
		platform.openReadStream(path, platform.useEncryption(), 14);
	if (!stream || stream->getStatus() != IReadStream::NORMAL) {
		return NULL;
	}
	*length = stream->getSize() - stream->getPosition();
	char* data = MALLOC(char, *length + 1);
	if (data) {
		stream->readBlock(data, *length);
	}
	delete stream;
	return data;
}

enum {
	START_ANIM,
	STOP_ANIM,
	SET_ANIM,
	SET_EVENT_CB
};

static IFactory::DEFCMD cmd[] = {
	{ "START_ANIM",   START_ANIM   },
	{ "STOP_ANIM",    STOP_ANIM    },
	{ "SET_ANIM",     SET_ANIM     },
	{ "SET_EVENT_CB", SET_EVENT_CB },
	{ 0, 0 }
};

// Spine animation callbacks identify their state object. Keep the owning UI
// task available without placing engine state inside the Spine runtime.
static std::map<spAnimationState*, CKLBUISpine*> spineTasks;

static CKLBTaskFactory<CKLBUISpine> factory("UI_Spine", CKLBUISpine::CLASS_ID, cmd);

CKLBLuaPropTask::PROP_V2 CKLBUISpine::ms_propItems[] = {
	UI_BASE_PROP
};

CKLBUISpine::CKLBUISpine()
: CKLBUITask(P_UIAFTER)
, m_assetName(NULL)
, m_atlas(NULL)
, m_skeletonData(NULL)
, m_animationStateData(NULL)
, m_animationState(NULL)
, m_skeleton(NULL)
, m_track(NULL)
{
	m_newScriptModel = true;
}

CKLBUISpine::~CKLBUISpine()
{
	KLBDELETEA(m_assetName);
}

CKLBUISpine*
CKLBUISpine::create(CKLBUITask* parent, u32 order, float x, float y,
					const char* assetName)
{
	CKLBUISpine* task = KLBNEW(CKLBUISpine);
	if (!task) {
		return NULL;
	}
	if (!task->init(parent, order, x, y, assetName)) {
		KLBDELETE(task);
		return NULL;
	}
	return task;
}

bool
CKLBUISpine::init(CKLBUITask* parent, u32 order, float x, float y,
				  const char* assetName)
{
	if (!setupNode()) {
		return false;
	}
	return registUI(parent, initCore(order, x, y, assetName));
}

bool
CKLBUISpine::initUI(CLuaState& lua)
{
	if (lua.numArgs() != 5) {
		return false;
	}

	float x = lua.getFloat(3);
	float y = lua.getFloat(4);
	u32 order = lua.getInt(2);
	const char* assetName = lua.getString(5);
	bool result = initCore(order, x, y, assetName);

	m_commands[0].handler = &CKLBUISpine::commandStop;
	m_commands[1].handler = &CKLBUISpine::commandPlay;
	m_commands[2].handler = &CKLBUISpine::commandSetAnimation;
	m_commands[3].handler = &CKLBUISpine::commandRegisterEvent;
	return result;
}

void
CKLBUISpine::commandStop(CLuaState& /* lua */)
{
	m_playing = 0;
}

void
CKLBUISpine::commandPlay(CLuaState& /* lua */)
{
	m_playing = 1;
}

void
CKLBUISpine::commandSetAnimation(CLuaState& lua)
{
	int argc = lua.numArgs();
	klb_assertNull(argc >= 3, "SPINE : too few param");

	spAnimation* animation;
	if (lua.isNum(3)) {
		animation = m_skeletonData->animations[lua.getInt(3)];
	} else {
		klb_assert(lua.isString(3), "SPINE : wrong cmd idx");
		const char* animationName = lua.getString(3);
		animation = spSkeletonData_findAnimation(m_skeletonData,
												 animationName);
	}
	klb_assertNull(animation, "SPINE : can NOT find anim");

	bool loop = true;
	if (argc > 3) {
		loop = lua.getBoolean(4);
	}
	m_track = spAnimationState_setAnimation(m_animationState, 0, animation, loop);
	spAnimationState_apply(m_animationState, m_skeleton);
	updateAttachments();
}

void
CKLBUISpine::commandRegisterEvent(CLuaState& lua)
{
	klb_assertNull(lua.numArgs() == 4, "SPINE : wrong params");

	const char* eventName = lua.getString(3);
	const char* callback = lua.getString(4);
	std::map<std::string, std::string>::iterator entry =
		m_eventCallbacks.find(eventName);
	if (entry != m_eventCallbacks.end()) {
		entry->second = callback;
	} else {
		m_eventCallbacks.insert(
			std::make_pair(std::string(eventName), std::string(callback)));
	}
}

u32
CKLBUISpine::getClassID()
{
	return CLASS_ID;
}

void
CKLBUISpine::buildRegionSprite(
	spSlot* slot, void* attachment, void* renderCommand, int slotIndex)
{
	CKLBDynSprite* sprite = static_cast<CKLBDynSprite*>(renderCommand);
	u16* indices = sprite->getSrcIndexBuffer();
	indices[0] = 0;
	indices[1] = 1;
	indices[2] = 3;
	indices[3] = 1;
	indices[4] = 2;
	indices[5] = 3;

	updateRegionSprite(slot, attachment, renderCommand);
	sprite->changeOrder(
		CKLBRenderingManager::getInstance(), m_order + slotIndex);
}

bool
CKLBUISpine::initCore(
	u32 order, float x, float y, const char* assetName)
{
	if (!setupPropertyList(
		reinterpret_cast<const char**>(ms_propItems),
		SizeOfArray(ms_propItems))) {
		return false;
	}

	setInitPos(x, y);
	REFRESH_A;
	klb_assert(static_cast<s32>(order) >= 0, "Order Problem");
	m_order = order;
	setStrC(m_assetName, assetName);
	m_playing = 1;

	std::stringstream path;
	path << assetName << ".atlas";
	m_atlas = spAtlas_createFromFile(path.str().c_str(), NULL);

	path.str(""); path.clear();
	path << assetName << ".json";
	spSkeletonJson* json = spSkeletonJson_create(m_atlas);
	m_skeletonData =
		spSkeletonJson_readSkeletonDataFile(json, path.str().c_str());
	klb_assertNull(
		m_skeletonData,
		"failed to load json");
	spSkeletonJson_dispose(json);

	m_animationStateData =
		spAnimationStateData_create(m_skeletonData);
	m_animationState =
		spAnimationState_create(m_animationStateData);
	m_animationState->listener = animationListener;
	spineTasks.insert(
		std::pair<spAnimationState*, CKLBUISpine*>(m_animationState, this));

	m_skeleton = spSkeleton_create(m_skeletonData);
	m_skeleton->scaleY = -m_skeleton->scaleY;
	m_pRootNode->setRenderSlotCount(m_skeleton->slotsCount);
	m_slotSprites = new std::vector<CKLBDynSprite*>(
		m_skeleton->slotsCount, static_cast<CKLBDynSprite*>(NULL));

	m_track = spAnimationState_setAnimation(
		m_animationState,
		0,
		m_skeletonData->animations[0],
		1);
	spAnimationState_apply(m_animationState, m_skeleton);
	updateAttachments();
	return true;
}

void
CKLBUISpine::animationListener(
	spAnimationState* state,
	spEventType type,
	spTrackEntry* entry,
	spEvent* event)
{
	std::map<spAnimationState*, CKLBUISpine*>::iterator owner =
		spineTasks.find(state);
	if (owner == spineTasks.end()) {
		return;
	}

	if (type == SP_ANIMATION_COMPLETE) {
		owner->second->callEventCallback(
			"SP_ANIMATION_COMPLETE", entry->animation->name);
	} else if (type == SP_ANIMATION_EVENT) {
		owner->second->callEventCallback(event);
	}
}

void
CKLBUISpine::callEventCallback(
	const char* eventName, const char* stringValue)
{
	std::map<std::string, std::string>::const_iterator callback =
		m_eventCallbacks.find(eventName);
	if (callback == m_eventCallbacks.end()) {
		return;
	}
	CKLBScriptEnv::getInstance().call_eventSpineAnim(
		callback->second.c_str(),
		0,
		stringValue,
		0.0f);
}

void
CKLBUISpine::callEventCallback(spEvent* event)
{
	const char* eventName = event->data->name;
	std::map<std::string, std::string>::const_iterator callback =
		m_eventCallbacks.find(eventName);
	if (callback == m_eventCallbacks.end()) {
		return;
	}
	const char* stringValue = "null";
	if (event->stringValue) {
		stringValue = event->stringValue;
	}
	CKLBScriptEnv::getInstance().call_eventSpineAnim(
		callback->second.c_str(),
		event->intValue,
		stringValue,
		event->floatValue);
}

void
CKLBUISpine::updateAttachments()
{
	CKLBNode* node = m_pRootNode;
	CKLBRenderingManager& renderingManager =
		CKLBRenderingManager::getInstance();
	int slotCount = m_skeleton->slotsCount;
	for (int slotIndex = 0;
		slotIndex < slotCount;
		++slotIndex) {
		CKLBDynSprite* sprite = (*m_slotSprites)[slotIndex];
		spSlot* slot = m_skeleton->drawOrder[slotIndex];
		spAttachment* attachment = slot->attachment;

		if (!attachment) {
			if (sprite) {
				node->setRender(NULL, slotIndex);
				renderingManager.releaseCommand(sprite);
				(*m_slotSprites)[slotIndex] = NULL;
			}
			continue;
		}

		if (attachment->type == SP_ATTACHMENT_CLIPPING) {
			continue;
		}
		if (attachment->type == SP_ATTACHMENT_REGION) {
			if (!sprite) {
				sprite =
					renderingManager.allocateCommandDynSprite(4, 6, 0);
				buildRegionSprite(
					slot, attachment, sprite, slotIndex);
				(*m_slotSprites)[slotIndex] = sprite;
				m_pRootNode->setRender(sprite, slotIndex);
			} else {
				updateRegionSprite(slot, attachment, sprite);
			}
		} else if (attachment->type == SP_ATTACHMENT_MESH) {
			spMeshAttachment* mesh =
				reinterpret_cast<spMeshAttachment*>(attachment);
			u16 vertexCount = static_cast<u16>(
				mesh->super.worldVerticesLength / 2);
			u16 indexCount =
				static_cast<u16>(mesh->trianglesCount);
			if (!sprite) {
				sprite = renderingManager.allocateCommandDynSprite(
					vertexCount, indexCount, 0);
				buildRegionMesh(
					slot, attachment, sprite, slotIndex);
				(*m_slotSprites)[slotIndex] = sprite;
				m_pRootNode->setRender(sprite, slotIndex);
			} else {
				updateRegionMesh(slot, attachment, sprite);
			}
		} else {
			klb_assertAlways("NOT supported");
		}
	}
	m_pRootNode->markUpMatrix();
}

void
CKLBUISpine::updateRegionSprite(
	spSlot* slot, void* attachment, void* renderCommand)
{
	spRegionAttachment* region =
		static_cast<spRegionAttachment*>(attachment);
	CKLBDynSprite* sprite =
		static_cast<CKLBDynSprite*>(renderCommand);

	spRegionAttachment_computeWorldVertices(
		region,
		slot->bone,
		sprite->getSrcXYBuffer(),
		0,
		2);
	for (u32 vertex = 0; vertex < 4; ++vertex) {
		sprite->setVertexUV(
			vertex,
			region->uvs[vertex * 2],
			region->uvs[vertex * 2 + 1]);
	}

	CKLBNode* node = m_pRootNode;
	u32 color = packSpineColor(m_skeleton, slot);
	for (u32 vertex = 0; vertex < 4; ++vertex) {
		sprite->setVertexColor(node, vertex, color);
	}
	sprite->setTexture(getAttachmentImage(region->rendererObject));
}

void
CKLBUISpine::updateRegionMesh(
	spSlot* slot, void* attachment, void* renderCommand)
{
	spMeshAttachment* mesh =
		static_cast<spMeshAttachment*>(attachment);
	CKLBDynSprite* sprite =
		static_cast<CKLBDynSprite*>(renderCommand);
	spVertexAttachment_computeWorldVertices(
		&mesh->super,
		slot,
		0,
		mesh->super.worldVerticesLength,
		sprite->getSrcXYBuffer(),
		0,
		2);

	int indexCount = mesh->trianglesCount;
	for (int index = 0; index < indexCount; ++index) {
		u16 vertex = mesh->triangles[index];
		sprite->setVertexUV(
			vertex,
			mesh->uvs[vertex * 2],
			mesh->uvs[vertex * 2 + 1]);
	}

	if (!(mesh->super.worldVerticesLength < 2)) {
		CKLBNode* node = m_pRootNode;
		u32 color = packSpineColor(m_skeleton, slot);
		for (int vertex = 0;
			vertex < mesh->super.worldVerticesLength / 2;
			++vertex) {
			sprite->setVertexColor(node, vertex, color);
		}
	}
	sprite->setTexture(getAttachmentImage(mesh->rendererObject));
}

void
CKLBUISpine::buildRegionMesh(
	spSlot* slot, void* attachment, void* renderCommand, int slotIndex)
{
	spMeshAttachment* mesh =
		static_cast<spMeshAttachment*>(attachment);
	CKLBDynSprite* sprite =
		static_cast<CKLBDynSprite*>(renderCommand);
	int indexCount = mesh->trianglesCount;
	u16* destination = sprite->getSrcIndexBuffer();
	for (int index = 0; index < indexCount; ++index, ++destination) {
		*destination = mesh->triangles[index];
	}
	updateRegionMesh(slot, attachment, renderCommand);
	sprite->changeOrder(
		CKLBRenderingManager::getInstance(),
		m_order + slotIndex);
}

void
CKLBUISpine::execute(u32 deltaT)
{
	if (CHANGE_A) {
		RESET_A;
	}
	if (m_playing) {
		return;
	}
	spAnimationState_update(
		m_animationState,
		static_cast<float>(deltaT) / 1000.0f);
	spAnimationState_apply(m_animationState, m_skeleton);
	spSkeleton_updateWorldTransform(m_skeleton);
	updateAttachments();
}

void
CKLBUISpine::dieUI()
{
	spineTasks.erase(m_animationState);

	spSkeleton_dispose(m_skeleton);
	spAnimationState_dispose(m_animationState);
	spAnimationStateData_dispose(m_animationStateData);
	spSkeletonData_dispose(m_skeletonData);
	spAtlas_dispose(m_atlas);

	CKLBNode* node = m_pRootNode;
	CKLBRenderingManager& renderingManager =
		CKLBRenderingManager::getInstance();
	int slotCount = m_slotSprites->size();
	for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex) {
		node->setRender(NULL, slotIndex);
		CKLBDynSprite* sprite = m_slotSprites->at(slotIndex);
		if (sprite) {
			renderingManager.releaseCommand(sprite);
		}
	}
	m_slotSprites->clear();
	delete m_slotSprites;
}

int
CKLBUISpine::commandUI(CLuaState& lua, int, int command)
{
	klb_assertNull(command >= START_ANIM && command <= SET_EVENT_CB,
				   "SPINE : wrong cmd idx");
	(this->*m_commands[command].handler)(lua);
	return 0;
}
