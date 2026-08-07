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
#include "CKLBUIPolygon.h"
#include "CKLBDataHandler.h"
#include "CKLBRendering.h"
#include "CKLBUtility.h"
#include "TextureManagement.h"
#include "poly2tri/poly2tri.h"

#include <cstddef>
#include <vector>

namespace {

enum {
	POLYGON_CONTOUR_COUNT = 10,
	POLYGON_POINTS_PER_BLOCK = 510
};

struct PolygonPointBlock {
	PolygonPointBlock()
	: next(NULL)
	, used(0)
	, full(false)
	{}

	PolygonPointBlock*	next;
	u16					used;
	bool				full;
	p2t::Point			points[POLYGON_POINTS_PER_BLOCK];
};

}

class CKLBPolygonBuilder {
	friend class CKLBUIPolygon;
public:
	CKLBPolygonBuilder()
	: m_currentBlock(NULL)
	{}

	~CKLBPolygonBuilder();

	void rewind() {
		m_contourCount = 0;
		m_pointCount = 0;
		m_currentBlock = &m_firstBlock;

		for(PolygonPointBlock* block = &m_firstBlock;
			block;
			block = block->next) {
			block->used = 0;
		}
	}

	void reset() {
		for(s32 index = 0; index < m_contourCount; ++index) {
			m_contours[index].clear();
		}
		rewind();

		m_vertices.clear();
		m_colors.clear();
	}

	bool beginContour(bool hole) {
		bool hasRoom = m_contourCount < POLYGON_CONTOUR_COUNT;
		if(hasRoom) {
			m_contours[m_contourCount].clear();
			m_holes[m_contourCount] = hole;
			++m_contourCount;
		}
		return hasRoom;
	}

	p2t::Point* allocatePoint() {
		PolygonPointBlock* block = m_currentBlock;
		if(block->used < SizeOfArray(block->points)) {
			return &block->points[block->used++];
		}

		PolygonPointBlock* next = block->next;
		if(!next) {
			next = new PolygonPointBlock();
			block->next = next;
		}
		if(next->used >= SizeOfArray(next->points)) {
			return NULL;
		}
		m_currentBlock = next;
		return &next->points[next->used++];
	}

	const std::vector<p2t::Point*>& contour(s32 index) const {
		return m_contours[index];
	}

	bool isHole(s32 index) const {
		return m_holes[index];
	}

	s32 contourCount() const {
		return m_contourCount;
	}

	std::vector<float>& vertices() {
		return m_vertices;
	}

	std::vector<u32>& colors() {
		return m_colors;
	}

private:
	PolygonPointBlock			m_firstBlock;
	PolygonPointBlock*			m_currentBlock;
	std::vector<float>			m_vertices;
	std::vector<u32>				m_colors;
	std::vector<p2t::Point*>	m_contours[POLYGON_CONTOUR_COUNT];
	bool						m_holes[POLYGON_CONTOUR_COUNT];
	s32							m_contourCount;
	s32							m_pointCount;
};

typedef char PolygonClientDataMustBePointerSized[
	(sizeof(p2t::PointClientData) == sizeof(void*)) ? 1 : -1];
typedef char PolygonPointMustMatchTargetLayout[
	(sizeof(p2t::Point) == 0x30) ? 1 : -1];
typedef char PolygonPointStorageMustBeNaturallyAligned[
	(offsetof(PolygonPointBlock, points) == 0x10) ? 1 : -1];
typedef char PolygonPointBlockMustMatchTargetLayout[
	(sizeof(PolygonPointBlock) == 0x5fb0) ? 1 : -1];
typedef char PolygonBuilderMustMatchTargetLayout[
	(sizeof(CKLBPolygonBuilder) == 0x60f0) ? 1 : -1];

CKLBLuaPropTask::PROP_V2 CKLBUIPolygon::ms_propItems[] = {
	UI_BASE_PROP
};

CKLBPolygonBuilder::~CKLBPolygonBuilder()
{
}

