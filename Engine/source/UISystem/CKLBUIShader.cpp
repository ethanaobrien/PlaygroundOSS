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
#include "CKLBUIShader.h"
#include "CKLBRendering.h"
#include "CKLBDataHandler.h"
#include "CKLBUtility.h"
#include <algorithm>
#include <math.h>

enum {
	SHADER_PSHADER_ANIM = 0,
	SHADER_VSHADER_ANIM,
	SHADER_PSHADER_VALUE,
	SHADER_VSHADER_VALUE,
	SHADER_PSHADER_TEXTURE,
	SHADER_SET_TEXTURE_ANIM,
	SHADER_ADD_TEXTURE_ANIM
};

static IFactory::DEFCMD cmd[] = {
	{ "PSHADER_ANIM",             SHADER_PSHADER_ANIM },
	{ "VSHADER_ANIM",             SHADER_VSHADER_ANIM },
	{ "PSHADER_VALUE",            SHADER_PSHADER_VALUE },
	{ "VSHADER_VALUE",            SHADER_VSHADER_VALUE },
	{ "PSHADER_TEXTURE",          SHADER_PSHADER_TEXTURE },
	{ "SET_SHADER_TEXTURE_ANIM",  SHADER_SET_TEXTURE_ANIM },
	{ "ADD_SHADER_TEXTURE_ANIM",  SHADER_ADD_TEXTURE_ANIM },
	{ "ONCE_FORWARD",             0 },
	{ "REPEAT_FORWARD",           1 },
	{ "PING_PONG",                2 },
	{ "FLAT",                     0 },
	{ "LINEAR",                   1 },
	{ "COSINE",                   2 },
	{ "SAMPLING_NEAREST",         0 },
	{ "SAMPLING_LINEAR",          1 },
	{ "UV_CLIP",                  1 },
	{ "UV_REPEAT",                0 },
	{ 0, 0 }
};

static CKLBTaskFactory<CKLBShaderTask> factory("UI_Shader", 0x00080054, cmd);

CKLBShaderTask::CKLBShaderTask()
{
	m_textureStateChanged = false;
	m_shaderDefinition = 0xffff;
	m_textureFrame = -1;
	m_texturePixels = NULL;
	m_shaderInstance = NULL;
	m_shaderCommand = NULL;
	m_animations = NULL;
	m_textureAnimationFrames = NULL;
	m_texture = NULL;
	m_textureUsage = NULL;
	m_textureAnimationName = NULL;
	std::fill(m_textureHandles, m_textureHandles + 10, 0xffffU);
}

u32
CKLBShaderTask::TextureDecodeTarget::allocate(TextureDecodeTarget* target)
{
	u32 byteSize =
		(target->width * target->height * target->bytesPerPixel + 3U) & ~3U;
	target->pixels = new u8[byteSize];
	target->byteSize = byteSize;
	return true;
}

CKLBShaderTask*
CKLBShaderTask::create(CKLBTask* parent, const char* assetName, u32 order)
{
	CKLBShaderTask* task = KLBNEW(CKLBShaderTask);
	if (!task) {
		return NULL;
	}
	if (!task->init(parent, assetName, order)) {
		KLBDELETE(task);
		return NULL;
	}
	return task;
}

bool
CKLBShaderTask::init(CKLBTask* parent, const char* assetName, u32 order)
{
	CKLBRenderingManager& rendering = CKLBRenderingManager::getInstance();
	m_order = order;

	if (assetName && assetName[0]) {
		// 名前付きシェーダを実体化し、そのインスタンスを保持する。
		u32 shaderDefinition = rendering.getShaderDefinition(assetName);
		m_shaderDefinition = shaderDefinition;
		if (shaderDefinition == 0xffff) {
			releaseResources();
			return false;
		}
		m_shaderInstance = rendering.instanceShader(shaderDefinition, order);
		if (!m_shaderInstance) {
			releaseResources();
			return false;
		}
	} else {
		// アセット名が無い場合は、シェーダを解除するコマンドとして振る舞う。
		m_shaderCommand = rendering.allocateCommandState();
		m_shaderCommand->setUse(false, false, NULL);
		m_shaderCommand->m_commandType = RENDERCOMMAND_UNSETSHADER;
		rendering.addToRendering(m_shaderCommand, order);
	}
	return regist(parent, P_UIAFTER);
}

CKLBShaderTask::~CKLBShaderTask()
{
	releaseResources();
}

u32
CKLBShaderTask::getClassID()
{
	return 0x00080054;
}

