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
#include "CKLBUIPolyline2.h"
#include "CKLBUtility.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace {

enum StrokeJoin {
	STROKE_ENDPOINT = 0,
	STROKE_LEFT_JOIN,
	STROKE_RIGHT_JOIN
};

enum StrokeJoinResult {
	STROKE_NO_JOIN = 0,
	STROKE_MITER_JOIN,
	STROKE_BEVEL_JOIN
};

static u32
renderColor(u32 argb)
{
	return (argb & 0xff00ff00)
	     | ((argb & 0x000000ff) << 16)
	     | ((argb & 0x00ff0000) >> 16);
}

}

/*
 * Solve the join between the previous stroke edge (a->b) and the current one
 * (c->d).  When the miter would run further than the stroke can carry, the
 * join degenerates into a bevel and the caller gets both of its corners.
 */
s32
solveStrokeJoin(
	float maximumDistanceSquared,
	float ax, float ay, float bx, float by,
	float cx, float cy, float dx, float dy,
	float previousDirectionX, float previousDirectionY,
	float currentDirectionX, float currentDirectionY,
	float& joinX, float& joinY, float& bevelX, float& bevelY)
{
	float cdx = dx - cx;
	float cdy = dy - cy;
	float acy = ay - cy;
	float abx = bx - ax;
	float firstNumerator = acy * cdx - cdy * (ax - cx);
	float denominator = abx * cdy - cdx * (by - ay);
	float secondNumerator = abx * acy - (by - ay) * (ax - cx);

	if((fabsf(firstNumerator) < 0.0000001f
	&& fabsf(secondNumerator) < 0.0000001f
	&& fabsf(denominator) < 0.0000001f)
	|| fabsf(denominator) < 0.0000001f) {
		joinX = (bx + cx) * 0.5f;
		joinY = (by + cy) * 0.5f;
		return !(fabsf(denominator) < 0.0000001f);
	}

	float distance = firstNumerator / denominator;
	float x = ax + abx * distance;
	float y = ay + (by - ay) * distance;
	float deltaX = x - bx;
	float deltaY = y - by;
	if(deltaX * deltaX + deltaY * deltaY > maximumDistanceSquared
	&& (distance > 1.0f || distance < 0.0f)) {
		joinX = bx + previousDirectionX;
		joinY = by + previousDirectionY;
		bevelX = cx - currentDirectionX;
		bevelY = cy - currentDirectionY;
		return STROKE_BEVEL_JOIN;
	}
	joinX = x;
	joinY = y;
	return STROKE_MITER_JOIN;
}

s32
isStrokeSampleCovered(float x, float y, float radiusSquared)
{
	return x * x + y * y < radiusSquared;
}

s32
rasterizeStrokeCoverage(
	s32 sampleCount,
	float x,
	float y,
	float sampleStep,
	float radiusSquared)
{
	float coverage = 0.0f;
	if(sampleCount > 0) {
		const float firstSampleOffset = sampleStep / (float)sampleCount;
		x += firstSampleOffset;
		y += firstSampleOffset;

		s32 covered = 0;
		for(s32 sampleY = 0; sampleY < sampleCount; ++sampleY) {
			float sampleXPosition = x;
			for(s32 sampleX = 0; sampleX < sampleCount; ++sampleX) {
				if(isStrokeSampleCovered(
					sampleXPosition, y, radiusSquared)) {
					++covered;
				}
				sampleXPosition += sampleStep;
			}
			y += sampleStep;
		}
		coverage = (float)covered;
	}

	return (s32)(
		(coverage / (float)(sampleCount * sampleCount)) * 255.0f);
}

CKLBLuaPropTask::PROP_V2 CKLBUIPolyline2::ms_propItems[] = {
	UI_BASE_PROP
};

CKLBUIPolyline2::CKLBUIPolyline2()
: CKLBUITask(P_MENU)
, m_cachedStrokeWidth(-1.0f)
, m_strokeTexture(NULL)
, m_strokeTextureUsage(NULL)
, m_pMesh(NULL)
{
	setNotAlwaysActive();
	m_newScriptModel = true;
}

