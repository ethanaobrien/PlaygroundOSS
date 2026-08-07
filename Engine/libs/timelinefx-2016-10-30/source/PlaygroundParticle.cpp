#include "PlaygroundParticle.h"
#include "TLFXPugiXMLLoader.h"
#include "TextureManagement.h"
#include "RenderingFramework.h"
#include "CKLBNode.h"
#include "CKLBUIParticle.h"
#include "CPFInterface.h"
#include "CKLBUtility.h"
#include "TLFXEffect.h"
#include "unzip.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <strings.h>

// Particle atlases use the target's embedded MaxRects implementation. Keep
// this in the particle translation unit: the shipped binary emits the packer
// immediately alongside the particle archive and asset-loading helpers.
#include "RectangleBinPack/Rect.cpp"
#include "RectangleBinPack/MaxRectsBinPack.cpp"

typedef char KLBImageSizeCheck[(sizeof(KLBImage) == 0xe8) ? 1 : -1];

static const SVertexEntry particlePositionUVFormat[] = {
    { 1, 0, false, VEC2 | VERTEX },
    { 2, 0, false, VEC2 | TEXTURE },
    { 0, 0, false, END_LIST }
};

static const SVertexEntry particleColorFormat[] = {
    { 3, 0, false, VEC4BYTE | COLOR },
    { 0, 0, false, END_LIST }
};

struct KLBParticleBatch {
    SRenderState*   state;
    s32             vertexCount;
    CTextureUsage*  textures[2];
    s32             uniformIDs[2];
    CBuffer*        buffers[2];
};

struct KLBParticleArchiveStream {
    const u8* data;
    u32       size;
    u32       position;
};

struct KLBParticleDecodedImage {
    typedef s32 (*AllocateCallback)(KLBParticleDecodedImage*);

    struct Destination {
        KLBImage* image;
        u8*       pixels;
    };

    Destination* destination;
    AllocateCallback allocate;
    u8*          pixels;
    u32          byteCount;
    u32          width;
    u32          height;
    u32          pixelBytes;

    static s32 allocatePixels(KLBParticleDecodedImage* image);
};

s32
KLBParticleDecodedImage::allocatePixels(KLBParticleDecodedImage* image)
{
    u32 size = image->width * image->height * image->pixelBytes;
    size = (size + 3) & ~3U;
    image->pixels = new u8[size];
    image->byteCount = size;
    image->destination->pixels = image->pixels;
    return 1;
}

void*
particleArchiveOpen(void* stream)
{
    return stream;
}

size_t
particleArchiveRead(void*, KLBParticleArchiveStream* stream, void* buffer, size_t byteCount)
{
    u32 end = stream->position + static_cast<u32>(byteCount);
    if (end > stream->size) {
        end = stream->size;
    }

    u32 readSize = end - stream->position;
    if (readSize) {
        std::memcpy(buffer, stream->data + stream->position, readSize);
    }
    stream->position = end;
    return readSize;
}

s32
particleArchiveWrite(void*, void*, const void*, size_t)
{
    return 0;
}

s32
particleArchiveClose(void*, void*)
{
    return 0;
}

long
particleArchiveTell(void*, KLBParticleArchiveStream* stream)
{
    return stream->position;
}

s32
particleArchiveSeek(void*, KLBParticleArchiveStream* stream, s32 offset, s32 origin)
{
    if (origin == 1) {
        stream->position += offset;
    } else if (origin == 0) {
        stream->position = offset;
    } else if (origin == 2) {
        stream->position = stream->size - offset;
    } else {
        klb_assertAlways("Unsupported archive seek origin");
    }
    return 0;
}

s32
particleArchiveError(void*, void*)
{
    return 0;
}

KLBImage::~KLBImage()
{
    delete [] m_frameUV;

    if (m_imageAsset) {
        m_imageAsset->decrementRefCount();
    } else {
        CKLBOGLWrapper& renderer = CKLBOGLWrapper::getInstance();

        for (u32 index = 0; index < 2; ++index) {
            CTextureUsage* usage = m_textureUsage[index];
            if (usage) {
                CTexture* texture = static_cast<CTexture*>(usage->pTexture);
                if (usage->releaseReference()) {
                    texture->releaseUsage(usage);
                    renderer.releaseTexture(texture);
                }
            }
        }
    }
}

void
KLBImage::FindRadius()
{
    _maxRadius = std::max(_height, _width) * 0.5f;
}