CKLBUIPolygon::CKLBUIPolygon()
: CKLBUITask(P_MENU)
, m_textureAsset(NULL)
, m_textureHandle(0)
, m_render(NULL)
, m_builder(NULL)
{
	setNotAlwaysActive();
	m_newScriptModel = true;
}

CKLBUIPolygon::~CKLBUIPolygon()
{
}

void
CKLBUIPolygon::createBuilder()
{
	if(!m_builder) {
		m_builder = KLBNEW(CKLBPolygonBuilder);
		m_builder->rewind();
	}
}

void
CKLBUIPolygon::resetBuilder()
{
	if(m_builder) {
		m_builder->reset();
	}
}

u32
CKLBUIPolygon::appendVertex(float x, float y, u32 color)
{
	CKLBPolygonBuilder* builder = m_builder;
	for(s32 index = 0; index < static_cast<s32>(m_geometry.vertexCount); ++index) {
		float deltaX = x - builder->m_vertices[index * 2];
		float deltaY = y - builder->m_vertices[index * 2 + 1];
		if(deltaX * deltaX + deltaY * deltaY < 0.5f) {
			return static_cast<u32>(index);
		}
	}

	builder->m_vertices.push_back(x);
	builder->m_vertices.push_back(y);
	builder->m_colors.push_back(color);
	return m_geometry.vertexCount++;
}

bool
CKLBUIPolygon::bindTexture(const char* assetName, float scale)
{
	if(m_textureHandle) {
		CKLBDataHandler::releaseHandle(static_cast<u16>(m_textureHandle));
		m_textureHandle = 0;
		m_render->setTexture(static_cast<CTextureUsage*>(NULL));
	}

	u32 handle = 0;
	if(assetName) {
		CKLBImageAsset* asset = static_cast<CKLBImageAsset*>(
			CKLBUtility::loadAssetScript(assetName, &handle));
		m_textureAsset = asset;
		if(!asset || !asset->hasStandardAttribute(CKLBImageAsset::IS_STANDARD_RECT)) {
			return false;
		}

		const float* uv = asset->getUVBuffer();
		SKLBRect* size = asset->getSize();
		s32 width = size->getWidth();
		s32 height = size->getHeight();
		float originU = uv[0];
		float originV = uv[1];
		float edgeU = uv[2];
		float edgeV = uv[3];
		float lowerU = uv[6];
		float lowerV = uv[7];
		float horizontalU = (edgeU - originU) / width;
		float horizontalV = (edgeV - originV) / width;
		float verticalU = (lowerU - originU) / height;
		float verticalV = (lowerV - originV) / height;
		float inverseScale = 1.0f / scale;
		m_textureMapping.scale[0] = horizontalU * inverseScale;
		m_textureMapping.scale[1] = horizontalV * inverseScale;
		m_textureMapping.scale[2] = verticalU * inverseScale;
		m_textureMapping.scale[3] = verticalV * inverseScale;
		m_textureMapping.origin[0] = uv[0];
		m_textureMapping.origin[1] = uv[1];
		m_render->setTexture(asset->getTexture()->m_pTextureUsage);
	} else {
		m_render->setTexture(static_cast<CTextureUsage*>(NULL));
	}

	m_textureHandle = handle;
	return true;
}

void
CKLBUIPolygon::updateTextureCoordinates()
{
	float* uv = m_render->getSrcUVBuffer();
	if(m_textureAsset) {
		float* vertices = m_render->getSrcXYBuffer();
		for(s64 index = 0; index < static_cast<s64>(m_geometry.vertexCount); ++index) {
			float x = vertices[static_cast<s32>(index) * 2];
			float y = vertices[static_cast<s32>(index) * 2 + 1];
			uv[index * 2] =
				m_textureMapping.scale[0] * x +
				m_textureMapping.scale[1] * y +
				m_textureMapping.origin[0];
			uv[index * 2 + 1] =
				m_textureMapping.scale[2] * x +
				m_textureMapping.scale[3] * y +
				m_textureMapping.origin[1];
		}
	} else {
		for(s32 index = 0; index < m_geometry.vertexCount; ++index) {
			uv[0] = 0.5f;
			uv[1] = 0.5f;
			uv += 2;
		}
	}

	getNode()->markUpMatrixAndColor();
	getNode()->markUpRender();
}

