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
#ifndef CKLBUIShader_h
#define CKLBUIShader_h

#include "CKLBLuaTask.h"

class CTexture;
class CTextureUsage;
class CKLBRenderState;

/*
 * UI_Shader is implemented by a Lua task in the shipped engine. It owns the
 * shader render commands, texture-animation resources, and asset handles used
 * by its animated parameters.
 */
class CKLBShaderTask : public CKLBLuaTask
{
	friend class CKLBTaskFactory<CKLBShaderTask>;

private:
	CKLBShaderTask();
	virtual ~CKLBShaderTask();

public:
	static CKLBShaderTask* create(CKLBTask* parent, const char* assetName,
		u32 order);

	virtual u32 getClassID();
	void execute(u32 deltaT);
	void die();
	bool initScript(CLuaState& lua);
	int commandScript(CLuaState& lua);

private:
	enum AnimationLoop {
		ANIM_ONCE,
		ANIM_REPEAT,
		ANIM_PING_PONG
	};

	enum AnimationCurve {
		ANIM_STEP,
		ANIM_LINEAR,
		ANIM_COSINE
	};

	struct ShaderAnimation {
		ShaderAnimation*	next;
		ShaderAnimation*	nextChannel;
		const char*			parameterName;
		bool				pixelShader;
		bool				active;
		u32					curve;
		u32					loop;
		float				elapsed;
		float				duration;
		float				value;
		float				from;
		float				to;
		float				range;
	};

	struct ShaderTextureFrame {
		ShaderTextureFrame*	next;
		s32					frame;
		const u8*			pixels;
	};

	struct TextureDecodeTarget {
		CKLBShaderTask*		owner;
		u32					(*allocator)(TextureDecodeTarget*);
		u8*					pixels;
		u32					byteSize;
		// 以下は復号器が画像ヘッダから書き込む出力。
		u32					width;
		u32					height;
		u32					bytesPerPixel;

		TextureDecodeTarget()
		: owner(NULL), allocator(NULL), pixels(NULL), byteSize(0) {}

		static u32 allocate(TextureDecodeTarget* target);
	};

	bool init(CKLBTask* parent, const char* assetName, u32 order);
	bool setPixelShaderValue(const char* parameterName, s32 componentCount,
		const float* values);
	bool setVertexShaderValue(const char* parameterName, s32 componentCount,
		const float* values);
	bool setShaderTexture(const char* parameterName, const char* assetName,
		u32 wrapping, u32 sampling);
	void setShaderAnimation(const char* parameterName, u32 loop, u32 curve,
		float from, float to, s32 duration, bool pixelShader);
	// Declares the shader parameter that receives the animated texture and the
	// sampler state its frames are bound with. The frames themselves arrive one
	// by one through addTextureAnimationFrame().
	void setTextureAnimation(const char* textureName, u32 wrapU, u32 wrapV,
		u32 sampling);
	// Decodes one texture asset and appends it to the animation frame list.
	// The first frame creates the texture and binds it to the shader.
	bool addTextureAnimationFrame(s32 frame, const char* assetName);
	void releaseShaderInstances();
	void releaseTextureAnimation();
	void releaseResources();
	static float advanceAnimation(ShaderAnimation* animation, float deltaT);

	const char*			m_shaderName;
	u32					m_order;
	u32					m_textureHandles[10];
	bool				m_textureStateChanged;
	float				m_parameterScratch[6];
	u32					m_shaderDefinition;
	s32					m_textureFrame;
	u32					m_textureByteSize;
	u32					m_textureFrameCapacity;
	u8*					m_texturePixels;
	void*				m_shaderInstance;
	CKLBRenderState*		m_shaderCommand;
	ShaderAnimation*	m_animations;
	ShaderTextureFrame*	m_textureAnimationFrames;
	CTexture*			m_texture;
	CTextureUsage*		m_textureUsage;
	const char*			m_textureAnimationName;
	u32					m_textureAnimationSampling;
	u32					m_textureAnimationWrapU;
	u32					m_textureAnimationWrapV;
	u8					m_texturePixelSize;
};

#endif // CKLBUIShader_h