bool
KLBImage::Load(const char* filename)
{
    // The task supplies replacement pairs as a flat {from, to} array, so the
    // effect's own image names can be redirected without touching the archive.
    CKLBParticleAssetPlugin* plugin = m_assetPlugin;
    s32 aliasCount = plugin->m_imageAliasCount;
    const char** aliases = plugin->m_imageAliases;
    const char* name = filename;
    for (s32 alias = 0; alias < aliasCount; ++alias) {
        if (strcasecmp(name, aliases[alias * 2]) == 0) {
            name = aliases[alias * 2 + 1];
            break;
        }
    }

    bool hasExtension = CKLBUtility::endsWith(name, strlen(name), ".imag", 5);
    if (m_assetPlugin->loadImageFromArchive(name)) {
        return m_assetPlugin->retainLoadedImage(this);
    }

    char assetName[512];
    sprintf(assetName, "%s%s", name, hasExtension ? "" : ".imag");

    CKLBAssetManager& assetManager = CKLBAssetManager::getInstance();
    CKLBAbstractAsset* asset =
        assetManager.getAsset(assetManager.getAssetIDFromName(assetName, 'I'));
    klb_assert(asset, "Asset image %s in Particle animation can not be found.", assetName);

    if (asset->getAssetType() == ASSET_TEXTURE) {
        CKLBImageAsset* image =
            static_cast<CKLBTextureAsset*>(asset)->getImage(assetName);
        if (image) {
            image->getTexture()->incrementRefCount();
            m_imageAsset = image->getTexture();
            m_imageMode = (image->m_usageType >> 4) & 3;

            const float* imageUV = image->getUVBuffer();
            for (u32 component = 0; component < 8; ++component) {
                m_uv[0][component] = imageUV[component];
            }

            SKLBRect rect = *image->getSize();
            buildFrameUV(rect.getWidth(), rect.getHeight());
            m_standardRect = (image->m_usageType >> 4) & 1;
            m_textureUsage[0] = m_imageAsset->m_pTextureUsage;
            return true;
        }
    }

    asset->incrementRefCount();
    asset->decrementRefCount();
    return false;
}

KLBInnerParticleAsset::~KLBInnerParticleAsset()
{
}

TLFX::XMLLoader*
KLBInnerParticleAsset::CreateLoader() const
{
    return new TLFX::PugiXMLLoader(0);
}

TLFX::AnimImage*
KLBInnerParticleAsset::CreateImage() const
{
    KLBImage* image = new KLBImage();
    image->m_assetPlugin = m_plugin;
    return image;
}

void
KLBImage::buildFrameUV(s32 textureWidth, s32 textureHeight)
{
    const u32 frameCount = _frames;
    if (frameCount < 2) {
        return;
    }

    const s32 frameWidth = static_cast<s32>(_width);
    const s32 frameHeight = static_cast<s32>(_height);
    m_frameUV = new float[frameCount * 16];
    const float textureWidthFloat = static_cast<float>(textureWidth);
    const float textureHeightFloat = static_cast<float>(textureHeight);
    const float frameWidthFloat = static_cast<float>(frameWidth);
    const float frameHeightFloat = static_cast<float>(frameHeight);

    const float horizontalU =
        (m_uv[0][2] - m_uv[0][0]) / textureWidthFloat;
    const float horizontalV =
        (m_uv[0][3] - m_uv[0][1]) / textureWidthFloat;
    const float verticalU =
        (m_uv[0][6] - m_uv[0][0]) / textureHeightFloat;
    const float verticalV =
        (m_uv[0][7] - m_uv[0][1]) / textureHeightFloat;
    const float frameHorizontalU = frameWidthFloat * horizontalU;
    const float frameHorizontalV = frameWidthFloat * horizontalV;
    const float frameVerticalU = frameHeightFloat * verticalU;
    const float frameVerticalV = frameHeightFloat * verticalV;

    s32 pixelX = 0;
    s32 pixelY = 0;
    float* frameUV = m_frameUV;
    for (s32 frame = 0; frame < _frames; ++frame) {
        const float left =
            m_uv[0][0] + pixelX * horizontalU + pixelY * verticalU;
        const float top =
            m_uv[0][1] + pixelX * horizontalV + pixelY * verticalV;
        frameUV[0] = left;
        frameUV[1] = top;

        const float right = left + frameHorizontalU;
        frameUV[2] = right;
        const float bottom = top + frameHorizontalV;
        frameUV[3] = bottom;
        const float lowerRightU = right + frameVerticalU;
        const float lowerRightV = bottom + frameVerticalV;
        const float lowerLeftU = left + frameVerticalU;
        const float lowerLeftV = top + frameVerticalV;
        float* nextFrameUV = frameUV + 8;
        frameUV[4] = lowerRightU;
        frameUV[5] = lowerRightV;
        frameUV[6] = lowerLeftU;
        frameUV[7] = lowerLeftV;

        pixelX += frameWidth;
        if (pixelX >= textureWidth) {
            pixelX = 0;
            pixelY += frameHeight;
        }
        frameUV = nextFrameUV;
    }

    const float maskHorizontalU =
        (m_uv[1][2] - m_uv[1][0]) / textureWidthFloat;
    const float maskHorizontalV =
        (m_uv[1][3] - m_uv[1][1]) / textureWidthFloat;
    const float maskVerticalU =
        (m_uv[1][6] - m_uv[1][0]) / textureHeightFloat;
    const float maskVerticalV =
        (m_uv[1][7] - m_uv[1][1]) / textureHeightFloat;
    const float maskFrameHorizontalU = frameWidthFloat * maskHorizontalU;
    const float maskFrameHorizontalV = frameWidthFloat * maskHorizontalV;
    const float maskFrameVerticalU = frameHeightFloat * maskVerticalU;
    const float maskFrameVerticalV = frameHeightFloat * maskVerticalV;

    pixelX = 0;
    pixelY = 0;
    for (s32 frame = 0; frame < _frames; ++frame) {
        const float left =
            m_uv[1][0] + pixelX * maskHorizontalU + pixelY * maskVerticalU;
        const float top =
            m_uv[1][1] + pixelX * maskHorizontalV + pixelY * maskVerticalV;
        frameUV[0] = left;
        frameUV[1] = top;

        const float right = left + maskFrameHorizontalU;
        frameUV[2] = right;
        const float bottom = top + maskFrameHorizontalV;
        frameUV[3] = bottom;
        frameUV[4] = right + maskFrameVerticalU;
        frameUV[5] = bottom + maskFrameVerticalV;
        frameUV[6] = left + maskFrameVerticalU;
        frameUV[7] = top + maskFrameVerticalV;
        float* nextFrameUV = frameUV + 8;

        pixelX += frameWidth;
        if (pixelX >= textureWidth) {
            pixelX = 0;
            pixelY += frameHeight;
        }
        frameUV = nextFrameUV;
    }
}