void
CKLBUIPolygon::releaseResources(bool releaseTexture)
{
	if(m_textureHandle && releaseTexture) {
		CKLBDataHandler::releaseHandle(static_cast<u16>(m_textureHandle));
		m_textureHandle = 0;
	}

	if(m_builder) {
		m_builder->reset();
		PolygonPointBlock* block = m_builder->m_firstBlock.next;
		while(block) {
			PolygonPointBlock* next = block->next;
			KLBDELETE(block);
			block = next;
		}
		KLBDELETE(m_builder);
		m_builder = NULL;
	}
}

CKLBUIPolygon*
CKLBUIPolygon::create(CKLBUITask* parent, CKLBNode* node, u32 order)
{
	CKLBUIPolygon* task = KLBNEW(CKLBUIPolygon);
	if(!task) {
		return NULL;
	}
	if(!task->init(parent, node, order)) {
		KLBDELETE(task);
		return NULL;
	}
	return task;
}

bool
CKLBUIPolygon::init(CKLBUITask* parent, CKLBNode* /* node */, u32 order)
{
	if(!setupNode()) {
		return false;
	}
	bool result = initCore(order);
	return registUI(parent, result);
}

bool
CKLBUIPolygon::initCore(u32 order)
{
	if(!setupPropertyList((const char**)ms_propItems, SizeOfArray(ms_propItems))) {
		return false;
	}

	klb_assertNull(static_cast<s32>(order) >= 0, "Order Problem");
	m_order = order;

	createBuilder();

	CKLBRenderingManager& renderingManager = CKLBRenderingManager::getInstance();
	m_render = renderingManager.allocateCommandDynSprite(0, 0, 0);
	if(!m_render) {
		return false;
	}

	getNode()->setRender(m_render);
	m_render->setTexture(static_cast<CTextureUsage*>(NULL));
	getNode()->setRenderOnDestroy(true);
	getNode()->setPriority(order);
	return m_render && m_builder;
}