CKLBUIPolyline2::~CKLBUIPolyline2()
{
	releaseStrokeTexture();
}

void
CKLBUIPolyline2::releaseStrokeTexture()
{
	if(m_strokeTexture) {
		if(m_strokeTextureUsage) {
			m_strokeTexture->releaseUsage(m_strokeTextureUsage);
		}
		m_strokeTextureUsage = NULL;
		CKLBOGLWrapper::getInstance().releaseTexture(m_strokeTexture);
	}
}

void
CKLBUIPolyline2::prepareStrokeTexture(
	float width, float* u, float* upperV, float* lowerV)
{
	if(m_cachedStrokeWidth == width) {
		return;
	}
	m_cachedStrokeWidth = width;

	s32 textureHeightInput = (s32)width + 2;
	s32 center = (s32)(width * 0.5f + 1.0f);
	s32 textureWidth = (s32)CKLBUtility::nearest2Pow(center * 2 - 1);
	s32 textureHeight =
		(s32)CKLBUtility::nearest2Pow(textureHeightInput);
	s32 stride = textureWidth * 4;
	s32 byteCount = stride * textureHeight;
	u8* pixels = (u8*)malloc(byteCount);
	if(!pixels) {
		return;
	}
	float textureWidthFloat = (float)textureWidth;
	float textureHeightFloat = (float)textureHeight;

	memset(pixels, 0xff, byteCount);
	for(s32 alpha = 3; alpha < byteCount; alpha += 4) {
		pixels[alpha] = 0;
	}

	float inverseWidth = 1.0f / textureWidthFloat;
	float inverseHeight = 1.0f / textureHeightFloat;
	s32 strokeEdge = center - 1;
	if(width >= 1.0f) {
		u8* centerAlpha =
			pixels + center * 4 + stride - 1;
		for(s32 y = 1; (float)y <= width; ++y) {
			*centerAlpha = 0xff;
			centerAlpha += stride;
		}
	}
	*u = ((float)strokeEdge + 0.5f) * inverseWidth;
	double inverseHeightDouble = inverseHeight;
	*upperV = 0.5f * inverseHeight;
	*lowerV = (float)((width + 1 + 0.5) * inverseHeightDouble);

	/*
	 * Rasterize one half of the round stroke profile. Four samples in each
	 * axis preserve a smooth edge while the fully covered center column is
	 * kept opaque. The mesh mirrors this profile across the line.
	 */
	const float radiusSquared =
		(float)((width * 0.5) * (width * 0.5));
	for(s32 y = 0; (float)y < width + 1.0f; ++y) {
		for(s32 x = 0; x < strokeEdge; ++x) {
			s32 coverage = rasterizeStrokeCoverage(
				4,
				(float)x + width * -0.5f,
				(float)y + (width * -0.5f - 1.0f),
				0.25f,
				radiusSquared);
			pixels[y * stride + x * 4 + 3] =
				(u8)coverage;
		}
	}

	releaseStrokeTexture();
	m_strokeTexture = CKLBOGLWrapper::getInstance().createTexture(
		textureWidth, textureHeight, GL_UNSIGNED_BYTE,
		CKLBOGLWrapper::RGBA, pixels, byteCount);
	free(pixels);
	if(m_strokeTexture) {
		m_strokeTextureUsage = m_strokeTexture->createUsage();
	}
	m_pMesh->setTexture(m_strokeTextureUsage);
}

u32
CKLBUIPolyline2::getClassID()
{
	return 0x00080101;
}