bool
CKLBParticleAssetPlugin::retainLoadedImage(KLBImage* image)
{
    s32 index = m_loadedImageCount;
    if (index < 32) {
        m_currentImage.image = image;
        m_loadedImageCount = index + 1;
        m_loadedImages[index] = m_currentImage;
    } else {
        delete [] m_currentImage.pixels;
    }
    return index < 32;
}

// Copies one decoded image plane into the shared atlas surface and writes the
// plane's UV rectangle back into the image. Blend mode 2 (and every explicit
// second plane) addresses the image's masked UV set.
void
CKLBParticleAssetPlugin::blitImagePlane(ImageLoadRecord* record, bool premultiplied, s32 plane)
{
    bool maskPlane = true;
    if (plane != 1) {
        maskPlane = (record->image->_blendModes == 2);
    }

    s32 pixelCount = record->width * record->height;
    if (pixelCount && plane == 0 && premultiplied) {
        u8 (*texel)[4] = (u8 (*)[4])record->pixels;
        s32 pixel = 0;
        do {
            u32 alpha = texel[pixel][3];
            u32 scale = alpha + (alpha >> 7);
            texel[pixel][0] = (u8)((texel[pixel][0] * scale) >> 8);
            texel[pixel][1] = (u8)((texel[pixel][1] * scale) >> 8);
            texel[pixel][2] = (u8)((texel[pixel][2] * scale) >> 8);
        } while (++pixel < pixelCount);
    }

    if (pixelCount && maskPlane && premultiplied) {
        u8 (*texel)[4] = (u8 (*)[4])record->pixels;
        s32 pixel = 0;
        do {
            texel[pixel][3] = 0;
        } while (++pixel < pixelCount);
    }

    KLBImage* image = record->image;
    image->m_standardRect = premultiplied;

    const u8* pixels = record->pixels;
    s32 left = (s16)record->x[plane];
    s32 top = (s16)record->y[plane];
    u32 atlasWidth = m_atlasWidth;
    u32 atlasHeight = m_atlasHeight;
    u8* destination = m_atlasPixels + (top * atlasWidth + left) * 4;
    u32 width = record->width;
    u64 sourceStride = record->channelCount * (u64)width;
    float horizontalScale = 1.0f / (float)atlasWidth;
    float verticalScale = 1.0f / (float)atlasHeight;
    s32 uvSet = maskPlane ? 1 : 0;
    float u0 = left * horizontalScale;
    float v0 = top * verticalScale;
    image->m_uv[uvSet][0] = u0;
    image->m_uv[uvSet][1] = v0;

    s32 destinationStride = atlasWidth * 4;
    if (record->rotated[plane]) {
        for (s64 row = 0; row < record->height; ++row) {
            const u8* source = pixels + row * sourceStride;
            const u8* sourceEnd = source + sourceStride;
            u8* column = destination;
            while (source < sourceEnd) {
                column[0] = source[0];
                column[1] = source[1];
                column[2] = source[2];
                column[3] = source[3];
                source += 4;
                column += destinationStride;
            }
            destination += 4;
        }
        image->m_uv[uvSet][2] = u0;
        image->m_uv[uvSet][3] = v0 + width * verticalScale;
        image->m_uv[uvSet][6] = u0 + record->height * horizontalScale;
        image->m_uv[uvSet][7] = v0;
        image->m_uv[uvSet][4] = image->m_uv[uvSet][6];
        image->m_uv[uvSet][5] = image->m_uv[uvSet][3];
    } else {
        for (s64 row = 0; row < record->height; ++row) {
            const u8* source = pixels + row * sourceStride;
            const u8* sourceEnd = source + sourceStride;
            u8* line = destination;
            while (source < sourceEnd) {
                line[0] = source[0];
                line[1] = source[1];
                line[2] = source[2];
                line[3] = source[3];
                source += 4;
                line += 4;
            }
            destination += destinationStride;
        }
        image->m_uv[uvSet][2] = u0 + width * horizontalScale;
        image->m_uv[uvSet][3] = v0;
        image->m_uv[uvSet][6] = u0;
        image->m_uv[uvSet][7] = v0 + record->height * verticalScale;
        image->m_uv[uvSet][4] = image->m_uv[uvSet][2];
        image->m_uv[uvSet][5] = image->m_uv[uvSet][7];
    }
}