int
CKLBUIPolygon::commandUI(CLuaState& lua, int argc, int cmd)
{
	createBuilder();
	CKLBPolygonBuilder* builder = m_builder;

	int result = false;
	switch(cmd) {
	case NEW_PATH:
		result = builder->beginContour(false);
		break;
	case NEW_HOLE:
		result = builder->beginContour(true);
		break;
	case ADD_POINT:
		result = builder->contourCount() > 0 && argc > 3;
		if(result) {
			p2t::Point* point = builder->allocatePoint();
			if(!point) {
				break;
			}
			point->x = lua.getInt(3);
			point->y = lua.getInt(4);

			point->client_data.polygon.has_color = argc >= 6;
			u32 color = 0xffffffff;
			if(point->client_data.polygon.has_color) {
				u32 rgb = static_cast<u32>(lua.getInt(5));
				u32 alpha = static_cast<u32>(lua.getInt(6));
				color = (rgb & 0xff00ff00)
				      | ((rgb & 0x000000ff) << 16)
				      | ((rgb & 0x00ff0000) >> 16)
				      | (alpha << 24);
			}
			point->client_data.polygon.color = color;
			builder->m_contours[
				builder->m_contourCount - 1].push_back(point);
			++builder->m_pointCount;
		}
		break;
	case END_HOLE:
	case PUSH_PATH:
		result = true;
		break;
	case SET_TEXTURE:
		if(argc >= 4) {
			if(lua.isNil(3)) {
				result = bindTexture(NULL, 0.0f);
			} else {
				result = bindTexture(lua.getString(3), lua.getFloat(4));
			}
			if(argc >= 5 && lua.getBool(5)) {
				updateTextureCoordinates();
			}
		}
		break;
	case BUILD:
		result = argc >= 4;
		if(result) {
			u32 rgb = static_cast<u32>(lua.getInt(3));
			u32 alpha = static_cast<u32>(lua.getInt(4));
			if(alpha > 255) {
				alpha = 255;
			}
			u32 color = (rgb & 0x0000ff00)
			          | ((rgb & 0x000000ff) << 16)
			          | ((rgb & 0x00ff0000) >> 16)
			          | (alpha << 24);

			std::vector<u16> indices;
			m_geometry.vertexCount = 0;
			builder->m_vertices.clear();

			s32 triangleCount = 0;
			s32 contourIndex = 0;
			while(contourIndex < builder->contourCount()
			&& !builder->isHole(contourIndex)) {
				p2t::CDT* triangulation =
					new p2t::CDT(builder->contour(contourIndex));
				++contourIndex;
				while(contourIndex < builder->contourCount()
				&& builder->isHole(contourIndex)) {
					triangulation->AddHole(
						builder->contour(contourIndex));
					++contourIndex;
				}
				triangulation->GetSweep()->Triangulate(
					*triangulation->GetSweepContext());

				std::vector<p2t::Triangle*> triangles =
					triangulation->GetTrianglesRef();
				for(std::size_t triangleIndex = 0;
					triangleIndex < triangles.size();
					++triangleIndex) {
					p2t::Triangle* triangle = triangles[triangleIndex];
					p2t::Point* point0 = triangle->GetPoint(0);
					p2t::Point* point1 = triangle->GetPoint(1);
					p2t::Point* point2 = triangle->GetPoint(2);
					u16 index0 = static_cast<u16>(appendVertex(
						static_cast<float>(point0->x),
						static_cast<float>(point0->y),
						point0->client_data.polygon.has_color
							? point0->client_data.polygon.color
							: color));
					u16 index1 = static_cast<u16>(appendVertex(
						static_cast<float>(point1->x),
						static_cast<float>(point1->y),
						point1->client_data.polygon.has_color
							? point1->client_data.polygon.color
							: color));
					u16 index2 = static_cast<u16>(appendVertex(
						static_cast<float>(point2->x),
						static_cast<float>(point2->y),
						point2->client_data.polygon.has_color
							? point2->client_data.polygon.color
							: color));
					indices.push_back(index0);
					indices.push_back(index1);
					indices.push_back(index2);
				}
				triangleCount += static_cast<s32>(triangles.size());
				delete triangulation;
			}

			s32 indexCount = triangleCount * 3;
			m_render->setTriangleCount(
				static_cast<u16>(m_geometry.vertexCount),
				static_cast<u16>(indexCount),
				false);

			u16* indexBuffer = m_render->getSrcIndexBuffer();
			for(s32 index = 0; index < indexCount; ++index) {
				indexBuffer[index] = indices[index];
			}

			const std::vector<float>& vertices = builder->m_vertices;
			float* vertexBuffer = m_render->getSrcXYBuffer();
			for(s32 index = 0;
				index < m_geometry.vertexCount;
				++index) {
				vertexBuffer[index * 2] = vertices[index * 2];
				vertexBuffer[index * 2 + 1] = vertices[index * 2 + 1];
			}

			updateTextureCoordinates();

			const std::vector<u32>& colors = builder->m_colors;
			if(m_geometry.vertexCount > 0) {
				m_render->setVertexColor(
					getNode(), 0, colors[0]);
				for(s32 index = 1;
					index < m_geometry.vertexCount;
					++index) {
					m_render->setVertexColor(
						NULL, index, colors[index]);
				}
			}

			getNode()->markUpMatrixAndColor();
			m_render->setVICount(
				m_geometry.vertexCount,
				indexCount);
			getNode()->markUpRender();
			getNode()->markUpMatrixAndColor();
		}
		releaseResources(false);
		break;
	}

	lua.retBool(result);
	return 1;
}
