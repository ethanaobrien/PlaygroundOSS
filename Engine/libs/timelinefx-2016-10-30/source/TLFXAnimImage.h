#ifdef _MSC_VER
#pragma once
#endif

#ifndef _TLFX_ANIMIMAGE_H
#define _TLFX_ANIMIMAGE_H

#include <string>

namespace TLFX
{

    class AnimImage
    {
    public:
        AnimImage();
        virtual ~AnimImage() {}

        virtual bool Load(const char *filename) = 0;

        void                SetWidth(float width) { _width = width; }
        float               GetWidth() const { return _width; }
        void                SetHeight(float height) { _height = height; }
        float               GetHeight() const { return _height; }
        void                SetMaxRadius(float radius) { _maxRadius = radius; }
        float               GetMaxRadius() const { return _maxRadius; }
        void                SetFramesCount(int frames) { _frames = frames; }
        int                 GetFramesCount() const { return _frames; }
        void                SetIndex(int index) { _index = index; }
        int                 GetIndex() const { return _index; }
        void                MarkBlendMode(int blendMode) { _blendModes |= (1U << blendMode); }
        void                ClearBlendModes() { _blendModes = 0; }
        unsigned char       GetBlendModes() const { return _blendModes; }
        void                SetFilename(const char *filename) { _filename = filename; }
        const char         *GetFilename() const { return _filename.c_str(); }
        void                SetName(const char *name) { _name = name; }
        const char         *GetName() const { return _name.c_str(); }

        virtual void        FindRadius() {}

    protected:
        float _width;
        float _height;
        float _maxRadius;
        unsigned char _frames;
        unsigned char _index;
        unsigned char _blendModes;
        std::string _filename;
        std::string _name;
    };

} // namespace TLFX

#endif // _TLFX_ANIMIMAGE_H
