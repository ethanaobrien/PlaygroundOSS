#ifdef _MSC_VER
#pragma once
#endif

#ifndef _TLFX_PARTICLE_H
#define _TLFX_PARTICLE_H

#include "TLFXEntity.h"

#include <list>

namespace TLFX
{

    class Emitter;
    class ParticleManager;
	class Particle;
	
	typedef std::list<Particle*> ParticleList;

    /**
     * Particle Type - extends tlEntity
     * This is the object that is spawned by emitter types and maintained by a Particle Manager. Particles are controlled by the emitters and effects they're
     * parented to.
     */
    class Particle : public Entity
    {
        typedef Entity base;
    public:
        friend class Emitter;

        Particle();

        /**
         * Updates the particle.
         * This is called by the emitter the particle is parented to.
         */
        bool Update();

        /**
         * Resets the particle so it's ready to be recycled by the particle manager
         */
        void Reset();

        void Destroy(bool releaseChildren = true);

        /**
         * Set the current x coordinate of the particle and capture the old value
         */
        void SetX(float x);

        /**
         * Set the current y coordinate of the particle and capture the old value
         */
        void SetY(float y);

        /**
         * Set the current zoom factor of the particle and capture the old value
         */
        void SetZ(float z);

        void SetGroupParticles(bool value) { _groupParticles = value; }
        bool IsGroupParticles() const { return _groupParticles; }

        void SetLayer(int layer) { _layer = layer; }
        int GetLayer() const { return _layer; }

        void SetEffectLayer(int layer) { _effectLayer = layer; }
        int GetEffectLayer() const { return _effectLayer; }

        void SetEmitter(Emitter *e) { _emitter = e; }
        Emitter* GetEmitter() const { return _emitter; }

        void SetParticleManager(ParticleManager *pm) { _particleManager = pm; }

        void SetReleaseSingleParticles(bool value) { _releaseSingleParticle = value; }

        void SetVelVariation(float velVariation) { _velVariation = velVariation; }
        float GetVelVariation() const { return _velVariation; }

        void SetGSizeX(float gSizeX) { _gSizeX = gSizeX; }
        float GetGSizeX() const { return _gSizeX; }
        void SetGSizeY(float gSizeY) { _gSizeY = gSizeY; }
        float GetGSizeY() const { return _gSizeY; }

        void SetScaleVariationX(float scaleVarX) { _scaleVariationX = scaleVarX; }
        float GetScaleVariationX() const { return _scaleVariationX; }
        void SetScaleVariationY(float scaleVarY) { _scaleVariationY = scaleVarY; }
        float GetScaleVariationY() const { return _scaleVariationY; }

        void SetEmissionAngle(float emissionAngle) { _emissionAngle = emissionAngle; }
        float GetEmissionAngle() const { return _emissionAngle; }

        void SetDirectionVairation(float dirVar) { _directionVariation = dirVar; }
        float GetDirectionVariation() const { return _directionVariation; }

        void SetSpinVariation(float spinVar) { _spinVariation = spinVar; }
        float GetSpinVariation() const { return _spinVariation; }

        void SetWeightVariation(float weightVar) { _weightVariation = weightVar; }
        float GetWeightVariation() const { return _weightVariation; }
		
		void SetIter(ParticleList::iterator iter) { _listIter = iter; }
		ParticleList::iterator GetIter() const { return _listIter; }

    protected:
        Emitter*                    _emitter;                       // emitter it belongs to
        // -----------------------------
        float                       _weightVariation;               // Particle weight variation
        float                       _scaleVariationX;               // particle size x variation
        float                       _scaleVariationY;               // particle size y variation
        float                       _gSizeX;                        // Particle global size x
        float                       _gSizeY;                        // Particle global size y
        // -----------------------------
        float                       _velVariation;                  // velocity variation
        // -----------------------------
        float                       _spinVariation;                 // variation of spin speed
        // -----------------------------
        float                       _directionVariation;            // Direction variation at spawn time
        int                         _timeTracker;                   // This is used to keep track of game ticks so that some things can be updated between specific time intervals
        float                       _randomDirection;               // current direction of the random motion that pulls the particle in different directions
        float                       _randomSpeed;                   // random speed to apply to the particle movement
        float                       _emissionAngle;                 // Direction variation at spawn time
        bool                        _releaseSingleParticle;         // set to true to release single particles and let them decay and die
        // ----------------------------
        ParticleManager*            _particleManager;               // link to the particle manager
        int                         _layer;                         // layer the particle belongs to
        bool                        _groupParticles;                // whether the particle is added the PM pool or kept in the emitter's pool
        int                         _effectLayer;
		
		ParticleList::iterator      _listIter;                      // for quick deletes from ParticleList
    };

} // namespace TLFX

#endif // _TLFX_PARTICLE_H