CKLBUIPolyline2*
CKLBUIPolyline2::create(CKLBUITask* parent, CKLBNode* node, u32 order)
{
	CKLBUIPolyline2* task = KLBNEW(CKLBUIPolyline2);
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
CKLBUIPolyline2::init(CKLBUITask* parent, CKLBNode* /* node */, u32 order)
{
	if(!setupNode()) {
		return false;
	}
	bool result = initCore((s32)order);
	return registUI(parent, result);
}

bool
CKLBUIPolyline2::initCore(s32 order)
{
	if(!setupPropertyList(
		(const char**)ms_propItems, SizeOfArray(ms_propItems))) {
		return false;
	}

	klb_assertNull(order >= 0, "Order Problem");
	m_order = (u32)order;

	m_pMesh = CKLBRenderingManager::getInstance()
		.allocateCommandDynSprite(0, 0);
	if(m_pMesh) {
		getNode()->setRender(m_pMesh, 0);
		m_pMesh->setTexture((CTextureUsage*)NULL);
		getNode()->setRenderOnDestroy(true);
		getNode()->setPriority((u32)order);
	}
	return m_pMesh != NULL;
}

void
CKLBUIPolyline2::execute(u32 /*deltaT*/)
{
}

void
CKLBUIPolyline2::dieUI()
{
}

void
CKLBUIPolyline2::build(u32 color, bool close, float width, bool antialias)
{
	(void)close;

	if(m_points.size() <= 1) {
		m_pMesh->setVICount(0, 0);
		getNode()->markUpRender();
		getNode()->markUpMatrixAndColor();
		return;
	}

	std::vector<float> leftSegments;
	std::vector<float> rightSegments;
	std::vector<float> vertices;
	std::vector<float> textureCoordinates;
	std::vector<u8> joins;
	std::vector<float> directions;

	if(antialias) {
		width = (float)(s32)width;
		if(width < 1.0f) {
			width = 1.0f;
		}
	}
	width = (width < 0.0f) ? 0.0f : width;
	width = (width > 30.0f) ? 30.0f : width;

	float radius = (width + (float)antialias) * 0.5f;
	if(!antialias) {
		m_textureUScale = 0.0f;
		m_textureVScale = 0.0f;
		m_textureVOffset = 1.0f;
		m_strokeTextureUsage = NULL;
	} else {
		prepareStrokeTexture(
			width, &m_textureUScale, &m_textureVScale, &m_textureVOffset);
	}

	const float maximumJoinDistanceSquared =
		((double)radius * 1.414) * ((double)radius * 1.414);
	s32 pointCount = (s32)(m_points.size() / 2);
	s32 segmentCount = pointCount - 1;
	u64 vertexCount = 0;
	u64 indexCount = 0;
	float currentX = 0.0f;
	float currentY = 0.0f;
	float offsetX = 0.0f;
	float offsetY = 0.0f;

	for(s32 i = 0; i < pointCount; ++i) {
		currentX = m_points[i * 2];
		currentY = m_points[i * 2 + 1];
		if(i + 1 >= pointCount) {
			continue;
		}

		float x1 = m_points[i * 2 + 2];
		float y1 = m_points[i * 2 + 3];
		float dx = x1 - currentX;
		float dy = y1 - currentY;
		float length = sqrtf(dx * dx + dy * dy);
		if(length <= 0.0f) {
			length = 0.0001f;
		}

		offsetX = dy / length * radius;
		offsetY = -dx / length * radius;

		leftSegments.push_back(currentX + offsetX);
		leftSegments.push_back(currentY + offsetY);
		leftSegments.push_back(x1 + offsetX);
		leftSegments.push_back(y1 + offsetY);

		rightSegments.push_back(currentX - offsetX);
		rightSegments.push_back(currentY - offsetY);
		rightSegments.push_back(x1 - offsetX);
		rightSegments.push_back(y1 - offsetY);

		directions.push_back(-offsetY);
		directions.push_back(offsetX);

		if(i > 0 || !antialias) {
			continue;
		}

		float normalX = leftSegments[0] - currentX;
		float normalY = leftSegments[1] - currentY;

		joins.push_back(STROKE_ENDPOINT);
		vertices.push_back(currentX + offsetY + normalX);
		vertices.push_back(currentY - offsetX + normalY);
		vertices.push_back(currentX + offsetY - normalX);
		vertices.push_back(currentY - offsetX - normalY);
		textureCoordinates.push_back(0.0f);
		textureCoordinates.push_back(m_textureVScale);
		textureCoordinates.push_back(0.0f);
		textureCoordinates.push_back(m_textureVOffset);
		vertexCount += 2;
		indexCount += 6;
	}

	for(s32 i = 0; i < segmentCount; ++i) {
		s32 current = i * 4;
		s32 previous = ((segmentCount - 1 + i) % segmentCount) * 4;
		float leftX;
		float leftY;
		float rightX;
		float rightY;
		u8 join = STROKE_ENDPOINT;
		float bevelX = 0.0f;
		float bevelY = 0.0f;

		if(i == 0) {
			leftX = leftSegments[current];
			leftY = leftSegments[current + 1];
			rightX = rightSegments[current];
			rightY = rightSegments[current + 1];
		} else {
			if(solveStrokeJoin(maximumJoinDistanceSquared,
				leftSegments[previous], leftSegments[previous + 1],
				leftSegments[previous + 2], leftSegments[previous + 3],
				leftSegments[current], leftSegments[current + 1],
				leftSegments[current + 2], leftSegments[current + 3],
				directions[(i - 1) * 2], directions[(i - 1) * 2 + 1],
				directions[i * 2], directions[i * 2 + 1],
				leftX, leftY, bevelX, bevelY) == STROKE_BEVEL_JOIN) {
				join = STROKE_LEFT_JOIN;
			}

			if(solveStrokeJoin(maximumJoinDistanceSquared,
				rightSegments[previous], rightSegments[previous + 1],
				rightSegments[previous + 2], rightSegments[previous + 3],
				rightSegments[current], rightSegments[current + 1],
				rightSegments[current + 2], rightSegments[current + 3],
				directions[(i - 1) * 2], directions[(i - 1) * 2 + 1],
				directions[i * 2], directions[i * 2 + 1],
				rightX, rightY, bevelX, bevelY) == STROKE_BEVEL_JOIN) {
				join = STROKE_RIGHT_JOIN;
			}
		}

		joins.push_back(join);
		vertices.push_back(leftX);
		vertices.push_back(leftY);
		textureCoordinates.push_back(m_textureUScale);
		textureCoordinates.push_back(m_textureVScale);
		vertices.push_back(rightX);
		vertices.push_back(rightY);
		textureCoordinates.push_back(m_textureUScale);
		textureCoordinates.push_back(m_textureVOffset);

		if(join != STROKE_ENDPOINT) {
			vertices.push_back(bevelX);
			vertices.push_back(bevelY);
			textureCoordinates.push_back(m_textureUScale);
			textureCoordinates.push_back(
				join == STROKE_LEFT_JOIN
				? m_textureVScale : m_textureVOffset);
			++vertexCount;
			indexCount += 3;
		}
		vertexCount += 2;
		indexCount += 6;
	}

	joins.push_back(STROKE_ENDPOINT);
	s32 last = segmentCount * 4 - 2;
	vertices.push_back(leftSegments[last]);
	vertices.push_back(leftSegments[last + 1]);
	vertices.push_back(rightSegments[last]);
	vertices.push_back(rightSegments[last + 1]);
	textureCoordinates.push_back(m_textureUScale);
	textureCoordinates.push_back(m_textureVScale);
	textureCoordinates.push_back(m_textureUScale);
	textureCoordinates.push_back(m_textureVOffset);
	vertexCount += 2;

	if(antialias) {
		/*
		 * The antialias texture has a one-pixel end cap. Extend the final
		 * cross-section by one scaled tangent so the cap can be sampled
		 * without shortening the visible path.
		 */
		joins.push_back(STROKE_ENDPOINT);
		float endDX = directions[(segmentCount - 1) * 2];
		float endDY = directions[(segmentCount - 1) * 2 + 1];
		float normalX =
			leftSegments[segmentCount * 4 - 2] - currentX;
		float normalY =
			leftSegments[segmentCount * 4 - 1] - currentY;

		vertices.push_back(currentX + endDX + normalX);
		vertices.push_back(currentY + endDY + normalY);
		vertices.push_back(currentX + endDX - normalX);
		vertices.push_back(currentY + endDY - normalY);
		textureCoordinates.push_back(0.0f);
		textureCoordinates.push_back(m_textureVScale);
		textureCoordinates.push_back(0.0f);
		textureCoordinates.push_back(m_textureVOffset);
		vertexCount += 2;
		indexCount += 6;
	}

	if(!m_pMesh->setTriangleCount(
		(u16)vertexCount, (u16)indexCount, false)) {
		return;
	}
	m_pMesh->setTexture(m_strokeTextureUsage);

	u16* indices = m_pMesh->getSrcIndexBuffer();
	s32 first = 0;
	s32 connectionCount = segmentCount + (antialias ? 2 : 0);
	for(s32 i = 0; i < connectionCount; ++i) {
		u8 join = joins[i];
		if(join == STROKE_ENDPOINT) {
			*indices++ = first + 2;
			*indices++ = first + 3;
			*indices++ = first + 1;
			*indices++ = first + 2;
			*indices++ = first + 1;
			*indices++ = first;
		} else if(join == STROKE_RIGHT_JOIN) {
			*indices++ = first;
			*indices++ = first + 1;
			*indices++ = first + 2;
			*indices++ = first + 2;
			*indices++ = first;
			*indices++ = first + 3;
			*indices++ = first + 2;
			*indices++ = first + 3;
			*indices++ = first + 4;
		} else {
			*indices++ = first;
			*indices++ = first + 2;
			*indices++ = first + 1;
			*indices++ = first + 3;
			*indices++ = first + 4;
			*indices++ = first + 1;
			*indices++ = first + 3;
			*indices++ = first + 1;
			*indices++ = first + 2;
		}
		first += 2 + (join != STROKE_ENDPOINT);
	}

	float* destinationXY = m_pMesh->getSrcXYBuffer();
	float* destinationUV = m_pMesh->getSrcUVBuffer();
	for(s32 vertex = 0; vertex < (s32)vertexCount; ++vertex) {
		s32 coordinate = vertex * 2;
		*destinationXY++ = vertices[coordinate];
		*destinationXY++ = vertices[coordinate + 1];
		*destinationUV++ = textureCoordinates[coordinate];
		*destinationUV++ = textureCoordinates[coordinate + 1];
	}

	u32 convertedColor = renderColor(color);
	for(s32 vertex = 0; vertex < (s32)vertexCount; ++vertex) {
		m_pMesh->setVertexColor(NULL, vertex, convertedColor);
	}
	m_pMesh->setVertexColor(getNode(), 0, convertedColor);
	getNode()->markUpMatrixAndColor();
	m_pMesh->setVICount(vertexCount, indexCount);
	getNode()->markUpRender();
	getNode()->markUpMatrixAndColor();
}

bool
CKLBUIPolyline2::initUI(CLuaState& lua)
{
	int argc = lua.numArgs();
	if(argc != 2) {
		return false;
	}

	s32 order = lua.getInt(2);
	return initCore(order);
}

int
CKLBUIPolyline2::commandUI(CLuaState& lua, int argc, int cmd)
{
	switch(cmd) {
	case BUILD:
		if(argc <= 5) {
			lua.retBool(false);
			return 1;
		}
		{
			u32 color = (u32)lua.getInt(3) & 0x00ffffff;
			u32 alpha = (u32)lua.getInt(4);
			if(alpha > 0xff) {
				alpha = 0xff;
			}
			float width = lua.getFloat(5);

			// The shipped dispatcher validates this legacy argument, but its
			// build call always uses an open path.
			(void)lua.getBool(6);
			bool antialias = argc >= 7 ? lua.getBool(7) : true;
			build(color | (alpha << 24), false, width, antialias);
		}
		// Building consumes the accumulated points just like an explicit clear.

	case CLEAR:
		m_points.clear();
		break;

	case ADD_POINT:
		if(argc != 4) {
			lua.retBool(false);
			return 1;
		}
		m_points.push_back(lua.getFloat(3));
		m_points.push_back(lua.getFloat(4));
		break;

	default:
		lua.retBool(false);
		return 1;
	}
	lua.retBool(true);
	return 1;
}
