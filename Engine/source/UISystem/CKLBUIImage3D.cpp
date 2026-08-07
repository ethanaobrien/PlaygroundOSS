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
#include "CKLBUIImage3D.h"
#include "CKLBDataHandler.h"
#include "CKLBRendering.h"
#include "CKLBUtility.h"
#include "TextureManagement.h"

enum {
	UI_IMAGE3D_SETCENTER,
	UI_IMAGE3D_SETROTATION,
	UI_IMAGE3D_SETFOV,
	UI_IMAGE3D_SETIMAGE,
	UI_IMAGE3D_SETSUBIMAGE,
	UI_IMAGE3D_SETCOLORS,
	UI_IMAGE3D_SETSCALEWIDTH,
	UI_IMAGE3D_SETSCALEHEIGHT,
	UI_IMAGE3D_SETSCALEWIDTHHEIGHT,
	UI_IMAGE3D_RESETSCALE,
	UI_IMAGE3D_SETCENTERMODE_X,
	UI_IMAGE3D_SETCENTERMODE_Y
};

enum {
	UI_IMAGE3D_ALIGN_LEFT,
	UI_IMAGE3D_ALIGN_CENTER,
	UI_IMAGE3D_ALIGN_RIGHT
};

static IFactory::DEFCMD cmd[] = {
	{ "UI_IMAGE3D_SETCENTER",          UI_IMAGE3D_SETCENTER          },
	{ "UI_IMAGE3D_SETROTATION",        UI_IMAGE3D_SETROTATION        },
	{ "UI_IMAGE3D_SETFOV",             UI_IMAGE3D_SETFOV             },
	{ "UI_IMAGE3D_SETIMAGE",           UI_IMAGE3D_SETIMAGE           },
	{ "UI_IMAGE3D_SETSUBIMAGE",        UI_IMAGE3D_SETSUBIMAGE        },
	{ "UI_IMAGE3D_SETCOLORS",          UI_IMAGE3D_SETCOLORS          },
	{ "UI_IMAGE3D_SETSCALEWIDTH",      UI_IMAGE3D_SETSCALEWIDTH      },
	{ "UI_IMAGE3D_SETSCALEHEIGHT",     UI_IMAGE3D_SETSCALEHEIGHT     },
	{ "UI_IMAGE3D_SETSCALEWIDTHHEIGHT", UI_IMAGE3D_SETSCALEWIDTHHEIGHT },
	{ "UI_IMAGE3D_RESETSCALE",         UI_IMAGE3D_RESETSCALE         },
	{ "UI_IMAGE3D_SETCENTERMODE_X",    UI_IMAGE3D_SETCENTERMODE_X    },
	{ "UI_IMAGE3D_SETCENTERMODE_Y",    UI_IMAGE3D_SETCENTERMODE_Y    },
	{ "UI_IMAGE3D_ALIGN_LEFT",         UI_IMAGE3D_ALIGN_LEFT         },
	{ "UI_IMAGE3D_ALIGN_CENTER",       UI_IMAGE3D_ALIGN_CENTER       },
	{ "UI_IMAGE3D_ALIGN_RIGHT",        UI_IMAGE3D_ALIGN_RIGHT        },
	{ 0, 0 }
};

static CKLBTaskFactory<CKLBUIImage3D> factory("UI_Image3D", CKLBUIImage3D::CLASS_ID, cmd);

CKLBLuaPropTask::PROP_V2 CKLBUIImage3D::ms_propItems[] = {
	UI_BASE_PROP,
	{ "order", R_UINTEGER, NULL, (getBoolT)&CKLBUIImage3D::getOrder, 0 }
};

CKLBUIImage3D::CKLBUIImage3D()
: CKLBUITask(P_UIAFTER)
{
	m_scaleMode = 0;
	m_centerX = 0.0f;
	m_centerY = 0.0f;
	m_fieldOfView = 1.0f;
	m_vertexCount = 0;
	m_vertexBuffer = NULL;
	m_dynSprite = NULL;
	m_asset = NULL;
	m_textureHandle = 0;
	m_divisionWidth = 1;
	m_divisionHeight = 1;
	m_rotation[0] = 0.0f;
	m_rotation[1] = 0.0f;
	m_rotation[2] = 0.0f;
	m_colors[0] = 0xffffffff;
	m_colors[1] = 0xffffffff;
	m_colors[2] = 0xffffffff;
	m_colors[3] = 0xffffffff;
	m_centerModeX = 0;
	m_centerModeY = 0;
	setNotAlwaysActive();
	m_newScriptModel = true;
}

