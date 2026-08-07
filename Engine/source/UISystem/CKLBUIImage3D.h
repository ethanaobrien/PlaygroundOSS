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
#ifndef CKLBUIImage3D_h
#define CKLBUIImage3D_h

#include "CKLBUITask.h"

class CKLBDynSprite;
class CKLBImageAsset;
class CKLBAsset;

class CKLBUIImage3D : public CKLBUITask
{
	friend class CKLBTaskFactory<CKLBUIImage3D>;

private:
	CKLBUIImage3D();
	virtual ~CKLBUIImage3D();

public:
	enum {
		CLASS_ID = 0x0008010f
	};

	static CKLBUIImage3D* create(CKLBUITask* parent, CKLBNode* node, u32 order, float x, float y);

	u32 getClassID();
	void execute(u32 deltaT);
	virtual u32 getOrder();
	virtual void setOrder(u32 order);
	void setPosition(float x, float y);
	void setRotation3D(float x, float y, float z);
	void setDistance(float distance);
	void notifyAssetUpdate(const char* sourceName, CKLBAsset* replacement);

protected:
	bool initUI(CLuaState& lua);
	int commandUI(CLuaState& lua, int argc, int cmd);
	void dieUI();

private:
	struct SVertex {
		float x;
		float y;
		float z;
		float w;
		float u;
		float v;
	};

	bool setup(s32 order, float x, float y);
	bool init(CKLBUITask* parent, CKLBNode* node, s32 order, float x, float y);
	bool allocMesh(float x, float y, const char* assetName, int vertexCount, int indexCount);
	bool createMesh(const char* assetName, float x, float y,
		int divisionWidth, int divisionHeight, int offsetX, int offsetY,
		int countX, int countY);
	bool buildMesh(int divisionWidth, u32 divisionHeight, int offsetX,
		int offsetY, int countX, int countY);
	void updateColor(CKLBDynSprite* sprite);
	bool releaseMesh();
	void executeProjection();
	static void projectPoint(float* projectedX, float* projectedY,
		const float* matrix, float x, float y, float distance);
	static void matMul(const float* left, const float* right, float* result);
	static void makeMatrix(const float* rotation, float* matrix);

	int		m_scaleMode;
	int		m_centerModeX;
	int		m_centerModeY;
	int		m_fixedWidth;
	int		m_fixedHeight;
	float	m_centerX;
	float	m_centerY;
	float	m_scaleX;
	float	m_scaleY;
	float	m_rotation[3];
	float	m_fieldOfView;
	float	m_projectionMatrix[16];
	float	m_workMatrix[16];
	u32		m_colors[5];
	float	m_textureU;
	float	m_textureV;
	u32		m_vertexCount;
	SVertex*	m_vertexBuffer;
	CKLBDynSprite* m_dynSprite;
	CKLBImageAsset* m_asset;
	u32		m_textureHandle;
	bool	m_assetChanged;
	int		m_divisionOffsetX;
	int		m_divisionOffsetY;
	int		m_divisionCountX;
	int		m_divisionCountY;
	int		m_divisionWidth;
	u32		m_divisionHeight;
	CKLBNode* m_renderNode;
	u32		m_order;

	static PROP_V2 ms_propItems[];
};

#endif // CKLBUIImage3D_h