// Every image the archive produced is packed into one square-ish atlas. Images
// that do not fit start a new atlas page; the surface is uploaded once per page
// and the decoded source pixels are released as soon as they are resident.
bool
CKLBParticleAssetPlugin::packLoadedImages()
{
    if (!m_loadedImageCount) {
        return true;
    }

    s32 area = 0;
    for (s64 index = 0; index < m_loadedImageCount; ++index) {
        ImageLoadRecord& record = m_loadedImages[index];
        bool twoPlanes = (record.image->_blendModes == 3);
        record.planeCount = 0;
        area += (record.width * record.height) << (twoPlanes ? 1 : 0);
    }

    s32 atlasWidth = CKLBUtility::nearest2Pow((u32)sqrtf((float)area));
    s32 atlasHeight = atlasWidth >> 1;
    if (atlasHeight * atlasWidth <= area) {
        atlasHeight = atlasWidth;
    }
    if (atlasWidth > 2048) {
        atlasWidth = 2048;
    }
    if (atlasHeight > 2048) {
        atlasHeight = 2048;
    }

    rbp::MaxRectsBinPack packer;
    s64 page = 0;
    packer.Init(atlasWidth, atlasHeight, true);
    bool packed = true;
    s32 restartIndex = -1;
    for (s64 index = 0; packed && index < m_loadedImageCount; ++index) {
        ImageLoadRecord& record = m_loadedImages[index];
        bool secondPlane = false;
        while (packed) {
            rbp::Rect placed = packer.Insert(
                record.width, record.height,
                rbp::MaxRectsBinPack::RectBestShortSideFit);
            if (placed.height <= 0) {
                if (restartIndex == index) {
                    packed = false;
                    continue;
                }
                packer.Init(atlasWidth, atlasHeight, true);
                ++page;
                restartIndex = index;
                continue;
            }

            u8 plane = record.planeCount;
            record.x[plane] = (u16)placed.x;
            record.y[plane] = (u16)placed.y;
            record.rotated[plane] = (placed.width != record.width);
            record.page[plane] = (u8)page;
            record.planeCount = plane + 1;

            if (secondPlane || record.image->_blendModes < 3) {
                break;
            }
            secondPlane = (record.image->_blendModes == 3);
        }
    }

    if (!packed) {
        return false;
    }

    CKLBOGLWrapper& renderer = CKLBOGLWrapper::getInstance();
    s32 byteCount = atlasWidth * atlasHeight * 4;
    u8* atlas = new u8[byteCount];
    memset(atlas, 0, byteCount);
    m_atlasWidth = atlasWidth;
    m_atlasHeight = atlasHeight;
    m_atlasPixels = atlas;

    CTexture* texture = 0;
    CTextureUsage* usage = 0;
    s32 currentPage = -1;
    for (s32 index = 0; index < m_loadedImageCount; ++index) {
        ImageLoadRecord& record = m_loadedImages[index];
        for (s32 plane = 0; plane < record.planeCount; ++plane) {
            if (record.page[plane] != currentPage) {
                currentPage = record.page[plane];
                if (texture) {
                    texture->updateTexture(0, 0, atlasWidth, atlasHeight, atlas, byteCount);
                }
                texture = renderer.createTexture(
                    atlasWidth, atlasHeight, GL_UNSIGNED_BYTE,
                    CKLBOGLWrapper::RGBA, NULL, 0, CKLBOGLWrapper::TEX_NONE, 0, NULL);
                if (!texture) {
                    packed = false;
                    break;
                }
                usage = texture->createUsage();
                if (!usage) {
                    packed = false;
                    texture = 0;
                    break;
                }
                usage->referenceCount = 0;
                usage->setSampling(CTextureUsage::LINEAR, CTextureUsage::LINEAR);
                usage->setWrapping(CTextureUsage::CLAMP_TO_EDGE, CTextureUsage::CLAMP_TO_EDGE);
            }
            record.image->m_textureUsage[plane] = usage;
            usage->referenceCount++;
            blitImagePlane(&record, true, plane);
        }
        if (!texture) {
            break;
        }
    }

    for (s32 index = 0; index < m_loadedImageCount; ++index) {
        delete [] m_loadedImages[index].pixels;
    }
    if (texture) {
        texture->updateTexture(0, 0, atlasWidth, atlasHeight, atlas, byteCount);
    }
    for (s32 index = 0; index < m_loadedImageCount; ++index) {
        ImageLoadRecord& record = m_loadedImages[index];
        record.image->buildFrameUV(record.width, record.height);
    }
    delete [] atlas;
    return packed;
}