CKLBUIImage3D::~CKLBUIImage3D()
{
}

u32
CKLBUIImage3D::getClassID()
{
	return CLASS_ID;
}

CKLBUIImage3D*
CKLBUIImage3D::create(CKLBUITask* parent, CKLBNode* node, u32 order, float x, float y)
{
	CKLBUIImage3D* task = KLBNEW(CKLBUIImage3D);
	if (!task) {
		return NULL;
	}
	if (task->setupNode()) {
		bool result = task->registUI(parent, task->setup(static_cast<s32>(order), x, y));
		if (node) {
			parent->getNode()->removeNode(task->getNode());
			node->addNode(task->getNode());
		}
		if (result) {
			return task;
		}
	}
	KLBDELETE(task);
	return NULL;
}

bool
CKLBUIImage3D::init(CKLBUITask* parent, CKLBNode* node, s32 order, float x, float y)
{
	if (!setupNode()) {
		return false;
	}
	bool result = setup(order, x, y);
	result = registUI(parent, result);
	if (node) {
		parent->getNode()->removeNode(getNode());
		node->addNode(getNode());
	}
	return result;
}

u32
CKLBUIImage3D::getOrder()
{
	return m_order;
}

void
CKLBUIImage3D::setOrder(u32)
{
	CKLBRenderingManager& renderingManager = CKLBRenderingManager::getInstance();
	m_dynSprite->changeOrder(renderingManager, m_order);
}

void
CKLBUIImage3D::execute(u32)
{
	executeProjection();
	m_renderNode->markUpMatrix();
	RESET_A;
}

bool
CKLBUIImage3D::releaseMesh()
{
	CKLBRenderingManager& renderingManager = CKLBRenderingManager::getInstance();
	m_renderNode->setRender(NULL, 0);
	if (m_dynSprite) {
		renderingManager.releaseCommand(m_dynSprite);
		m_dynSprite = NULL;
	}
	if (m_vertexBuffer) {
		delete [] m_vertexBuffer;
	}
	m_vertexBuffer = NULL;
	CKLBDataHandler::releaseHandle(static_cast<u16>(m_textureHandle));
	m_textureHandle = 0;
	return true;
}

bool
CKLBUIImage3D::allocMesh(float x, float y, const char* assetName, int vertexCount, int indexCount)
{
	if (vertexCount <= 2 || indexCount <= 2 || !assetName) {
		return false;
	}

	CKLBRenderingManager& renderingManager = CKLBRenderingManager::getInstance();
	u32 handle;
	CKLBImageAsset* asset = static_cast<CKLBImageAsset*>(CKLBUtility::loadAssetScript(assetName, &handle));
	CKLBDynSprite* sprite =
		renderingManager.allocateCommandDynSprite(static_cast<u16>(vertexCount), static_cast<u16>(indexCount), m_order);
	SVertex* vertexBuffer = KLBNEWA(SVertex, vertexCount);

	if (!asset || !sprite) {
		if (asset && handle) {
			CKLBDataHandler::releaseHandle(static_cast<u16>(handle));
		}
		if (sprite) {
			renderingManager.releaseCommand(sprite);
		}
		KLBDELETEA(vertexBuffer);
		return false;
	}

	m_asset = asset;
	m_dynSprite = sprite;
	m_vertexBuffer = vertexBuffer;
	m_textureHandle = handle;
	m_vertexCount = static_cast<u32>(vertexCount);
	m_textureU = x;
	m_textureV = y;
	m_renderNode->setRender(sprite, 0);
	sprite->setTexture(asset);
	return true;
}

bool
CKLBUIImage3D::createMesh(const char* assetName, float x, float y,
	int divisionWidth, int divisionHeight, int offsetX, int offsetY,
	int countX, int countY)
{
	if (divisionWidth < 1) {
		divisionWidth = 1;
	}
	if (divisionHeight < 1) {
		divisionHeight = 1;
	}

	KLBTextureAssetPlugin* texturePlugin =
		static_cast<KLBTextureAssetPlugin*>(
			CKLBAssetManager::getInstance().getPlugin('T'));
	texturePlugin->setDisableImageSizeOptimization(true);

	bool result = allocMesh(x, y, assetName,
		(divisionWidth + 1) * (divisionHeight + 1),
		divisionWidth * divisionHeight * 6);
	if (result) {
		buildMesh(divisionWidth, divisionHeight,
			offsetX, offsetY, countX, countY);
	}

	texturePlugin->setDisableImageSizeOptimization(false);
	return result;
}