void
CKLBShaderTask::die()
{
	releaseResources();
}

void
CKLBShaderTask::releaseShaderInstances()
{
	CKLBRenderingManager& rendering = CKLBRenderingManager::getInstance();
	if (m_shaderInstance) {
		rendering.removeShader(m_shaderInstance);
		m_shaderInstance = NULL;
	}
	if (m_shaderCommand) {
		rendering.releaseCommand(m_shaderCommand);
		m_shaderCommand = NULL;
	}
}

void
CKLBShaderTask::releaseResources()
{
	ShaderAnimation* animation = m_animations;
	while (animation) {
		ShaderAnimation* next = animation->next;
		delete [] animation->parameterName;
		delete animation->nextChannel;
		delete animation;
		animation = next;
	}
	m_animations = NULL;

	releaseTextureAnimation();

	for (u32 index = 0; index < 10; ++index) {
		u32 handle = m_textureHandles[index];
		if (handle != 0xffff) {
			CKLBDataHandler::releaseHandle(static_cast<u16>(handle));
			m_textureHandles[index] = 0xffff;
		}
	}

	releaseShaderInstances();
}

void
CKLBShaderTask::releaseTextureAnimation()
{
	CKLBOGLWrapper& graphics = CKLBOGLWrapper::getInstance();
	if (m_textureUsage) {
		m_texture->releaseUsage(m_textureUsage);
		m_textureUsage = NULL;
	}
	if (m_texture) {
		graphics.releaseTexture(m_texture);
		m_texture = NULL;
	}

	if (m_texturePixels) {
		delete [] m_texturePixels;
		m_texturePixels = NULL;
	}

	ShaderTextureFrame* frame = m_textureAnimationFrames;
	while (frame) {
		ShaderTextureFrame* next = frame->next;
		delete [] frame->pixels;
		delete frame;
		frame = next;
	}
	m_textureAnimationFrames = NULL;

	delete [] m_textureAnimationName;
	m_textureAnimationName = NULL;
}

bool
CKLBShaderTask::initScript(CLuaState& lua)
{
	if (lua.numArgs() != 3) {
		return false;
	}

	CKLBTask* parent = lua.isNil(1) ? NULL
		: static_cast<CKLBTask*>(const_cast<void*>(lua.getScriptPtr(1)));
	const char* assetName = lua.isNil(2) ? NULL : lua.getString(2);
	if (assetName && !assetName[0]) {
		assetName = NULL;
	}
	u32 order = lua.getInt(3);
	return init(parent, assetName, order);
}

int
CKLBShaderTask::commandScript(CLuaState& lua)
{
	int argc = lua.numArgs();
	if (argc < 2) {
		lua.retBool(false);
		return 1;
	}
	int cmd = lua.getInt(2);
	switch (cmd)
	{
	default:
		break;
	case SHADER_PSHADER_ANIM:
	case SHADER_VSHADER_ANIM:
		{
			if (argc < 8) {
				break;
			}
			const char* parameterName = lua.getString(3);
			u32 loop = lua.getInt(4);
			u32 curve = lua.getInt(5);
			float from = lua.getFloat(6);
			float to = lua.getFloat(7);
			s32 duration = lua.getInt(8);
			setShaderAnimation(parameterName, loop, curve, from, to, duration,
				cmd == SHADER_PSHADER_ANIM);
			lua.retBool(true);
			return 1;
		}
	case SHADER_PSHADER_VALUE:
	case SHADER_VSHADER_VALUE:
		{
			if (argc < 4) {
				break;
			}
			// 4番目以降の引数がそのままシェーダ定数の成分になる。
			// 渡されなかった成分は 0 のままにする。values[0] は必ず
			// 下のループが書き込むため、ここでは初期化しない。
			float values[12];
			values[1] = 0.0f;
			values[2] = 0.0f;
			values[3] = 0.0f;
			s32 componentCount = argc - 3;
			for (s32 index = 0; index < componentCount; ++index) {
				values[index] = lua.getFloat(index + 4);
			}
			const char* parameterName = lua.getString(3);
			bool result = (cmd == SHADER_PSHADER_VALUE)
				? setPixelShaderValue(parameterName, componentCount, values)
				: setVertexShaderValue(parameterName, componentCount, values);
			lua.retBool(result);
			return 1;
		}
	case SHADER_PSHADER_TEXTURE:
		{
			if (argc < 4) {
				break;
			}
			const char* parameterName = lua.getString(3);
			const char* assetName = lua.getString(4);
			u32 wrapping = lua.getInt(5);
			u32 sampling = lua.getInt(6);
			lua.retBool(setShaderTexture(
				parameterName, assetName, wrapping, sampling));
			return 1;
		}
	case SHADER_SET_TEXTURE_ANIM:
		{
			if (argc < 7) {
				break;
			}
			const char* textureName = lua.getString(3);
			u32 wrapU = lua.getInt(4);
			u32 wrapV = lua.getInt(5);
			u32 sampling = lua.getInt(6);
			// 7番目の引数は型の検証のみ行い、現在は使用していない。
			lua.getBool(7);
			setTextureAnimation(textureName, wrapU, wrapV, sampling);
			lua.retBool(true);
			return 1;
		}
	case SHADER_ADD_TEXTURE_ANIM:
		{
			if (argc < 4) {
				break;
			}
			s32 frame = lua.getInt(4);
			const char* assetName = lua.getString(3);
			lua.retBool(addTextureAnimationFrame(frame, assetName));
			return 1;
		}
	}
	lua.retBool(false);
	return 1;
}