// Images referenced by an effect are normally stored inside the effect archive
// itself. The archive is re-opened over the plugin's own stream cursor for every
// requested name, decoded straight into the pending load record, and expanded to
// RGBA when the decoder reports three channels.
bool
CKLBParticleAssetPlugin::loadImageFromArchive(const char* name)
{
    zlib_filefunc_def archiveFunctions;
    archiveFunctions.opaque      = reinterpret_cast<KLBParticleArchiveStream*>(&m_stream);
    archiveFunctions.zopen_file  = (open_file_func)particleArchiveOpen;
    archiveFunctions.zclose_file = (close_file_func)particleArchiveClose;
    archiveFunctions.zerror_file = (testerror_file_func)particleArchiveError;
    archiveFunctions.zseek_file  = (seek_file_func)particleArchiveSeek;
    archiveFunctions.ztell_file  = (tell_file_func)particleArchiveTell;
    archiveFunctions.zread_file  = (read_file_func)particleArchiveRead;
    archiveFunctions.zwrite_file = (write_file_func)particleArchiveWrite;
    m_streamPosition = 0;

    bool loaded = false;
    unzFile archiveFile = unzOpen2(NULL, &archiveFunctions);
    if (archiveFile) {
        if (unzGoToFirstFile(archiveFile) == UNZ_OK) {
            bool found = false;
            while (!found) {
                unz_file_info entry;
                char entryName[260];
                if (unzGetCurrentFileInfo(archiveFile, &entry, entryName,
                                          sizeof(entryName), NULL, 0, NULL, 0) != UNZ_OK) {
                    break;
                }
                if (strcasecmp(entryName, name) == 0) {
                    u32 size = entry.uncompressed_size;
                    u8* buffer = new u8[size];
                    if (unzOpenCurrentFile(archiveFile) == UNZ_OK) {
                        if (unzReadCurrentFile(archiveFile, buffer, size) == (s32)size) {
                            u32 channelCount;
                            KLBParticleDecodedImage decoded;
                            decoded.destination = 0;
                            decoded.allocate = 0;
                            decoded.pixels = 0;
                            decoded.byteCount = 0;
                            decoded.destination =
                                (KLBParticleDecodedImage::Destination*)&m_currentImage;
                            decoded.allocate = KLBParticleDecodedImage::allocatePixels;

                            loaded = decodeTextureImage(
                                buffer, size, 0, 0,
                                m_currentImage.pixels, 0,
                                &channelCount,
                                (TextureDecodeTarget*)&decoded);
                            if (loaded) {
                                if (channelCount == 3) {
                                    s32 pixelCount = decoded.width * decoded.height;
                                    u8* rgba = new u8[pixelCount * 4];
                                    channelCount = 4;
                                    u8* destination = rgba;
                                    const u8* source = m_currentImage.pixels;
                                    for (s32 pixel = 0; pixel < pixelCount; ++pixel) {
                                        *destination++ = *source++;
                                        *destination++ = *source++;
                                        *destination++ = *source++;
                                        *destination++ = 0xff;
                                    }
                                    delete [] m_currentImage.pixels;
                                    m_currentImage.pixels = rgba;
                                }
                                m_currentImage.width = decoded.width;
                                m_currentImage.height = decoded.height;
                                m_currentImage.channelCount = channelCount;
                                m_currentImage.image = 0;
                            }
                            found = true;
                        }
                        unzCloseCurrentFile(archiveFile);
                    }
                    delete [] buffer;
                }
                if (unzGoToNextFile(archiveFile) != UNZ_OK) {
                    break;
                }
            }
        }
        unzClose(archiveFile);
    }
    return loaded;
}

KLBParticleMovie::KLBParticleMovie(s32 particleCount)
: TLFX::ParticleManager(particleCount, 1)
, m_particleTextures(0)
, m_vertexCount(0)
, m_currentImage(0)
, m_indexBuffer(0)
, m_positionBuffer(0)
, m_uvColorBuffer(0)
, m_renderCallback()
, m_particleCapacity(particleCount)
{
    m_renderCallback.context = this;
    m_renderCallback.callback = renderCallback;
}

KLBParticleMovie::~KLBParticleMovie()
{
    delete [] m_particleTextures;

    CKLBOGLWrapper& renderer = CKLBOGLWrapper::getInstance();
    if (m_indexBuffer) {
        renderer.releaseIndexBuffer(m_indexBuffer);
    }
    if (m_positionBuffer) {
        renderer.releaseVertexBuffer(m_positionBuffer);
    }
    if (m_uvColorBuffer) {
        renderer.releaseVertexBuffer(m_uvColorBuffer);
    }
}