bool
CKLBUIImage3D::initUI(CLuaState& lua)
{
	int argc = lua.numArgs();
	if (argc < 4) {
		return false;
	}
	u32 order = lua.getInt(2);
	float x = lua.getFloat(3);
	float y = lua.getFloat(4);
	return setup(static_cast<s32>(order), x, y);
}

bool
CKLBUIImage3D::setup(s32 order, float x, float y)
{
	if (!setupPropertyList((const char**)ms_propItems, SizeOfArray(ms_propItems))) {
		return false;
	}

	CKLBRenderingManager& renderingManager = CKLBRenderingManager::getInstance();
	(void)renderingManager;
	setInitPos(x, y);

	klb_assert(order >= 0, "Order Problem");
	m_order = order;

	m_renderNode = KLBNEW(CKLBNode);
	m_renderNode->setRenderSlotCount(10);
	m_renderNode->setRenderOnDestroy(true);
	getNode()->addNode(m_renderNode);
	m_renderNode->setTranslate(0.0f, 0.0f);
	m_renderNode->setPriority(order);
	return true;
}

void
CKLBUIImage3D::updateColor(CKLBDynSprite* sprite)
{
	int divisionWidth = m_divisionWidth;
	if (divisionWidth < 0) {
		return;
	}

	const u32 topLeft = m_colors[0];
	const u32 topRight = m_colors[1];
	const u32 topLeftAlphaChannel = topLeft >> 24;
	const u32 topLeftRedChannel = (topLeft >> 16) & 0xff;
	const u32 topLeftGreenChannel = (topLeft >> 8) & 0xff;
	const u32 topLeftBlueChannel = topLeft & 0xff;
	const float topLeftBlue = static_cast<float>(topLeftBlueChannel);
	const float topLeftGreen = static_cast<float>(topLeftGreenChannel);
	const float topLeftRed = static_cast<float>(topLeftRedChannel);
	const float topLeftAlpha = static_cast<float>(topLeftAlphaChannel);
	const u32 topRightAlphaChannel = topRight >> 24;
	const u32 topRightRedChannel = (topRight >> 16) & 0xff;
	const u32 topRightGreenChannel = (topRight >> 8) & 0xff;
	const u32 topRightBlueChannel = topRight & 0xff;
	const float topRightBlue = static_cast<float>(topRightBlueChannel);
	const float topRightGreen = static_cast<float>(topRightGreenChannel);
	const float topRightRed = static_cast<float>(topRightRedChannel);
	const float topRightAlpha = static_cast<float>(topRightAlphaChannel);

	const u32 bottomLeft = m_colors[2];
	const u32 bottomLeftAlphaChannel = bottomLeft >> 24;
	const u32 bottomLeftRedChannel = (bottomLeft >> 16) & 0xff;
	const u32 bottomLeftGreenChannel = (bottomLeft >> 8) & 0xff;
	const u32 bottomLeftBlueChannel = bottomLeft & 0xff;
	const float bottomLeftBlue = static_cast<float>(bottomLeftBlueChannel);
	const float bottomLeftGreen = static_cast<float>(bottomLeftGreenChannel);
	const float bottomLeftRed = static_cast<float>(bottomLeftRedChannel);
	const float bottomLeftAlpha = static_cast<float>(bottomLeftAlphaChannel);
	const u32 bottomRight = m_colors[3];
	const u32 bottomRightAlphaChannel = bottomRight >> 24;
	const u32 bottomRightRedChannel = (bottomRight >> 16) & 0xff;
	const u32 bottomRightGreenChannel = (bottomRight >> 8) & 0xff;
	const u32 bottomRightBlueChannel = bottomRight & 0xff;
	const float bottomRightBlue = static_cast<float>(bottomRightBlueChannel);
	const float bottomRightGreen = static_cast<float>(bottomRightGreenChannel);
	const float bottomRightRed = static_cast<float>(bottomRightRedChannel);
	const float bottomRightAlpha = static_cast<float>(bottomRightAlphaChannel);
	const float topBlueDelta = topRightBlue - topLeftBlue;
	const float bottomBlueDelta = bottomLeftBlue - bottomRightBlue;
	const float topGreenDelta = topRightGreen - topLeftGreen;
	const float bottomGreenDelta = bottomLeftGreen - bottomRightGreen;
	const float topRedDelta = topRightRed - topLeftRed;
	const float bottomRedDelta = bottomLeftRed - bottomRightRed;
	const float topAlphaDelta = topRightAlpha - topLeftAlpha;
	const float bottomAlphaDelta = bottomLeftAlpha - bottomRightAlpha;

	int divisionHeight = static_cast<int>(m_divisionHeight);
	int y = 0;
	int base = 0;
	bool continueRows;
	do {
		if (divisionHeight >= 0) {
			float yRatio = static_cast<float>(y) / static_cast<float>(divisionWidth);
			float topBlue = topBlueDelta * yRatio + topLeftBlue;
			float topGreen = topGreenDelta * yRatio + topLeftGreen;
			float topRed = topRedDelta * yRatio + topLeftRed;
			float topAlpha = topAlphaDelta * yRatio + topLeftAlpha;
			float bottomBlue = bottomBlueDelta * yRatio + bottomRightBlue;
			float bottomGreen = bottomGreenDelta * yRatio + bottomRightGreen;
			float bottomRed = bottomRedDelta * yRatio + bottomRightRed;
			float bottomAlpha = bottomAlphaDelta * yRatio + bottomRightAlpha;
			u32 x = 0;
			int column;
			do {
				column = static_cast<int>(x);
				float xRatio = static_cast<float>(column) / static_cast<float>(divisionHeight);
				float inverseX = 1.0f - xRatio;

				float interpolatedBlue =
					topBlue * inverseX + bottomBlue * xRatio;
				float interpolatedGreen =
					topGreen * inverseX + bottomGreen * xRatio;
				float interpolatedRed =
					topRed * inverseX + bottomRed * xRatio;
				float interpolatedAlpha =
					topAlpha * inverseX + bottomAlpha * xRatio;

				int blue = static_cast<int>(interpolatedBlue);
				int green = static_cast<int>(interpolatedGreen);
				int red = static_cast<int>(interpolatedRed);
				int alpha = static_cast<int>(interpolatedAlpha);

				sprite->setVertexColor(m_renderNode, base + column,
					((static_cast<u32>(alpha) & 0xff) << 24)
					| ((static_cast<u32>(red) & 0xff) << 16)
					| ((static_cast<u32>(green) & 0xff) << 8)
					| (static_cast<u32>(blue) & 0xff));
				x++;
				divisionHeight = static_cast<int>(m_divisionHeight);
			} while (column < divisionHeight);
			base += 1 + column;
			divisionWidth = m_divisionWidth;
		}
		continueRows = y < divisionWidth;
		y++;
	} while (continueRows);
}

