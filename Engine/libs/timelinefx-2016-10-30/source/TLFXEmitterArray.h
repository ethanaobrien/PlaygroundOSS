#ifdef _MSC_VER
#pragma once
#endif

#ifndef _TLFX_EMITTERARRAY_H
#define _TLFX_EMITTERARRAY_H

#include "TLFXAttributeNode.h"

#include <vector>
#include <list>

namespace TLFX
{

    /**
     * Editable attribute graph.  The shipped engine keeps this source graph
     * separate from the compact table used by the particle update path.
     */
    class EmitterArrayGraph
    {
    public:
        EmitterArrayGraph();

        void SetRange(float min, float max);

        void           Clear(unsigned int size = 0);
        AttributeNode* Add(float frame, float value);
        float          Interpolate(float frame) const;
        float          InterpolateOT(float age, float lifetime, bool bezier = true) const;
        void           Sort();
        int            GetAttributesCount() const;
        float          GetMaxValue() const;

        std::list<AttributeNode>&       GetAttributes()       { return _attributes; }
        const std::list<AttributeNode>& GetAttributes() const { return _attributes; }
        float                           GetMin() const         { return _min; }
        float                           GetMax() const         { return _max; }

    private:
        friend class CompiledEmitterArray;

        std::list<AttributeNode> _attributes;
        float                    _min;
        float                    _max;

        static float GetBezierValue(const AttributeNode& lastec, const AttributeNode& a, float t, float yMin, float yMax);
        static void GetQuadBezier(float p0x, float p0y, float p1x, float p1y, float p2x, float p2y, float t, float yMin, float yMax, float& outX, float& outY, bool clamp = true);
        static void GetCubicBezier(float p0x, float p0y, float p1x, float p1y, float p2x, float p2y, float p3x, float p3y,
            float t, float yMin, float yMax, float& outX, float& outY, bool clamp = true);
    };

    /**
     * Compact sampled representation of an EmitterArrayGraph.  A descriptor
     * may either own its sample storage or describe an interleaved table owned
     * by an emitter template.
     */
    class CompiledEmitterArray
    {
    public:
        CompiledEmitterArray();
        ~CompiledEmitterArray();

        void         Release();
        void         Resize(int count);
        bool         Compile(const EmitterArrayGraph& graph, float& initialValue);
        bool         Prepare(const EmitterArrayGraph& graph) const;
        void         CompileOverTime(const EmitterArrayGraph& graph, int life,
                         int stride, float* values, int count);
        int          PrepareOverTime(const EmitterArrayGraph& graph,
                         float& initialValue, int life);
        unsigned int GetLastFrame() const;
        void         SetCompiled(unsigned int frame, float value);
        void         SetLife(int life);

        float        GetLife() const   { return _life; }
        float*       GetValues() const { return _values; }
        int          GetCount() const  { return _count; }
        unsigned int GetStride() const { return _stride; }
        bool         OwnsValues() const { return _ownsValues; }

    private:
        float         _life;
        float*        _values;
        int           _count;
        unsigned char _stride;
        bool          _ownsValues;
    };

    class EmitterArray
    {
    public:
        EmitterArray(float min, float max);

        void           Clear(unsigned int size = 0);
        AttributeNode* Add(float frame, float value);
        float          Get(float frame, bool bezier = true) const;
        float          operator()(float frame, bool bezier = true) const;
        float          GetOT(float age, float lifetime, bool bezier = true) const;
        float          operator()(float age, float lifetime, bool bezier = true) const;

        float          Interpolate(float frame, bool bezier = true) const;
        float          InterpolateOT(float age, float lifetime, bool bezier = true) const;

        void           Sort();

        unsigned int   GetAttributesCount() const;

        float           GetMaxValue() const;

        // compiled
        void           Compile();
        void           CompileOT(float longestLife);
        void           CompileOT();

        unsigned int   GetLastFrame() const;
        float          GetCompiled(unsigned int frame) const;
        void           SetCompiled(unsigned int frame, float value);

        float&         operator[](unsigned int frame);
        const float&   operator[](unsigned int frame) const;

        int            GetLife() const;
        void           SetLife(int life);

    protected:
        std::list<AttributeNode> _attributes;

        // compiled
        std::vector<float>       _changes;
        int                      _life;
        bool                     _compiled;
        float                    _min, _max;

        static float GetBezierValue(const AttributeNode& lastec, const AttributeNode& a, float t, float yMin, float yMax);
        static void GetQuadBezier(float p0x, float p0y, float p1x, float p1y, float p2x, float p2y, float t, float yMin, float yMax, float& outX, float& outY, bool clamp = true);
        static void GetCubicBezier(float p0x, float p0y, float p1x, float p1y, float p2x, float p2y, float p3x, float p3y,
            float t, float yMin, float yMax, float& outX, float& outY, bool clamp = true);
    };

} // namespace TLFX

#endif // _TLFX_EMITTERARRAY_H