bool
CKLBShaderTask::setPixelShaderValue(const char* parameterName,
	s32 componentCount, const float* values)
{
	CKLBRenderingManager& rendering = CKLBRenderingManager::getInstance();
	if (--componentCount <= 3U) {
		rendering.setShaderParamF(
			m_shaderInstance,
			parameterName,
			const_cast<float*>(values));
		return true;
	}
	return false;
}

bool
CKLBShaderTask::setVertexShaderValue(const char* parameterName,
	s32 componentCount, const float* values)
{
	CKLBRenderingManager& rendering = CKLBRenderingManager::getInstance();
	if (--componentCount <= 3U) {
		rendering.setVertexShaderParamF(
			m_shaderInstance,
			parameterName,
			const_cast<float*>(values));
		return true;
	}
	return false;
}

bool
CKLBShaderTask::setShaderTexture(const char* parameterName,
	const char* assetName, u32 wrapping, u32 sampling)
{
	CKLBRenderingManager& rendering = CKLBRenderingManager::getInstance();
	u32 parameter = rendering.getShaderParamID(
		m_shaderInstance, parameterName);
	klb_assertNull(parameter < 10, "Texture UniformID overflow");

	u32& handle = m_textureHandles[parameter];
	if (handle != 0xffff) {
		CKLBDataHandler::releaseHandle(static_cast<u16>(handle));
		handle = 0xffff;
	}

	bool result = false;
	CKLBAsset* asset = CKLBUtility::loadAsset(
		assetName, &handle, NULL, true);
	if (asset->getAssetType() == ASSET_TEXTURE) {
		CKLBTextureAsset* texture =
			reinterpret_cast<CKLBTextureAsset*>(asset);
		CTextureUsage* usage = texture->m_pTextureUsage;
		usage->setSampling(
			static_cast<CTextureUsage::SAMPLING>(sampling),
			static_cast<CTextureUsage::SAMPLING>(sampling));
		usage->setWrapping(
			static_cast<CTextureUsage::WRAPPING>(wrapping),
			static_cast<CTextureUsage::WRAPPING>(wrapping));
		rendering.setShaderParamTexture(m_shaderInstance, parameter, usage);
		result = true;
	}
	return result;
}

void
CKLBShaderTask::setTextureAnimation(const char* textureName, u32 wrapU,
	u32 wrapV, u32 sampling)
{
	releaseTextureAnimation();
	m_textureAnimationName = CKLBUtility::copyString(textureName);
	m_textureAnimationWrapU = wrapU;
	m_textureAnimationWrapV = wrapV;
	m_textureAnimationSampling = sampling;
	m_textureFrame = 0;
}