bool
CKLBUIImage3D::buildMesh(int divisionWidth, u32 divisionHeight,
	int offsetX, int offsetY, int countX, int countY)
{
	const u32 indexCount = divisionWidth * divisionHeight * 6;
	CKLBDynSprite* sprite = m_dynSprite;
	CKLBImageAsset* asset = m_asset;
	sprite->setVICount(
		m_vertexCount,
		indexCount);

	const float* xy = asset->getXYBuffer();
	const float* uv = asset->getUVBuffer();
	SKLBRect* imageSize = asset->getSize();
	const int imageWidth = imageSize->getWidth();
	const int imageHeight = imageSize->getHeight();
	float duDx;
	float dvDx;
	float duDy;
	float dvDy;
	float baseU;
	float baseV;

	if (asset->hasStandardAttribute(CKLBImageAsset::IS_STANDARD_RECT)) {
		const float deltaX = xy[2] - xy[0];
		const float deltaY = xy[7] - xy[1];
		duDx = (uv[2] - uv[0]) / deltaX;
		dvDx = (uv[3] - uv[1]) / deltaX;
		duDy = (uv[6] - uv[0]) / deltaY;
		dvDy = (uv[7] - uv[1]) / deltaY;
		baseU = uv[0] - (xy[0] * duDx + xy[1] * duDy);
		baseV = uv[1] - (xy[0] * dvDx + xy[1] * dvDy);
	} else {
		/*
		 * Packed images can be rotated or mirrored in their texture atlas.
		 * Recover which local axis carries U and V from the extreme texture
		 * coordinates, then retain the corresponding sign.
		 */
		CKLBTextureAsset* texture = asset->getTexture();
		const float boundWidth = asset->m_boundWidth;
		const float boundHeight = asset->m_boundHeight;
		float minimumX = 9999.0f;
		float minimumY = 9999.0f;
		float minimumU = 9999.0f;
		float minimumV = 9999.0f;
		float maximumU = -9999.0f;
		float maximumV = -9999.0f;
		float uAtMinimumU = 9999.0f;
		float vAtMinimumV = 9999.0f;

		for (int index = 0; index < asset->getVertexCount(); ++index) {
			const float localU = uv[index * 2];
			const float pixelU =
				localU * static_cast<float>(texture->m_width);
			const float localV = uv[index * 2 + 1];
			const float pixelV =
				localV * static_cast<float>(texture->m_height);
			const float localX = xy[index * 2];
			const float localY = xy[index * 2 + 1];
			if (localX < minimumX) {
				minimumX = localX;
			}
			if (localY < minimumY) {
				minimumY = localY;
			}
			const bool newMinimumU = pixelU < minimumU;
			minimumU = newMinimumU ? pixelU : minimumU;
			if (newMinimumU) {
				uAtMinimumU = localU;
			}
			if (pixelU > maximumU) {
				maximumU = pixelU;
			}
			const bool newMinimumV = pixelV < minimumV;
			minimumV = newMinimumV ? pixelV : minimumV;
			if (newMinimumV) {
				vAtMinimumV = localV;
			}
			if (pixelV > maximumV) {
				maximumV = pixelV;
			}
		}

		const float uSpan = maximumU - minimumU;
		const float vSpan = maximumV - minimumV;
		bool transposed = uSpan > vSpan;
		transposed ^= boundWidth > boundHeight;
		transposed &= boundWidth != boundHeight;
		const float inverseTextureWidth =
			1.0f / static_cast<float>(texture->m_width);
		const float inverseTextureHeight =
			1.0f / static_cast<float>(texture->m_height);
		duDy = transposed ? inverseTextureWidth : 0.0f;
		dvDy = transposed ? 0.0f : inverseTextureHeight;
		duDx = transposed ? 0.0f : inverseTextureWidth;
		dvDx = transposed ? inverseTextureHeight : 0.0f;
		baseU = uAtMinimumU - (minimumX * duDx + minimumY * duDy);
		baseV = vAtMinimumV - (minimumX * dvDx + minimumY * dvDy);
	}

	const int availableWidth = imageWidth - offsetX;
	if (countX < 0) {
		countX = (availableWidth > 0) ? availableWidth : 1;
	} else {
		countX = (availableWidth <= countX)
			? availableWidth
			: countX;
		if (countX <= 0) {
			countX = 1;
		}
	}
	const int availableHeight = imageHeight - offsetY;
	if (countY < 0) {
		countY = (availableHeight > 0) ? availableHeight : 1;
	} else {
		countY = (availableHeight <= countY)
			? availableHeight
			: countY;
		if (countY <= 0) {
			countY = 1;
		}
	}

	switch (m_scaleMode) {
	case 0:
		m_scaleX = 1.0f;
		m_scaleY = 1.0f;
		break;
	case 1:
		m_scaleX = static_cast<float>(m_fixedWidth)
			/ static_cast<float>(countX);
		m_scaleY = m_scaleX;
		break;
	case 2:
		m_scaleY = static_cast<float>(m_fixedHeight)
			/ static_cast<float>(countY);
		m_scaleX = m_scaleY;
		break;
	case 3:
		m_scaleX = static_cast<float>(m_fixedWidth)
			/ static_cast<float>(countX);
		m_scaleY = static_cast<float>(m_fixedHeight)
			/ static_cast<float>(countY);
		break;
	}

	m_divisionOffsetX = offsetX;
	m_divisionOffsetY = offsetY;
	m_divisionCountX = countX;
	m_divisionCountY = countY;
	m_divisionWidth = divisionWidth;
	m_divisionHeight = divisionHeight;

	if (divisionWidth < 0) {
		updateColor(sprite);
	} else {
		const float stepX = static_cast<float>(countX)
			/ static_cast<float>(divisionWidth);
		const float stepY = static_cast<float>(countY)
			/ static_cast<float>(static_cast<int>(divisionHeight));
		const float imageOffsetX = static_cast<float>(offsetX);
		const float imageOffsetY = static_cast<float>(offsetY);
		const int rowStride = static_cast<int>(divisionHeight) + 1;
		int vertexBase = 0;
		for (int column = 0; column <= divisionWidth; ++column) {
			if (static_cast<int>(divisionHeight) >= 0) {
				const float imageX =
					static_cast<float>(column) * stepX
					+ imageOffsetX;
				const float localX = imageX - imageOffsetX;
				const float textureU = imageX * duDx + baseU;
				const float textureV = imageX * dvDx + baseV;
				for (int row = 0; row <= static_cast<int>(divisionHeight); ++row) {
					const float imageY =
						static_cast<float>(row) * stepY
						+ imageOffsetY;
					const int vertexIndex = vertexBase + row;
					SVertex& vertex = m_vertexBuffer[vertexIndex];
					const float localY = imageY - imageOffsetY;
					vertex.z = textureU + imageY * duDy;
					vertex.w = textureV + imageY * dvDy;
					vertex.u = localX + m_textureU;
					vertex.v = localY + m_textureV;
					sprite->setVertexUV(
						vertexIndex, vertex.z, vertex.w);
				}
				vertexBase += rowStride;
			}
		}

		updateColor(sprite);
		u16* indices = sprite->getSrcIndexBuffer();
		int indexBase = 0;
		for (int column = 0; column < divisionWidth; ++column) {
			for (int row = 0; row < static_cast<int>(divisionHeight); ++row) {
				const int topLeft = indexBase + row;
				*indices++ = static_cast<u16>(topLeft);
				const int bottomLeft = topLeft + rowStride;
				*indices++ = static_cast<u16>(bottomLeft);
				*indices++ = static_cast<u16>(topLeft + 1);
				*indices++ = static_cast<u16>(bottomLeft);
				*indices++ = static_cast<u16>(bottomLeft + 1);
				*indices++ = static_cast<u16>(topLeft + 1);
			}
			indexBase += rowStride;
		}
	}

	sprite->mark(0x0b);
	m_renderNode->markUpMatrix();
	return true;
}

