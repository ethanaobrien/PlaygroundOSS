#include "TLFXParticle.h"
#include "TLFXEmitter.h"
#include "TLFXParticleManager.h"
#include "TLFXEffect.h"
#include "TLFXEffectsLibrary.h"         // TLFXLOG

namespace TLFX
{

    Particle::Particle()
        : _emitter(NULL)

        , _weightVariation(0)
        , _scaleVariationX(0)
        , _scaleVariationY(0)
        , _gSizeX(0)
        , _gSizeY(0)

        , _velVariation(0)
        , _spinVariation(0)

        , _directionVariation(0)
        , _timeTracker(0)
        , _randomDirection(0)
        , _randomSpeed(0)
        , _emissionAngle(0)
        , _releaseSingleParticle(false)

        , _particleManager(NULL)
        , _layer(0)
        , _groupParticles(false)
        , _effectLayer(0)
		// can't initialize _listIter without knowing what list this particle will be put into
    {
        _updateHandler = static_cast<UpdateHandler>(&Particle::Update);
    }

    bool Particle::Update()
    {
        TLFXLOG(PARTICLES, ("particle #%p update", this));

        Capture();

        if (_emitter->IsDying() || _emitter->IsOneShot() || _dead)
            _releaseSingleParticle = true;

        if (_emitter->IsSingleParticle() && !_releaseSingleParticle)
        {
            _age = _particleManager->GetCurrentTime() - _dob;
            if (_age > _lifeTime)
            {
                _age = 0;
                _dob = _particleManager->GetCurrentTime();
            }
        }
        else
        {
            _age = _particleManager->GetCurrentTime() - _dob;
        }

        base::Update();

        if (_age > _lifeTime || _dead == 2)                 // if dead=2 then that means its reached the end of the line (in kill mode) for line traversal effects
        {
            _dead = 1;
            if (_children.empty())
            {
                _particleManager->ReleaseParticle(this);
                if (_emitter->IsGroupParticles())
                    _emitter->GetParentEffect()->RemoveInUse(_layer, this);

                Reset();
                return false;               // RemoveChild
            }
            else
            {
                _emitter->ControlParticle(this);
                KillChildren();
            }

            return true;
        }

        _emitter->ControlParticle(this);
        return true;
    }

    void Particle::Reset()
    {
        _age = 0;
        _wx = 0;
        _wy = 0;
        _z = 1.0f;
        _avatar = NULL;
        _dead = 0;
        ClearChildren();
        _directionVariation = 0;
        _direction = 0;
        _directionLocked = false;
        _randomSpeed = 0;
        _randomDirection = 0;
        _parent = NULL;
        _rootParent = NULL;
        _aCycles = 0;
        _cCycles = 0;
        _rptAgeA = 0;
        _rptAgeC = 0;
        _releaseSingleParticle = false;
        _gravity = 0;
        _weight = 0;
        _emitter = NULL;
		// let _listIter stay an invalid iterator
    }

    void Particle::Destroy(bool releaseChildren)
    {
        _particleManager->ReleaseParticle(this);
        base::Destroy();
        Reset();
    }

    void Particle::SetX( float x )
    {
        if (_age > 0)
            _oldX = _x;
        else
            _oldX = x;
        _x = x;
    }

    void Particle::SetY( float y )
    {
        if (_age > 0)
            _oldY = _y;
        else
            _oldY = y;
        _y = y;
    }

    void Particle::SetZ( float z )
    {
        if (_age > 0)
            _oldZ = _z;
        else
            _oldZ = z;
        _z = z;
    }

} // namespace TLFX