bool
CKLBShaderTask::addTextureAnimationFrame(s32 frame, const char* assetName)
{
	IPlatformRequest& platform = CPFInterface::getInstance().platform();
	IReadStream* stream = platform.openReadStream(
		assetName, platform.useEncryption(), (u32)-1);
	if (!stream) {
		return false;
	}
	if (stream->getStatus() != IReadStream::NORMAL) {
		delete stream;
		return false;
	}

	s32 sourceSize = stream->getSize();
	u8* source = new u8[stream->getSize()];
	stream->readBlock(source, sourceSize);

	bool result = false;
	u32 channelCount;

	// The frame size is only known once the image header has been read, so the
	// decoder calls back into allocate() to obtain the pixel buffer.
	TextureDecodeTarget target;
	target.owner = this;
	target.allocator = &TextureDecodeTarget::allocate;

	if (decodeTextureImage(source, sourceSize, 0, 0, NULL, 0, &channelCount,
			reinterpret_cast< ::TextureDecodeTarget*>(&target))) {
		ShaderTextureFrame* newFrame = new ShaderTextureFrame();
		ShaderTextureFrame* last = m_textureAnimationFrames;
		newFrame->next = NULL;
		newFrame->pixels = NULL;
		newFrame->frame = frame;

		u32 byteSize =
			(target.width * target.height * target.bytesPerPixel + 3U) & ~3U;
		if (last) {
			klb_assertNull(byteSize == m_textureByteSize,
				"Texture animation for shader is using texture of different size or format");
			while (last->next) {
				last = last->next;
			}
			last->next = newFrame;
		} else {
			CKLBOGLWrapper& graphics = CKLBOGLWrapper::getInstance();
			s32 dataLength =
				target.width * target.height * target.bytesPerPixel;
			CTexture* texture = graphics.createTexture(
				target.width, target.height, GL_UNSIGNED_BYTE,
				static_cast<CKLBOGLWrapper::TEX_CHANNEL>(target.bytesPerPixel),
				target.pixels,
				dataLength,
				CKLBOGLWrapper::TEX_NONE, 0, NULL);
			m_textureByteSize = byteSize;
			m_texturePixels = new u8[byteSize];
			m_texturePixelSize = static_cast<u8>(target.bytesPerPixel);
			if (texture) {
				CTextureUsage* usage = texture->createUsage();
				usage->setSampling(
					static_cast<CTextureUsage::SAMPLING>(m_textureAnimationSampling),
					static_cast<CTextureUsage::SAMPLING>(m_textureAnimationSampling));
				usage->setWrapping(
					static_cast<CTextureUsage::WRAPPING>(m_textureAnimationWrapU),
					static_cast<CTextureUsage::WRAPPING>(m_textureAnimationWrapV));
				CKLBRenderingManager& rendering =
					CKLBRenderingManager::getInstance();
				rendering.setShaderParamTexture(
					m_shaderInstance, m_textureAnimationName, usage);
				m_texture = texture;
				m_textureUsage = usage;
			}
			m_textureAnimationFrames = newFrame;
		}
		newFrame->pixels = target.pixels;
		result = true;
	}
	delete [] source;
	delete stream;
	return result;
}

void
CKLBShaderTask::execute(u32 deltaT)
{
	CKLBRenderingManager& rendering = CKLBRenderingManager::getInstance();
	ShaderAnimation* animation = m_animations;
	float elapsed = static_cast<float>(deltaT);
	while (animation) {
		float values[16];
		float* value = values;
		for (ShaderAnimation* channel = animation;
			 channel;
			 channel = channel->nextChannel) {
			*value++ = advanceAnimation(channel, elapsed);
		}
		if (animation->pixelShader) {
			rendering.setShaderParamF(
				m_shaderInstance, animation->parameterName, values);
		} else {
			rendering.setVertexShaderParamF(
				m_shaderInstance, animation->parameterName, values);
		}
		animation = animation->next;
	}

	if (m_textureFrame < 0) {
		return;
	}

	ShaderTextureFrame* current = NULL;
	for (ShaderTextureFrame* frame = m_textureAnimationFrames;
		 frame;
		 frame = frame->next) {
		if (frame->frame > m_textureFrame) {
			break;
		}
		current = frame;
	}
	if (!current) {
		return;
	}

	u32 width = m_texture->getWidth();
	u32 height = m_texture->getHeight();
	ShaderTextureFrame* next = current->next;
	const u8* fromPixels = current->pixels;
	if (next) {
		u32 byteSize = width * height * m_texturePixelSize;
		const u8* toPixels = next->pixels;
		u32 span = static_cast<u32>(next->frame - current->frame);
		u32 blend = 0;
		if (span) {
			blend = static_cast<u32>(
				(m_textureFrame - current->frame) << 8) / span;
		}
		u32 inverseBlend = 256U - blend;
		for (u32 offset = 0; offset < m_textureByteSize; offset += 4) {
			m_texturePixels[offset] = static_cast<u8>(
				(inverseBlend * fromPixels[offset]
				 + blend * toPixels[offset]) >> 8);
			m_texturePixels[offset + 1] = static_cast<u8>(
				(inverseBlend * fromPixels[offset + 1]
				 + blend * toPixels[offset + 1]) >> 8);
			m_texturePixels[offset + 2] = static_cast<u8>(
				(inverseBlend * fromPixels[offset + 2]
				 + blend * toPixels[offset + 2]) >> 8);
			m_texturePixels[offset + 3] = static_cast<u8>(
				(inverseBlend * fromPixels[offset + 3]
				 + blend * toPixels[offset + 3]) >> 8);
		}
		m_texture->updateTexture(
			0, 0, m_texture->getWidth(), m_texture->getHeight(),
			m_texturePixels, byteSize);
		m_textureFrame += deltaT;
	} else {
		m_texture->updateTexture(
			0, 0, width, height,
			const_cast<u8*>(fromPixels), m_textureByteSize);
		m_textureFrame = -1;
	}
}