int
CKLBUIImage3D::commandUI(CLuaState& lua, int argc, int cmd)
{
	bool result = false;
	switch (cmd)
	{
	case UI_IMAGE3D_SETCENTER:
		if (argc >= 4) {
			float x = lua.getFloat(3);
			float y = lua.getFloat(4);
			setPosition(x, y);
			REFRESH_A;
			result = true;
		}
		break;
	case UI_IMAGE3D_SETROTATION:
		if (argc >= 5) {
			float x = static_cast<float>(lua.getFloat(3) / 180.0 * 3.1415926);
			float y = static_cast<float>(lua.getFloat(4) / 180.0 * 3.1415926);
			float z = static_cast<float>(lua.getFloat(5) / 180.0 * 3.1415926);
			setRotation3D(x, y, z);
			REFRESH_A;
			result = true;
		}
		break;
	case UI_IMAGE3D_SETFOV:
		if (argc >= 3) {
			setDistance(lua.getFloat(3));
			REFRESH_A;
			result = true;
		}
		break;
	case UI_IMAGE3D_SETIMAGE:
		if (argc >= 7) {
			const char* assetName = lua.getString(3);
			float x = lua.getFloat(4);
			float y = lua.getFloat(5);
			int divisionWidth = lua.getInt(6);
			int divisionHeight = lua.getInt(7);
			releaseMesh();
			createMesh(assetName, x, y, divisionWidth, divisionHeight,
				0, 0, -1, -1);
			REFRESH_A;
			result = true;
		}
		break;
	case UI_IMAGE3D_SETSUBIMAGE:
		if (argc >= 11) {
			const char* assetName = lua.getString(3);
			float x = lua.getFloat(4);
			float y = lua.getFloat(5);
			int divisionWidth = lua.getInt(6);
			int divisionHeight = lua.getInt(7);
			int offsetX = lua.getInt(8);
			int offsetY = lua.getInt(9);
			int countX = lua.getInt(10);
			int countY = lua.getInt(11);
			releaseMesh();
			createMesh(assetName, x, y, divisionWidth, divisionHeight,
				offsetX, offsetY, countX, countY);
			REFRESH_A;
			result = true;
		}
		break;
	case UI_IMAGE3D_SETCOLORS:
		{
			bool colorsSet = false;
			if (argc >= 6) {
				m_colors[0] = static_cast<u32>(lua.getDoubleUnchecked(3));
				m_colors[1] = static_cast<u32>(lua.getDoubleUnchecked(4));
				m_colors[2] = static_cast<u32>(lua.getDoubleUnchecked(5));
				m_colors[3] = static_cast<u32>(lua.getDoubleUnchecked(6));
				bool refresh = true;
				if (argc >= 7) {
					refresh = lua.getBool(7);
				}
				colorsSet = true;
				CKLBDynSprite* sprite = m_dynSprite;
				if (sprite && refresh) {
					updateColor(sprite);
				}
			}
			lua.retBoolean(colorsSet);
			return 1;
		}
	case UI_IMAGE3D_SETSCALEWIDTH:
		if (argc >= 3) {
			m_fixedWidth = lua.getInt(3);
			m_scaleMode = 1;
		}
		lua.retBoolean(false);
		return 1;
	case UI_IMAGE3D_SETSCALEHEIGHT:
		if (argc >= 3) {
			m_fixedHeight = lua.getInt(3);
			m_scaleMode = 2;
		}
		lua.retBoolean(false);
		return 1;
	case UI_IMAGE3D_SETSCALEWIDTHHEIGHT:
		if (argc >= 3) {
			m_fixedWidth = lua.getInt(3);
			m_fixedHeight = lua.getInt(4);
			m_scaleMode = 3;
		}
		lua.retBoolean(false);
		return 1;
	case UI_IMAGE3D_RESETSCALE:
		m_scaleMode = 0;
		lua.retBoolean(true);
		return 1;
	case UI_IMAGE3D_SETCENTERMODE_X:
	case UI_IMAGE3D_SETCENTERMODE_Y:
		if (argc >= 3) {
			int mode = lua.getInt(3);
			if (cmd == UI_IMAGE3D_SETCENTERMODE_X) {
				m_centerModeX = mode;
			} else {
				m_centerModeY = mode;
			}
			REFRESH_A;
			result = true;
		}
		break;
	default:
		lua.retBoolean(false);
		return 1;
	}
	lua.retBoolean(result);
	return 1;
}

