#ifndef _PLAYGROUND_PARTICLE_H
#define _PLAYGROUND_PARTICLE_H

#include "TLFXAnimImage.h"
#include "TLFXEffectsLibrary.h"
#include "TLFXParticleManager.h"
#include "CKLBAsset.h"
#include "CKLBRendering.h"

class CKLBParticleAsset;
class CKLBParticleAssetPlugin;
class CKLBUIParticle;
class CKLBImageAsset;
class CKLBTextureAsset;
class CTextureUsage;
class CKLBDynSprite;
struct KLBParticleBatch;

class KLBImage : public TLFX::AnimImage
{
public:
    KLBImage()
    : m_textureUsage()
    , m_imageAsset(0)
    , m_frameUV(0)
    {
    }

    virtual ~KLBImage();

    virtual bool Load(const char* filename);
    virtual void FindRadius();

private:
    friend class KLBInnerParticleAsset;
    friend class KLBParticleMovie;
    friend class CKLBParticleAssetPlugin;

    void buildFrameUV(s32 textureWidth, s32 textureHeight);

    CTextureUsage*      m_textureUsage[2];
    CKLBTextureAsset*   m_imageAsset;
    CKLBParticleAssetPlugin* m_assetPlugin;
    float               m_uv[2][8];
    float*              m_frameUV;
    unsigned char       m_imageMode;
    bool                m_standardRect;
};

// Playground's EffectsLibrary adapter routes TimelineFX image requests through
// the particle asset plugin that owns the active load context.
class KLBInnerParticleAsset : public TLFX::EffectsLibrary
{
public:
    virtual ~KLBInnerParticleAsset();

    virtual TLFX::XMLLoader* CreateLoader() const;
    virtual TLFX::AnimImage* CreateImage() const;

    CKLBParticleAssetPlugin* m_plugin;
};

// TimelineFX batches four vertices and six indices for each live particle.
// The transient write cursors address the dynamic sprite's typed geometry and
// color buffers while the owned GPU buffers retain submitted particle batches.
class KLBParticleMovie : public TLFX::ParticleManager
{
    friend class CKLBUIParticle;
public:
    explicit KLBParticleMovie(s32 particleCount);
    virtual ~KLBParticleMovie();

    bool setup(CKLBNode* node, u32 renderOrder);

protected:
    virtual void DrawSprite(
        TLFX::AnimImage* sprite,
        float px, float py,
        float frame,
        float x, float y,
        float rotation,
        float scaleX, float scaleY,
        unsigned char r, unsigned char g, unsigned char b,
        float a,
        bool additive
    );
    virtual void finishParticleBatch();
    virtual void beginParticleBatch();

private:
    static SRenderState* renderCallback(void* context);
    SRenderState* renderParticles();
    void submitParticleBatch(bool switchTexture, KLBParticleBatch* batch);

    CTextureUsage*         m_currentTexture;
    CKLBDynSprite*          m_particleSprite;
    CTextureUsage**        m_particleTextures;
    u32*                   m_colorCursor;
    float*                 m_uvCursor;
    float*                 m_positionCursor;
    u16*                   m_indexCursor;
    u32                    m_vertexCount;
    KLBImage*              m_currentImage;
    CIndexBuffer*          m_indexBuffer;
    CBuffer*               m_positionBuffer;
    CBuffer*               m_uvColorBuffer;
    CKLBNode*              m_renderNode;
    SRenderStateCallback   m_renderCallback;
    s32                    m_particleCapacity;
};

class CKLBParticleAsset : public CKLBAsset
{
public:
    enum {
        CLASS_ID = 0x0002000c
    };

    virtual ~CKLBParticleAsset();

    virtual u32 getClassID() { return CLASS_ID; }
    virtual ASSET_TYPE getAssetType() { return static_cast<ASSET_TYPE>(14); }
    virtual CKLBNode* createSubTree(u32 priorityBase = 0);

private:
    friend class CKLBParticleAssetPlugin;
    friend class CKLBUIParticle;

    KLBInnerParticleAsset* m_library;
};

class CKLBParticleAssetPlugin : public IKLBAssetPlugin
{
public:
    CKLBParticleAssetPlugin();
    virtual ~CKLBParticleAssetPlugin();

    virtual u8 charHeader() { return 'C'; }
    virtual u32 getChunkID() { return CHUNK_TAG('P', 'A', 'R', 'T'); }
    virtual const char* fileExtension() { return ".eff"; }
    virtual CKLBAbstractAsset* loadAsset(u8* stream, size_t streamSize);
    virtual CKLBAbstractAsset* loadByFileName(const char*) { return 0; }

    // The particle task hands its script-supplied replacement table to the
    // plugin before requesting the effect archive; the loader consumes it while
    // resolving image names.
    void setImageAliases(const char** aliases, s32 aliasCount);

private:
    friend class KLBImage;

    // One entry per image the effect archive asked for. The packer writes the
    // atlas placement of every plane back into the record: TimelineFX blend
    // mode 3 carries a second, masked plane, so the placement arrays hold two.
    struct ImageLoadRecord {
        KLBImage*          image;
        u8*                pixels;
        u16                width;
        u16                height;
        u16                x[2];
        u16                y[2];
        u8                 channelCount;
        u8                 page[2];
        bool               rotated[2];
        u8                 planeCount;
    };

    bool retainLoadedImage(KLBImage* image);
    bool loadImageFromArchive(const char* name);
    bool packLoadedImages();
    void blitImagePlane(ImageLoadRecord* record, bool premultiplied, s32 plane);

    u8*                    m_stream;
    u32                    m_streamSize;
    u32                    m_streamPosition;
    const char**           m_imageAliases;
    s32                    m_imageAliasCount;
    s32                    m_loadedImageCount;
    u32                    m_atlasWidth;
    u32                    m_atlasHeight;
    u8*                    m_atlasPixels;
    ImageLoadRecord        m_currentImage;
    ImageLoadRecord        m_loadedImages[32];
};

#endif // _PLAYGROUND_PARTICLE_H