bool
KLBParticleMovie::setup(CKLBNode* node, u32 renderOrder)
{
    m_particleTextures = new CTextureUsage*[m_particleCapacity];

    CKLBRenderingManager& renderingManager = CKLBRenderingManager::getInstance();
    CKLBRenderState* renderState = renderingManager.allocateCommandState();
    if (renderState) {
        renderState->changeOrder(renderingManager, renderOrder);
        renderState->setStateCallback(&m_renderCallback);
    }
    node->setRender(renderState);
    m_renderNode = node;

    CKLBOGLWrapper& renderer = CKLBOGLWrapper::getInstance();
    s32 particleCount = m_particleCapacity;
    s32 indexCount = particleCount * 6;
    m_indexBuffer = renderer.createIndexBuffer(indexCount, false);
    if (m_indexBuffer) {
        u16* indices = reinterpret_cast<u16*>(m_indexBuffer->updateStart(0));
        for (s32 particle = 0; particle < particleCount; ++particle) {
            u16 vertex = static_cast<u16>(particle * 4);
            *indices++ = vertex;
            *indices++ = vertex + 1;
            *indices++ = vertex + 3;
            *indices++ = vertex + 1;
            *indices++ = vertex + 2;
            *indices++ = vertex + 3;
        }
        m_indexBuffer->updateComplete(indexCount);
        m_indexBuffer->setDrawOffset(0);
    }

    s32 vertexCount = particleCount * 4;
    m_positionBuffer = renderer.createVertexBuffer(vertexCount, particlePositionUVFormat);
    m_uvColorBuffer = renderer.createVertexBuffer(vertexCount, particleColorFormat);

    return renderState
        && m_particleTextures
        && m_indexBuffer
        && m_positionBuffer
        && m_uvColorBuffer;
}

void
KLBParticleMovie::finishParticleBatch()
{
}

void
KLBParticleMovie::beginParticleBatch()
{
    m_particleSprite->mark(
        CKLBDynSprite::MARK_CHANGE_XY
        | CKLBDynSprite::MARK_CHANGE_UV
        | FLAG_BUFFERSHIFT
        | FLAG_COLORUPDATE
    );
    m_uvCursor = m_particleSprite->getSrcUVBuffer();
    m_positionCursor = m_particleSprite->getSrcXYBuffer();
    m_indexCursor = m_particleSprite->getSrcIndexBuffer();
    m_colorCursor = m_particleSprite->getLocalColorBuffer();
    m_vertexCount = 0;
    m_currentTexture = 0;
}

void
KLBParticleMovie::DrawSprite(
    TLFX::AnimImage* sprite,
    float px, float py,
    float frame,
    float x, float y,
    float rotation,
    float scaleX, float scaleY,
    unsigned char r, unsigned char g, unsigned char b,
    float a,
    bool additive)
{
    if (m_vertexCount >= m_particleSprite->getMaxIndexCount()) {
        return;
    }

    KLBImage* image = static_cast<KLBImage*>(sprite);
    u32 alpha = static_cast<u32>(a * 255.0);
    if (image->m_standardRect) {
        s32 alphaScale = static_cast<s32>(alpha)
                       + (static_cast<s32>(alpha) >> 7);
        r = static_cast<unsigned char>((r * alphaScale) >> 8);
        g = static_cast<unsigned char>((g * alphaScale) >> 8);
        b = static_cast<unsigned char>((b * alphaScale) >> 8);
        alpha = additive ? 0 : alpha;
    }

    u32 color = (static_cast<u32>(alpha) << 24)
              | (static_cast<u32>(b) << 16)
              | (static_cast<u32>(g) << 8)
              | r;
    for (u32 vertex = 0; vertex < 4; ++vertex) {
        m_colorCursor[vertex] = color;
    }

    const float* imageUV;
    if (image->_frames == 1) {
        imageUV = image->m_uv[additive ? 1 : 0];
    } else {
        s32 frameIndex = static_cast<s32>(frame);
        if (additive) {
            frameIndex += image->_frames;
        }
        imageUV = image->m_frameUV + frameIndex * 8;
    }
    float firstUV = imageUV[0];
    float* destinationUV = m_uvCursor;
    destinationUV[0] = firstUV;
    for (u32 component = 1; component < 8; ++component) {
        destinationUV[component] = imageUV[component];
    }

    float top = -y * scaleY;
    float bottom = (image->_height - y) * scaleY;
    float left = x * scaleX;
    float right = (image->_width - x) * scaleX;
    float angle = rotation * 0.01745329251994329577f;
    float cosine = cosf(angle);
    float sine = sinf(angle);
    float cosineLeft = cosine;
    cosineLeft *= left;

    m_positionCursor[0] = px - cosineLeft - top * sine;
    m_positionCursor[1] = py - left * sine + top * cosine;
    m_positionCursor[2] = px + right * cosine - top * sine;
    m_positionCursor[3] = py + right * sine + top * cosine;
    m_positionCursor[4] = px + right * cosine - bottom * sine;
    m_positionCursor[5] = py + right * sine + bottom * cosine;
    m_positionCursor[6] = px - cosineLeft - bottom * sine;
    m_positionCursor[7] = py - left * sine + bottom * cosine;

    bool useMaskTexture = additive && image->_blendModes == 3;
    CTextureUsage* texture = image->m_textureUsage[useMaskTexture ? 1 : 0];
    m_particleTextures[m_vertexCount >> 2] = texture;

    m_uvCursor += 8;
    m_positionCursor += 8;
    m_vertexCount += 4;
    m_colorCursor += 4;
    m_currentImage = image;
}

SRenderState*
KLBParticleMovie::renderCallback(void* context)
{
    return static_cast<KLBParticleMovie*>(context)->renderParticles();
}