void
CKLBUIImage3D::dieUI()
{
	releaseMesh();
	if (m_renderNode) {
		delete m_renderNode;
	}
}

void
CKLBUIImage3D::notifyAssetUpdate(const char* sourceName, CKLBAsset* replacement)
{
	if (m_asset && m_asset->getTexture()->getFileSource()
	 && !strcmp(m_asset->getTexture()->getFileSource(), sourceName)) {
		buildMesh(m_divisionWidth, m_divisionHeight,
			m_divisionOffsetX, m_divisionOffsetY,
			m_divisionCountY, m_divisionCountX);
		m_asset = static_cast<CKLBImageAsset*>(replacement);
	}
}

void
CKLBUIImage3D::setPosition(float x, float y)
{
	m_centerX = x;
	m_centerY = y;
}

void
CKLBUIImage3D::setRotation3D(float x, float y, float z)
{
	m_rotation[0] = x;
	m_rotation[1] = y;
	m_rotation[2] = z;
}

void
CKLBUIImage3D::setDistance(float distance)
{
	m_fieldOfView = distance;
}

void
CKLBUIImage3D::executeProjection()
{
	makeMatrix(m_rotation, m_projectionMatrix);

	float centerX;
	switch (m_centerModeX) {
	case UI_IMAGE3D_ALIGN_LEFT:
		centerX = m_centerX;
		break;
	case UI_IMAGE3D_ALIGN_CENTER:
		centerX = static_cast<float>(m_divisionCountX * 0.5 + m_centerX);
		break;
	case UI_IMAGE3D_ALIGN_RIGHT:
		centerX = static_cast<float>(m_divisionCountX) - m_centerX;
		break;
	default:
		centerX = 0.0f;
		break;
	}

	float centerY;
	switch (m_centerModeY) {
	case UI_IMAGE3D_ALIGN_LEFT:
		centerY = m_centerY;
		break;
	case UI_IMAGE3D_ALIGN_CENTER:
		centerY = static_cast<float>(m_divisionCountY * 0.5 + m_centerY);
		break;
	case UI_IMAGE3D_ALIGN_RIGHT:
		centerY = static_cast<float>(m_divisionCountY) - m_centerY;
		break;
	default:
		centerY = 0.0f;
		break;
	}

	CKLBDynSprite* sprite = m_dynSprite;
	if (!sprite) {
		return;
	}

	for (int index = 0; index < static_cast<int>(m_vertexCount); index++) {
		SVertex& vertex = m_vertexBuffer[index];
		float localX = vertex.u * m_scaleX - centerX;
		float localY = vertex.v * m_scaleY - centerY;
		float projectedX;
		float projectedY;
		projectPoint(&projectedX, &projectedY, m_projectionMatrix,
			localX, localY, m_fieldOfView);
		vertex.x = projectedX;
		vertex.y = projectedY;
		sprite->setVertexXY(index, projectedX + centerX, projectedY + centerY);
	}

	sprite->mark(0x0b);
}

