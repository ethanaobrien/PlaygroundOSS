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
#ifndef CKLBUIPolygon_h
#define CKLBUIPolygon_h

#include "CKLBUITask.h"

class CKLBDynSprite;
class CKLBImageAsset;
class CKLBPolygonBuilder;

class CKLBUIPolygon : public CKLBUITask
{
	friend class CKLBTaskFactory<CKLBUIPolygon>;

protected:
	CKLBUIPolygon();
	virtual ~CKLBUIPolygon();

public:
	enum {
		CLASS_ID = 0x00080102
	};

	static CKLBUIPolygon* create(CKLBUITask* parent, CKLBNode* node, u32 order);
	virtual u32 getClassID() { return CLASS_ID; }

protected:
	virtual void execute(u32 /* deltaT */) {}
	virtual void dieUI() { releaseResources(true); }

private:
	static PROP_V2 ms_propItems[];

	enum Command {
		NEW_PATH,
		NEW_HOLE,
		ADD_POINT,
		END_HOLE,
		PUSH_PATH,
		BUILD,
		SET_TEXTURE
	};

	bool init(CKLBUITask* parent, CKLBNode* node, u32 order);
	bool initCore(u32 order);
	void createBuilder();
	void resetBuilder();
	u32 appendVertex(float x, float y, u32 color);
	bool bindTexture(const char* assetName, float scale);
	void updateTextureCoordinates();
	bool initUI(CLuaState& lua) {
		int argc = lua.numArgs();
		if(argc != 2) {
			return false;
		}

		u32 order = lua.getInt(2);
		return initCore(order);
	}
	int commandUI(CLuaState& lua, int argc, int cmd);
	void releaseResources(bool releaseTexture);

	struct GeometryCounts {
		s32 vertexCount;
		u32 indexCount;
	};

	struct TextureMapping {
		float scale[4];
		float origin[2];
		float bounds[4];
		float requestedScale;
	};

	u32					m_order;
	GeometryCounts		m_geometry;
	CKLBImageAsset*		m_textureAsset;
	u32					m_textureHandle;
	TextureMapping		m_textureMapping;
	CKLBDynSprite*		m_render;
	CKLBPolygonBuilder*	m_builder;
};

#endif // CKLBUIPolygon_h