SRenderState*
KLBParticleMovie::renderParticles()
{
    CKLBRenderingManager& renderingManager = CKLBRenderingManager::getInstance();
    SRenderState* state = 0;
    u32 particleCount = m_vertexCount >> 2;

    if (particleCount) {
        CKLBOGLWrapper::getInstance();

        KLBParticleBatch batch;
        batch.textures[0] = 0;
        batch.textures[1] = 0;
        batch.buffers[0] = m_positionBuffer;
        batch.buffers[1] = m_uvColorBuffer;
        batch.uniformIDs[0] = 1;
        batch.vertexCount = 0;
        batch.state = 0;

        m_uvCursor = m_particleSprite->getSrcUVBuffer();
        m_positionCursor = m_particleSprite->getSrcXYBuffer();
        m_colorCursor = m_particleSprite->getLocalColorBuffer();

        const SMatrix2D& matrix = m_renderNode->m_composedMatrix;
        state = renderingManager.getAlphaState();

        for (u32 particle = 0; particle < particleCount; ++particle) {
            u16 stride;
            float* positions = m_positionBuffer->updateStart(1, 0, &stride);
            u32* colors = reinterpret_cast<u32*>(
                m_uvColorBuffer->updateStart(3, 0, 0)
            );

            batch.textures[0] = m_particleTextures[particle];
            if (batch.vertexCount) {
                submitParticleBatch(true, &batch);
            }

            batch.vertexCount = 0;
            batch.state = state;

            float* sourceUV = m_uvCursor;
            float* sourcePosition = m_positionCursor;
            u32* sourceColors = m_colorCursor;
            u32* particleColors = sourceColors;
            sourceColors += 4;

            for (u32 vertex = 0; vertex < 4; ++vertex) {
                float x = sourcePosition[0];
                float y = sourcePosition[1];
                positions[0] = matrix.m_matrix[MAT_A] * x
                             + matrix.m_matrix[MAT_B] * y
                             + matrix.m_matrix[MAT_TX];
                positions[1] = matrix.m_matrix[MAT_C] * x
                             + matrix.m_matrix[MAT_D] * y
                             + matrix.m_matrix[MAT_TY];
                positions[2] = sourceUV[0];
                positions[3] = sourceUV[1];
                *colors++ = particleColors[vertex];

                positions += 4;
                sourceUV += 2;
                sourcePosition += 2;
            }
            m_uvCursor = sourceUV;
            m_positionCursor = sourcePosition;
            m_colorCursor = sourceColors;
            batch.vertexCount += 4;
        }

        if (batch.vertexCount) {
            submitParticleBatch(true, &batch);
        }
    }

    return state;
}

void
KLBParticleMovie::submitParticleBatch(bool switchState, KLBParticleBatch* batch)
{
    CKLBRenderingManager& renderingManager = CKLBRenderingManager::getInstance();
    CKLBOGLWrapper& renderer = CKLBOGLWrapper::getInstance();

    if (switchState) {
        renderer.applyState(batch->state);
    }

    m_positionBuffer->updateComplete(batch->vertexCount);
    m_positionBuffer->setDrawOffset(0);
    m_uvColorBuffer->updateComplete(batch->vertexCount);
    m_uvColorBuffer->setDrawOffset(0);

    renderingManager.draw(
        batch->buffers,
        m_indexBuffer,
        batch->textures,
        batch->uniformIDs,
        (batch->vertexCount >> 2) * 6
    );
}

CKLBParticleAsset::~CKLBParticleAsset()
{
    delete m_library;
}

CKLBNode*
CKLBParticleAsset::createSubTree(u32)
{
    return 0;
}

CKLBParticleAssetPlugin::~CKLBParticleAssetPlugin()
{
}

void
CKLBParticleAssetPlugin::setImageAliases(const char** aliases, s32 aliasCount)
{
    m_imageAliases = aliases;
    m_imageAliasCount = aliasCount;
}

CKLBParticleAssetPlugin::CKLBParticleAssetPlugin()
: m_atlasPixels(0)
{
}