void
CKLBUIImage3D::projectPoint(float* projectedX, float* projectedY,
	const float* matrix, float x, float y, float distance)
{
	float numeratorX = matrix[0] * x + matrix[1] * y;
	float numeratorY = matrix[4] * x + matrix[5] * y;
	float depth = matrix[8] * x + matrix[9] * y;
	float denominator = distance - depth;
	float resultX = (numeratorX * distance) / denominator;
	float resultY = (numeratorY * distance) / denominator;
	*projectedX = resultX;
	*projectedY = resultY;
}

void
CKLBUIImage3D::matMul(const float* left, const float* right, float* result)
{
	float x = left[0];
	float y = left[1];
	float z = left[2];
	float w = left[3];
	result[0] = x * right[0] + y * right[4] + z * right[8]  + w * right[12];
	result[1] = x * right[1] + y * right[5] + z * right[9]  + w * right[13];
	result[2] = x * right[2] + y * right[6] + z * right[10] + w * right[14];
	result[3] = x * right[3] + y * right[7] + z * right[11] + w * right[15];

	x = left[4];
	y = left[5];
	z = left[6];
	w = left[7];
	result[4] = x * right[0] + y * right[4] + z * right[8]  + w * right[12];
	result[5] = x * right[1] + y * right[5] + z * right[9]  + w * right[13];
	result[6] = x * right[2] + y * right[6] + z * right[10] + w * right[14];
	result[7] = x * right[3] + y * right[7] + z * right[11] + w * right[15];

	x = left[8];
	y = left[9];
	z = left[10];
	w = left[11];
	result[8]  = x * right[0] + y * right[4] + z * right[8]  + w * right[12];
	result[9]  = x * right[1] + y * right[5] + z * right[9]  + w * right[13];
	result[10] = x * right[2] + y * right[6] + z * right[10] + w * right[14];
	result[11] = x * right[3] + y * right[7] + z * right[11] + w * right[15];

	x = left[12];
	y = left[13];
	z = left[14];
	w = left[15];
	result[12] = x * right[0] + y * right[4] + z * right[8]  + w * right[12];
	result[13] = x * right[1] + y * right[5] + z * right[9]  + w * right[13];
	result[14] = x * right[2] + y * right[6] + z * right[10] + w * right[14];
	result[15] = x * right[3] + y * right[7] + z * right[11] + w * right[15];
}