void
CKLBShaderTask::setShaderAnimation(const char* parameterName, u32 loop,
	u32 curve, float from, float to, s32 duration, bool pixelShader)
{
	if (!parameterName) {
		return;
	}

	ShaderAnimation* animation = duration ? new ShaderAnimation() : NULL;
	ShaderAnimation* previous = NULL;
	ShaderAnimation* current = m_animations;
	while (current) {
		if (current->parameterName
		 && !strcmp(current->parameterName, parameterName)
		 && current->pixelShader == pixelShader) {
			break;
		}
		previous = current;
		current = current->next;
	}

	if (!current) {
		animation->pixelShader = pixelShader;
		animation->next = NULL;
		const char* copiedName = CKLBUtility::copyString(parameterName);
		ShaderAnimation* next = m_animations;
		animation->parameterName = copiedName;
		animation->loop = loop;
		animation->curve = curve;
		animation->from = from;
		animation->to = to;
		animation->active = true;
		animation->range = to - from;
		animation->duration =
			duration < 0 ? 1.0f : static_cast<float>(duration);
		animation->elapsed = 0.0f;
		animation->next = next;
		m_animations = animation;
		return;
	}

	if (duration) {
		animation->pixelShader = pixelShader;
		animation->next = NULL;
		animation->parameterName = NULL;
		animation->loop = loop;
		animation->curve = curve;
		animation->from = from;
		animation->to = to;
		animation->active = true;
		animation->range = to - from;
		animation->duration =
			duration < 0 ? 1.0f : static_cast<float>(duration);
		animation->elapsed = 0.0f;
		ShaderAnimation* channel = current;
		while (channel->nextChannel) {
			channel = channel->nextChannel;
		}
		channel->nextChannel = animation;
		return;
	}

	if (previous) {
		previous->next = current->next;
	} else {
		m_animations = current->next;
	}
	delete [] current->parameterName;
	delete current->nextChannel;
	delete current;
}

float
CKLBShaderTask::advanceAnimation(ShaderAnimation* animation, float deltaT)
{
	if (!animation->active) {
		return animation->value;
	}

	animation->elapsed += deltaT;
	float progress = 0.0f;
	u32 loop = animation->loop;
	if (loop != ANIM_PING_PONG) {
		if (loop != ANIM_REPEAT) {
			if (loop == ANIM_ONCE) {
				if (animation->elapsed >= animation->duration) {
					animation->active = false;
					progress = 1.0f;
				} else {
					progress = animation->elapsed / animation->duration;
				}
			}
		} else {
			progress = (s32)animation->elapsed % (s32)animation->duration;
			progress /= animation->duration;
		}
	} else {
		progress = (s32)animation->elapsed % (s32)animation->duration;
		progress /= animation->duration * 0.5f;
		if (progress > 1.0f) {
			progress = 1.0f - (progress - 1.0f);
		}
	}

	u32 curve = animation->curve;
	if (curve != ANIM_COSINE) {
		if (curve != ANIM_LINEAR) {
			if (curve == ANIM_STEP) {
				if (0.5f > progress) {
					animation->value = animation->from;
				} else {
					animation->value = animation->to;
				}
			}
		} else {
			animation->value =
				progress * animation->range + animation->from;
		}
	} else {
		float base = animation->from;
		animation->value =
			((float)cos((double)((progress - 1.0f) * 3.14f)) + 1.0f)
			* animation->range * 0.5f + base;
	}
	return animation->value;
}

// The shipped build compiled the render-buffer task as part of this unit: one
// static initialiser at 0x71ac0 constructs BOTH factories in a single body,
// and that symbol is emitted once per translation unit.  Shader first, which
// is the order that body registers them in.
#include "CKLBRenderBufferTask.cpp"