CKLBAbstractAsset*
CKLBParticleAssetPlugin::loadAsset(u8* stream, size_t streamSize)
{
    CKLBParticleAsset* asset = new CKLBParticleAsset();
    if (asset) {
        m_loadedImageCount = 0;

        KLBInnerParticleAsset* library = new KLBInnerParticleAsset();
        asset->m_library = library;
        library->m_plugin = this;

        zlib_filefunc_def archiveFunctions;
        KLBParticleArchiveStream archive;
        archive.size = streamSize;
        archive.data = stream;
        archive.position = 0;

        archiveFunctions.opaque      = &archive;
        archiveFunctions.zopen_file  = (open_file_func)particleArchiveOpen;
        archiveFunctions.zclose_file = (close_file_func)particleArchiveClose;
        archiveFunctions.zerror_file = (testerror_file_func)particleArchiveError;
        archiveFunctions.zseek_file  = (seek_file_func)particleArchiveSeek;
        archiveFunctions.ztell_file  = (tell_file_func)particleArchiveTell;
        archiveFunctions.zread_file  = (read_file_func)particleArchiveRead;
        archiveFunctions.zwrite_file = (write_file_func)particleArchiveWrite;

        m_streamSize = streamSize;
        m_stream = stream;
        m_streamPosition = 0;

        unzFile archiveFile = unzOpen2(NULL, &archiveFunctions);
        if (archiveFile) {
            if (unzGoToFirstFile(archiveFile) == UNZ_OK) {
                do {
                    unz_file_info entry;
                    char entryName[260];
                    if (unzGetCurrentFileInfo(archiveFile, &entry, entryName,
                                              sizeof(entryName), NULL, 0, NULL, 0) != UNZ_OK) {
                        break;
                    }
                    if (strcasecmp(entryName, "data.xml") == 0) {
                        u32 size = entry.uncompressed_size;
                        u8* buffer = new u8[size];
                        if (unzOpenCurrentFile(archiveFile) == UNZ_OK) {
                            if (unzReadCurrentFile(archiveFile, buffer, size) == (s32)size) {
                                if (asset->m_library->Load((const char*)buffer, size, true)) {
                                    unzCloseCurrentFile(archiveFile);
                                    unzClose(archiveFile);
                                    delete [] buffer;
                                    return packLoadedImages() ? asset : NULL;
                                }
                                klb_assertAlways("FAIL LOADING ASSET PARTICLES SETUP");
                            }
                            unzCloseCurrentFile(archiveFile);
                        }
                        delete [] buffer;
                    }
                } while (unzGoToNextFile(archiveFile) == UNZ_OK);
            }
            unzClose(archiveFile);
        }
        delete asset;
    }
    klb_assertAlways("Fail to Load Particle Asset.");
}

// Allowed Property Keys
CKLBLuaPropTask::PROP_V2 CKLBUIParticle::ms_propItems[] = {
    UI_BASE_PROP,
    { "order", UINTEGER, (setBoolT)&CKLBUIParticle::setOrder, (getBoolT)&CKLBUIParticle::getOrder, 0 },
    { "asset", R_STRING, NULL,                                 (getBoolT)&CKLBUIParticle::getAsset, 0 }
};

// CKLBUIParticle::initCore lives in this translation unit in the shipped
// build: its assertions carry this file's name and line numbers, and it
// inlines both KLBParticleMovie's constructor and the plugin's alias setter,
// neither of which is visible from the UI task's own translation unit.
bool
CKLBUIParticle::initCore(u32 order, float x, float y,
                         const char* assetName, const char* effectName,
                         u32 particleCount)
{
    // Six indices per particle must stay inside a 16 bit index buffer. The
    // shipped assertion in this rejection path repeats the order predicate of
    // the check below while carrying the particle-count diagnostic.
    u32 indexCount = particleCount * 6;
    if (indexCount >= 0x10000) {
        klb_assert(((s32)order) >= 0, "Particle Count too big. Max 10922 (6 index, 4 vertice per particle < 65k)");
        return false;
    }

    if(!setupPropertyList((const char**)ms_propItems, SizeOfArray(ms_propItems))) {
        return false;
    }

    setInitPos(x, y);

    klb_assert(((s32)order) >= 0, "Order Problem");
    m_order = order;
    setStrC(m_assetName, assetName);

    CKLBNode* node = getNode();
    node->setRenderSlotCount(1);

    CKLBRenderingManager& render = CKLBRenderingManager::getInstance();
    CKLBDynSprite* sprite = render.allocateCommandDynSprite(
        (u16)(particleCount * 4), (u16)indexCount, 0);

    CKLBAssetManager& assetManager = CKLBAssetManager::getInstance();
    CKLBParticleAssetPlugin* plugin =
        static_cast<CKLBParticleAssetPlugin*>(assetManager.getPlugin('C'));
    plugin->setImageAliases((const char**)m_textureNames, m_textureNameCount);

    CKLBAbstractAsset* asset = assetManager.loadAssetByFileName(assetName, plugin, false, false);
    if(!asset) { return false; }
    if(asset->getAssetType() != static_cast<ASSET_TYPE>(14)) { return false; }
    m_asset = (CKLBParticleAsset*)asset;
    asset->incrementRefCount();

    m_movie = new KLBParticleMovie(particleCount);
    bool result = m_movie->setup(node, m_order);
    // setup() publishes the movie through the node, so re-read the member.
    if(!m_movie) { return false; }

    IClientRequest& client = CPFInterface::getInstance().client();
    s32 screenWidth = client.getPhysicalScreenWidth();
    s32 screenHeight = client.getPhysicalScreenHeight();
    m_movie->SetScreenSize(screenWidth, screenHeight);

    m_libraryEffect = m_asset->m_library->GetEffect(effectName);
    klb_assert(m_libraryEffect, "Fail to find effect '%s'.", effectName);

    TLFX::Effect* effect = new TLFX::Effect(*m_libraryEffect, m_movie, false);
    effect->SetPosition(0.0f, 0.0f);
    m_movie->AddEffect(effect, 0);
    m_movie->m_particleSprite = sprite;
    m_effect = effect;

    return (sprite != NULL) && result;
}