void
CKLBUIImage3D::makeMatrix(const float* rotation, float* matrix)
{
	float cosineX = cosf(rotation[0]);
	float sineX = sinf(rotation[0]);
	float cosineY = cosf(rotation[1]);
	float sineY = sinf(rotation[1]);
	float cosineZ = cosf(rotation[2]);
	float sineZ = sinf(rotation[2]);

	float negativeSineX = -sineX;
	float rotateX[4][4] = {
		{ 1.0f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, cosineX, negativeSineX, 0.0f },
		{ 0.0f, sineX, cosineX, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 1.0f }
	};
	float negativeSineY = -sineY;
	float rotateY[4][4] = {
		{ cosineY, 0.0f, sineY, 0.0f },
		{ 0.0f, 1.0f, 0.0f, 0.0f },
		{ negativeSineY, 0.0f, cosineY, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 1.0f }
	};
	float negativeSineZ = -sineZ;
	float rotateZ[4][4] = {
		{ cosineZ, negativeSineZ, 0.0f, 0.0f },
		{ sineZ, cosineZ, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 1.0f }
	};

	struct MatrixElements {
		float m00, m01, m02, m03;
		float m10, m11, m12, m13;
		float m20, m21, m22, m23;
		float m30, m31, m32, m33;
	};
	union Matrix4 {
		float values[16];
		MatrixElements elements;
	};
	Matrix4 combined;
	float temporary[16];
	for (int index = 0; index < 15; index++) {
		temporary[index] = 0.0f;
	}
	temporary[0] = 1.0f;
	temporary[5] = 1.0f;
	temporary[10] = 1.0f;
	temporary[15] = 1.0f;
	matMul(&rotateX[0][0], &rotateY[0][0], temporary);
	for (int index = 0; index < 15; index++) {
		combined.values[index] = 0.0f;
	}
	combined.values[0] = 1.0f;
	combined.values[5] = 1.0f;
	combined.values[10] = 1.0f;
	combined.values[15] = 1.0f;
	matMul(temporary, &rotateZ[0][0], combined.values);

	matrix[0] = combined.elements.m00;
	matrix[1] = combined.elements.m01;
	matrix[2] = combined.elements.m02;
	matrix[3] = 0.0f;
	matrix[4] = combined.elements.m10;
	matrix[5] = combined.elements.m11;
	matrix[6] = combined.elements.m12;
	matrix[7] = 0.0f;
	matrix[8] = combined.elements.m20;
	matrix[9] = combined.elements.m21;
	matrix[10] = combined.elements.m22;
	matrix[11] = 0.0f;
	matrix[12] = 0.0f;
	matrix[13] = 0.0f;
	matrix[14] = 0.0f;
	matrix[15] = 1.0f;
}
