#include "TLFXEmitter.h"
#include "TLFXEffect.h"
#include "TLFXEffectsLibrary.h"  // UpdateMode
#include "TLFXAnimImage.h"
#include "TLFXParticleManager.h"
#include "TLFXParticle.h"

#include <algorithm>
#include <cmath>
#include <cassert>

namespace TLFX
{
    Emitter::Emitter()
        : Entity()
        , _currentLife(0)
        , _uniform(true)
        , _parentEffect(NULL)
        , _image(NULL)
        , _attributeArrays(NULL)
        , _handleCenter(false)
        , _angleOffset(0)
        , _lockedAngle(false)
        , _gx(0)
        , _gy(0)
        , _counter(0)
        , _oldCounter(0)
        , _angleType(AngAlign)
        , _angleRelative(false)
        , _useEffectEmission(false)
        , _deleted(false)
        , _visible(true)
        , _singleParticle(false)
        , _startedSpawning(false)
        , _spawned(0)
        , _randomColor(false)
        , _zLayer(0)
        , _animate(false)
        , _randomStartFrame(false)
        , _animationDirection(1)
        , _colorRepeat(0)
        , _alphaRepeat(0)
        , _dirAlternater(false)
        , _oneShot(false)
        , _particlesRelative(false)
        , _tweenSpawns(false)
        , _once(false)
        , _dying(false)
        , _groupParticles(false)

        , _arrayOwner(true)

        , _bypassWeight(false)
        , _bypassSpeed(false)
        , _bypassSpin(false)
        , _bypassDirectionvariation(false)
        , _bypassColor(false)
        , _bRed(false)
        , _bGreen(false)
        , _bBlue(false)
        , _bypassScaleX(false)
        , _bypassScaleY(false)
        , _bypassLifeVariation(false)
        , _bypassFramerate(false)
        , _bypassStretch(false)
        , _bypassSplatter(false)

        , _AABB_ParticleMaxWidth(0)
        , _AABB_ParticleMaxHeight(0)
        , _AABB_ParticleMinWidth(0)
        , _AABB_ParticleMinHeight(0)

        , _currentLifeVariation(0)
        , _currentWeight(0)
        , _currentWeightVariation(0)
        , _currentSpeed(0)
        , _currentSpeedVariation(0)
        , _currentSpin(0)
        , _currentSpinVariation(0)
        , _currentDirectionVariation(0)
        , _currentEmissionAngle(0)
        , _currentEmissionRange(0)
        , _currentSizeX(0)
        , _currentSizeY(0)
        , _currentSizeXVariation(0)
        , _currentSizeYVariation(0)
        , _currentFramerate(0)

    {
        _childrenOwner = false;         // the Particles are managing by pool

        _cAmount.SetRange(EffectsLibrary::amountMin, EffectsLibrary::amountMax);
        _cLife.SetRange(EffectsLibrary::lifeMin, EffectsLibrary::lifeMax);
        _cSizeX.SetRange(EffectsLibrary::dimensionsMin, EffectsLibrary::dimensionsMax);
        _cSizeY.SetRange(EffectsLibrary::dimensionsMin, EffectsLibrary::dimensionsMax);
        _cBaseSpeed.SetRange(EffectsLibrary::velocityMin, EffectsLibrary::velocityMax);
        _cBaseWeight.SetRange(EffectsLibrary::weightMin, EffectsLibrary::weightMax);
        _cBaseSpin.SetRange(EffectsLibrary::spinMin, EffectsLibrary::spinMax);
        _cEmissionAngle.SetRange(EffectsLibrary::angleMin, EffectsLibrary::angleMax);
        _cEmissionRange.SetRange(EffectsLibrary::emissionRangeMin, EffectsLibrary::emissionRangeMax);
        _cSplatter.SetRange(EffectsLibrary::dimensionsMin, EffectsLibrary::dimensionsMax);
        _cVelVariation.SetRange(EffectsLibrary::velocityMin, EffectsLibrary::velocityMax);
        _cWeightVariation.SetRange(EffectsLibrary::weightVariationMin, EffectsLibrary::weightVariationMax);
        _cLifeVariation.SetRange(EffectsLibrary::lifeMin, EffectsLibrary::lifeMax);
        _cAmountVariation.SetRange(EffectsLibrary::amountMin, EffectsLibrary::amountMax);
        _cSizeXVariation.SetRange(EffectsLibrary::dimensionsMin, EffectsLibrary::dimensionsMax);
        _cSizeYVariation.SetRange(EffectsLibrary::dimensionsMin, EffectsLibrary::dimensionsMax);
        _cSpinVariation.SetRange(EffectsLibrary::spinVariationMin, EffectsLibrary::spinVariationMax);
        _cDirectionVariation.SetRange(EffectsLibrary::globalPercentMin, EffectsLibrary::globalPercentMax);
        _cAlpha.SetRange(0, 1.0f);
        _cR.SetRange(0, 0);
        _cG.SetRange(0, 0);
        _cB.SetRange(0, 0);
        _cScaleX.SetRange(EffectsLibrary::globalPercentMin, EffectsLibrary::globalPercentMax);
        _cScaleY.SetRange(EffectsLibrary::globalPercentMin, EffectsLibrary::globalPercentMax);
        _cSpin.SetRange(EffectsLibrary::spinOverTimeMin, EffectsLibrary::spinOverTimeMax);
        _cVelocity.SetRange(EffectsLibrary::velocityOverTimeMin, EffectsLibrary::velocityOverTimeMax);
        _cWeight.SetRange(EffectsLibrary::globalPercentMin, EffectsLibrary::globalPercentMax);
        _cDirection.SetRange(EffectsLibrary::directionOverTimeMin, EffectsLibrary::directionOverTimeMax);
        _cDirectionVariationOT.SetRange(EffectsLibrary::globalPercentMin, EffectsLibrary::globalPercentMax);
        _cFramerate.SetRange(EffectsLibrary::framerateMin, EffectsLibrary::framerateMax);
        _cStretch.SetRange(EffectsLibrary::globalPercentMin, EffectsLibrary::globalPercentMax);
        _cGlobalVelocity.SetRange(EffectsLibrary::globalPercentMin, EffectsLibrary::globalPercentMax);
        _updateHandler = static_cast<UpdateHandler>(&Emitter::Update);
        _constantAttributes = 0;
    }

    Emitter::Emitter( const Emitter& o, ParticleManager *pm )
        : Entity(o)

        , _currentLife(o._currentLife)
        , _uniform(o._uniform)
        , _parentEffect(NULL)
        , _image(o._image)
        , _attributeArrays(o._attributeArrays)
        , _handleCenter(o._handleCenter)
        , _angleOffset(o._angleOffset)
        , _lockedAngle(o._lockedAngle)
        , _gx(o._gx)
        , _gy(o._gy)
        , _counter(o._counter)
        , _oldCounter(o._oldCounter)
        , _angleType(o._angleType)
        , _angleRelative(o._angleRelative)
        , _useEffectEmission(o._useEffectEmission)
        , _deleted(o._deleted)
        , _visible(o._visible)
        , _singleParticle(o._singleParticle)
        , _startedSpawning(o._startedSpawning)
        , _spawned(o._spawned)
        , _randomColor(o._randomColor)
        , _zLayer(o._zLayer)
        , _animate(o._animate)
        , _randomStartFrame(o._randomStartFrame)
        , _animationDirection(o._animationDirection)
        , _colorRepeat(o._colorRepeat)
        , _alphaRepeat(o._alphaRepeat)
        , _dirAlternater(o._dirAlternater)
        , _oneShot(o._oneShot)
        , _particlesRelative(o._particlesRelative)
        , _tweenSpawns(o._tweenSpawns)
        , _once(o._once)
        , _dying(o._dying)
        , _groupParticles(o._groupParticles)

        , _initialAttributes(o._initialAttributes)
        , _compiledR(o._compiledR), _compiledG(o._compiledG), _compiledB(o._compiledB)
        , _compiledBaseSpin(o._compiledBaseSpin), _compiledSpin(o._compiledSpin), _compiledSpinVariation(o._compiledSpinVariation)
        , _compiledVelocity(o._compiledVelocity), _compiledBaseWeight(o._compiledBaseWeight), _compiledWeight(o._compiledWeight), _compiledWeightVariation(o._compiledWeightVariation)
        , _compiledBaseSpeed(o._compiledBaseSpeed), _compiledVelVariation(o._compiledVelVariation), _compiledAlpha(o._compiledAlpha)
        , _compiledSizeX(o._compiledSizeX), _compiledSizeY(o._compiledSizeY), _compiledScaleX(o._compiledScaleX), _compiledScaleY(o._compiledScaleY)
        , _compiledSizeXVariation(o._compiledSizeXVariation), _compiledSizeYVariation(o._compiledSizeYVariation)
        , _compiledLifeVariation(o._compiledLifeVariation), _compiledLife(o._compiledLife)
        , _compiledAmount(o._compiledAmount), _compiledAmountVariation(o._compiledAmountVariation)
        , _compiledEmissionAngle(o._compiledEmissionAngle), _compiledEmissionRange(o._compiledEmissionRange), _compiledGlobalVelocity(o._compiledGlobalVelocity)
        , _compiledDirection(o._compiledDirection), _compiledDirectionVariation(o._compiledDirectionVariation), _compiledDirectionVariationOT(o._compiledDirectionVariationOT)
        , _compiledFramerate(o._compiledFramerate), _compiledStretch(o._compiledStretch), _compiledSplatter(o._compiledSplatter)
        , _arrayOwner(false)                // runtime copies share the template's compiled samples

        , _bypassWeight(o._bypassWeight)
        , _bypassSpeed(o._bypassSpeed)
        , _bypassSpin(o._bypassSpin)
        , _bypassDirectionvariation(o._bypassDirectionvariation)
        , _bypassColor(o._bypassColor)
        , _bRed(o._bRed)
        , _bGreen(o._bGreen)
        , _bBlue(o._bBlue)
        , _bypassScaleX(o._bypassScaleX)
        , _bypassScaleY(o._bypassScaleY)
        , _bypassLifeVariation(o._bypassLifeVariation)
        , _bypassFramerate(o._bypassFramerate)
        , _bypassStretch(o._bypassStretch)
        , _bypassSplatter(o._bypassSplatter)

        , _AABB_ParticleMaxWidth(o._AABB_ParticleMaxWidth)
        , _AABB_ParticleMaxHeight(o._AABB_ParticleMaxHeight)
        , _AABB_ParticleMinWidth(o._AABB_ParticleMinWidth)
        , _AABB_ParticleMinHeight(o._AABB_ParticleMinHeight)

        , _currentLifeVariation(o._currentLifeVariation)
        , _currentWeight(o._currentWeight)
        , _currentWeightVariation(o._currentWeightVariation)
        , _currentSpeed(o._currentSpeed)
        , _currentSpeedVariation(o._currentSpeedVariation)
        , _currentSpin(o._currentSpin)
        , _currentSpinVariation(o._currentSpinVariation)
        , _currentDirectionVariation(o._currentDirectionVariation)
        , _currentEmissionAngle(o._currentEmissionAngle)
        , _currentEmissionRange(o._currentEmissionRange)
        , _currentSizeX(o._currentSizeX)
        , _currentSizeY(o._currentSizeY)
        , _currentSizeXVariation(o._currentSizeXVariation)
        , _currentSizeYVariation(o._currentSizeYVariation)
        , _currentFramerate(o._currentFramerate)

        , _path(o._path)

        // copy automatically: base/entity
        // not copy: 
    {
        _dob = pm->GetCurrentTime();
        SetOKtoRender(false);

        _children.clear();
        for (auto it = o.GetEffects().begin(); it != o.GetEffects().end(); ++it)
        {
            AddEffect(new Effect(**it, pm));
        }
        _updateHandler = static_cast<UpdateHandler>(&Emitter::Update);
        _constantAttributes = o._constantAttributes;
    }


    Emitter::~Emitter()
    {
        if (_arrayOwner)
        {
            delete [] _attributeArrays;
            _compiledR.Release();
            _compiledG.Release();
            _compiledB.Release();
            _compiledBaseSpin.Release();
            _compiledSpin.Release();
            _compiledSpinVariation.Release();
            _compiledVelocity.Release();
            _compiledBaseWeight.Release();
            _compiledWeight.Release();
            _compiledWeightVariation.Release();
            _compiledBaseSpeed.Release();
            _compiledVelVariation.Release();
            _compiledAlpha.Release();
            _compiledSizeX.Release();
            _compiledSizeY.Release();
            _compiledScaleX.Release();
            _compiledScaleY.Release();
            _compiledSizeXVariation.Release();
            _compiledSizeYVariation.Release();
            _compiledLifeVariation.Release();
            _compiledLife.Release();
            _compiledAmount.Release();
            _compiledAmountVariation.Release();
            _compiledEmissionAngle.Release();
            _compiledEmissionRange.Release();
            _compiledGlobalVelocity.Release();
            _compiledDirection.Release();
            _compiledDirectionVariation.Release();
            _compiledDirectionVariationOT.Release();
            _compiledFramerate.Release();
            _compiledStretch.Release();
            _compiledSplatter.Release();
        }
    }

    void Emitter::SortAll()
    {
        _cR.Sort();
        _cG.Sort();
        _cB.Sort();
        _cBaseSpin.Sort();
        _cSpin.Sort();
        _cSpinVariation.Sort();
        _cVelocity.Sort();
        _cBaseSpeed.Sort();
        _cVelVariation.Sort();
        //_cAs.Sort();
        _cAlpha.Sort();
        _cSizeX.Sort();
        _cSizeY.Sort();
        _cScaleX.Sort();
        _cScaleY.Sort();
        _cSizeXVariation.Sort();
        _cSizeYVariation.Sort();
        _cLifeVariation.Sort();
        _cLife.Sort();
        _cAmount.Sort();
        _cAmountVariation.Sort();
        _cEmissionAngle.Sort();
        _cEmissionRange.Sort();
        _cFramerate.Sort();
        _cStretch.Sort();
        _cGlobalVelocity.Sort();
    }

    void Emitter::ShowAll()
    {
        SetVisible(true);
        // Effect
        for (auto it = _effects.begin(); it != _effects.end(); ++it)
        {
            (*it)->ShowAll();
        }
    }

    void Emitter::HideAll()
    {
        SetVisible(false);
        // Effect
        for (auto it = _effects.begin(); it != _effects.end(); ++it)
        {
            (*it)->HideAll();
        }
    }

    AttributeNode* Emitter::AddScaleX( float f, float v )
    {
        return _cScaleX.Add(f, v);
    }

    AttributeNode* Emitter::AddScaleY( float f, float v )
    {
        return _cScaleY.Add(f, v);
    }

    AttributeNode* Emitter::AddSizeX( float f, float v )
    {
        return _cSizeX.Add(f, v);
    }

    AttributeNode* Emitter::AddSizeY( float f, float v )
    {
        return _cSizeY.Add(f, v);
    }

    AttributeNode* Emitter::AddSizeXVariation( float f, float v )
    {
        return _cSizeXVariation.Add(f, v);
    }

    AttributeNode* Emitter::AddSizeYVariation( float f, float v )
    {
        return _cSizeYVariation.Add(f, v);
    }

    AttributeNode* Emitter::AddBaseSpeed( float f, float v )
    {
        return _cBaseSpeed.Add(f, v);
    }

    AttributeNode* Emitter::AddVelocity( float f, float v )
    {
        return _cVelocity.Add(f, v);
    }

    AttributeNode* Emitter::AddBaseWeight( float f, float v )
    {
        return _cBaseWeight.Add(f, v);
    }

    AttributeNode* Emitter::AddWeightVariation( float f, float v )
    {
        return _cWeightVariation.Add(f, v);
    }

    AttributeNode* Emitter::AddWeight( float f, float v )
    {
        return _cWeight.Add(f, v);
    }

    AttributeNode* Emitter::AddVelVariation( float f, float v )
    {
        return _cVelVariation.Add(f, v);
    }

//     AttributeNode* Emitter::AddAS( float f, float v )
//     {
//         return _cAs.Add(f, v);
//     }

    AttributeNode* Emitter::AddAlpha( float f, float v )
    {
        return _cAlpha.Add(f, v);
    }

    AttributeNode* Emitter::AddSpin( float f, float v )
    {
        return _cSpin.Add(f, v);
    }

    AttributeNode* Emitter::AddBaseSpin( float f, float v )
    {
        return _cBaseSpin.Add(f, v);
    }

    AttributeNode* Emitter::AddSpinVariation( float f, float v )
    {
        return _cSpinVariation.Add(f, v);
    }

    AttributeNode* Emitter::AddR( float f, float v )
    {
        return _cR.Add(f, v);
    }

    AttributeNode* Emitter::AddG( float f, float v )
    {
        return _cG.Add(f, v);
    }

    AttributeNode* Emitter::AddB( float f, float v )
    {
        return _cB.Add(f, v);
    }

    AttributeNode* Emitter::AddLifeVariation( float f, float v )
    {
        return _cLifeVariation.Add(f, v);
    }

    AttributeNode* Emitter::AddLife( float f, float v )
    {
        return _cLife.Add(f, v);
    }

    AttributeNode* Emitter::AddAmount( float f, float v )
    {
        return _cAmount.Add(f, v);
    }

    AttributeNode* Emitter::AddAmountVariation( float f, float v )
    {
        return _cAmountVariation.Add(f, v);
    }

    AttributeNode* Emitter::AddEmissionAngle( float f, float v )
    {
        return _cEmissionAngle.Add(f, v);
    }

    AttributeNode* Emitter::AddEmissionRange( float f, float v )
    {
        return _cEmissionRange.Add(f, v);
    }

    AttributeNode* Emitter::AddGlobalVelocity( float f, float v )
    {
        return _cGlobalVelocity.Add(f, v);
    }

    AttributeNode* Emitter::AddDirection( float f, float v )
    {
        return _cDirection.Add(f, v);
    }

    AttributeNode* Emitter::AddDirectionVariation( float f, float v )
    {
        return _cDirectionVariation.Add(f, v);
    }

    AttributeNode* Emitter::AddDirectionVariationOT( float f, float v )
    {
        return _cDirectionVariationOT.Add(f, v);
    }

    AttributeNode* Emitter::AddFramerate( float f, float v )
    {
        return _cFramerate.Add(f, v);
    }

    AttributeNode* Emitter::AddStretch( float f, float v )
    {
        return _cStretch.Add(f, v);
    }

    AttributeNode* Emitter::AddSplatter( float f, float v )
    {
        return _cSplatter.Add(f, v);
    }

    void Emitter::AddEffect( Effect* effect )
    {
        _effects.push_back(effect);
    }

    void Emitter::SetParentEffect( Effect *parent )
    {
        _parentEffect = parent;
    }

    void Emitter::SetImage( AnimImage* image )
    {
        _image = image;
        if (image)
            image->MarkBlendMode(_blendMode);
        _AABB_ParticleMaxWidth = image->GetWidth() * 0.5f;
        _AABB_ParticleMaxHeight = image->GetHeight() * 0.5f;
        _AABB_ParticleMinWidth = image->GetWidth() * (-0.5f);
        _AABB_ParticleMinHeight = image->GetHeight() * (-0.5f);
    }

    void Emitter::SetAngleOffset( int offset )
    {
        _angleOffset = offset;
    }

    void Emitter::SetUniform( bool value )
    {
        _uniform = value;
    }

    void Emitter::SetAngleType( Angle type )
    {
        _angleType = type;
    }

    void Emitter::SetAngleType( int type )
    {
        _angleType = AngAlign;
        switch (type)
        {
        case 0: break;              // nothing, already set by default
        case 1: _angleType = AngRandom; break;
        case 2: _angleType = AngSpecify; break;
        default:
            assert(false);
        }
    }

    void Emitter::SetUseEffectEmission( bool value )
    {
        _useEffectEmission = value;
    }

    void Emitter::SetVisible( bool value )
    {
        _visible = value;
    }

    void Emitter::SetSingleParticle( bool value )
    {
        _singleParticle = value;
    }

    void Emitter::SetRandomColor( bool value )
    {
        _randomColor = value;
    }

    void Emitter::SetZLayer( int zLayer )
    {
        _zLayer = zLayer;
    }

    void Emitter::SetAnimate( bool value )
    {
        _animate = value;
    }

    void Emitter::SetRandomStartFrame( bool value )
    {
        _randomStartFrame = value;
    }

    void Emitter::SetAnimationDirection( int direction )
    {
        _animationDirection = direction;
    }

    void Emitter::SetColorRepeat( int repeat )
    {
        _colorRepeat = repeat;
    }

    void Emitter::SetAlphaRepeat( int repeat )
    {
        _alphaRepeat = repeat;
    }

    void Emitter::SetOneShot( bool value )
    {
        _oneShot = value;
    }

    void Emitter::SetHandleCenter( bool value )
    {
        _handleCenter = value;
    }

    void Emitter::SetParticlesRelative( bool value )
    {
        _particlesRelative = value;
    }

    void Emitter::SetTweenSpawns( bool value )
    {
        _tweenSpawns = value;
    }

    void Emitter::SetLockAngle( bool value )
    {
        _lockedAngle = value;
    }

    void Emitter::SetAngleRelative( bool value )
    {
        _angleRelative = value;
    }

    void Emitter::SetOnce( bool value )
    {
        _once = value;
    }

    void Emitter::SetGroupParticles( bool value )
    {
        _groupParticles = value;
    }

    const char * Emitter::GetPath() const
    {
        return _path.c_str();
    }

    void Emitter::SetRadiusCalculate( bool value )
    {
        _radiusCalculate = value;
        // Entity
        for (auto it = _children.begin(); it != _children.end(); ++it)
        {
            (*it)->SetRadiusCalculate(value);
        }
        // Effect
        for (auto it = _effects.begin(); it != _effects.end(); ++it)
        {
            (*it)->SetRadiusCalculate(value);
        }
    }

    void Emitter::Destroy(bool releaseChildren)
    {
        _parentEffect = NULL;
        _image = NULL;
        // Effect
        for (auto it = _effects.begin(); it != _effects.end(); ++it)
        {
            (*it)->Destroy();
            delete *it;
        }
        _effects.clear();

        base::Destroy(false);
    }

    void Emitter::ChangeDoB( float dob )
    {
        _dob = dob;
        // Effect
        for (auto it = _effects.begin(); it != _effects.end(); ++it)
        {
            (*it)->ChangeDoB(dob);
        }
    }

    bool Emitter::Update()
    {
        Capture();

        const float radians = _angle * ((float)M_PI / 180.0f);
        const float cosine = cosf(radians);
        const float sine = sinf(radians);
        _matrix.Set(cosine, sine, -sine, cosine);

        if (_parent && _relative)
        {
            SetZ(_parent->GetZ());
            _matrix = _matrix.Transform(_parent->GetMatrix());
            Vector2 rotvec = _parent->GetMatrix().TransformVector(Vector2(_x, _y));

            _wx = _parent->GetWX() + rotvec.x * _z;
            _wy = _parent->GetWY() + rotvec.y * _z;

            _relativeAngle = _parent->GetRelativeAngle() + _angle;
        }
        else
        {
            _wx = _x;
            _wy = _y;
        }

        if (!_tweenSpawns)
        {
            Capture();
            _tweenSpawns = true;
        }

        _dying = _parentEffect->IsDying();

        if (_radiusCalculate)
            base::UpdateEntityRadius();

        UpdateChildren();

        if (!_dead && !_dying)
        {
            if (_visible &&_parentEffect->GetParticleManager()->IsSpawningAllowed())
                UpdateSpawns();
        }
        else
        {
            if (_children.empty())
            {
                Destroy();
                return false;
            }
            else
            {
                KillChildren();
            }
        }
        return true;
    }

    void Emitter::UpdateSpawns( Particle *eSingle /*= NULL*/ )
    {
        int intCounter;
        float qty;
        float er;
        Particle* e;
        unsigned int curFrame = _parentEffect->GetCurrentEffectFrame();
        ParticleManager* pm = _parentEffect->GetParticleManager();

        qty = ((GetEmitterAmount(curFrame) + Rnd(GetEmitterAmountVariation(curFrame))) * _parentEffect->GetCurrentAmount() * pm->GetGlobalAmountScale() * pm->GetLocalAmountScale()) / EffectsLibrary::GetUpdateFrequency();
        if (!_singleParticle)
            _counter += qty;
        intCounter = (int)_counter;
        if (intCounter >= 1 || (_singleParticle && !_startedSpawning))
        {
            TLFXLOG(PARTICLES, ("spawned: %d", intCounter));
            if (!_startedSpawning && _singleParticle)
            {
                switch (_parentEffect->GetClass())
                {
                case Effect::TypePoint: intCounter = 1; break;
                case Effect::TypeArea:  intCounter = _parentEffect->GetMGX() * _parentEffect->GetMGY(); break;
                case Effect::TypeLine:
                case Effect::TypeEllipse: intCounter = _parentEffect->GetMGX(); break;
                }
            }
            else if (_singleParticle && _startedSpawning)
            {
                intCounter = 0;
            }

            // preload attributes
            _currentLife = GetEmitterLife(curFrame) * _parentEffect->GetCurrentLife();
            if (!_bypassWeight)
            {
                _currentWeight = GetEmitterBaseWeight(curFrame);
                _currentWeightVariation = GetEmitterWeightVariation(curFrame);
            }

            if (!_bypassSpeed)
            {
                _currentSpeed = GetEmitterBaseSpeed(curFrame);
                _currentSpeedVariation = GetEmitterVelVariation(curFrame);
            }

            if (!_bypassSpin)
            {
                _currentSpin = GetEmitterBaseSpin(curFrame);
                _currentSpinVariation = GetEmitterSpinVariation(curFrame);
            }

            _currentDirectionVariation = GetEmitterDirectionVariation(curFrame);

            if (_useEffectEmission)
            {
                er = _parentEffect->GetCurrentEmissionRange();
                _currentEmissionAngle = _parentEffect->GetCurrentEmissionAngle();
            }
            else
            {
                er = GetEmitterEmissionRange(curFrame);
                _currentEmissionAngle = GetEmitterEmissionAngle(curFrame);
            }

            _currentLifeVariation = GetEmitterLifeVariation(curFrame);
            _currentSizeX = GetEmitterSizeX(curFrame);
            _currentSizeY = GetEmitterSizeY(curFrame);
            _currentSizeXVariation = GetEmitterSizeXVariation(curFrame);
            _currentSizeYVariation = GetEmitterSizeYVariation(curFrame);

            const CompiledEmitterArray& compiledSplatter = _compiledSplatter;
            const float& initialSplatter = _initialAttributes[InitialSplatter];
            const float negativeEmissionRange = -er;

            // ------------------------------
            for (int c = 1; c <= intCounter; ++c)
            {
                _startedSpawning = true;
                assert(pm);
                if (!eSingle)
                {
                    e = pm->GrabParticle(_parentEffect, _groupParticles, _zLayer);
                }
                else
                {
                    e = eSingle;
                }

                if (e)
                {
#ifdef _DEBUG
                    ++EffectsLibrary::particlesCreated;
#endif
                    // -----Link to its emitter and assign the control source (which is this emitter)----
					std::string particleName = "(particle)";
					particleName.append(GetName());
					e->SetName(particleName.c_str());
                    e->SetEmitter(this);
                    e->SetParent(this);
                    e->SetParticleManager(pm);
                    e->SetEffectLayer(_parentEffect->GetEffectLayer());
                    // ----------------------------------------------------
                    e->SetDoB(pm->GetCurrentTime());

                    if (_parentEffect->GetTraverseEdge() && _parentEffect->GetClass() == Effect::TypeLine)
                    {
                        _particlesRelative = true;
                    }
                    e->SetRelative(_particlesRelative);

                    switch (_parentEffect->GetClass())
                    {
                    case Effect::TypePoint:
                        if (e->IsRelative())
                        {
                            e->SetX((float)(0 - _parentEffect->GetHandleX()));
                            e->SetY((float)(0 - _parentEffect->GetHandleY()));
                        }
                        else
                        {
                            float tween = (float)c / intCounter;
                            if (_parentEffect->GetHandleCenter() || (_parentEffect->GetHandleX() + _parentEffect->GetHandleY() == 0))
                            {
                                // @dan already set? tween = (float)c / intCounter;
                                e->SetX(TweenValues(_oldWX, _wx, tween));
                                e->SetY(TweenValues(_oldWY, _wy, tween));
                                if (_z != 1)
                                {
                                    e->SetWX(e->GetX() - _parentEffect->GetHandleX() * _z);
                                    e->SetWY(e->GetY() - _parentEffect->GetHandleY() * _z);
                                }
                                else
                                {
                                    e->SetWX(e->GetX() - _parentEffect->GetHandleX());
                                    e->SetWY(e->GetY() - _parentEffect->GetHandleY());
                                }
                            }
                            else
                            {
                                e->SetX((float)(0 - _parentEffect->GetHandleX()));
                                e->SetY((float)(0 - _parentEffect->GetHandleY()));
                                Vector2 rotvec = _parent->GetMatrix().TransformVector(Vector2(e->GetX(), e->GetY()));
                                e->SetX(TweenValues(_oldWX, _wx, tween) + rotvec.x);
                                e->SetY(TweenValues(_oldWY, _wy, tween) + rotvec.y);
                                if (_z != 1)
                                {
                                    e->SetWX(e->GetX() * _z);
                                    e->SetWY(e->GetY() * _z);
                                }
                                else
                                {
                                    e->SetWX(e->GetX());
                                    e->SetWY(e->GetY());
                                }
                            }
                        }
                        break;

                    case Effect::TypeArea:
                        if (_parentEffect->GetEmitAtPoints())
                        {
                            if (_parentEffect->GetSpawnDirection() == -1)
                            {
                                _gx += _parentEffect->GetSpawnDirection();
                                if (_gx < 0)
                                {
                                    _gx = (float)(_parentEffect->GetMGX() - 1);
                                    _gy += _parentEffect->GetSpawnDirection();
                                    if (_gy < 0)
                                        _gy = (float)(_parentEffect->GetMGY() - 1);
                                }
                            }

                            if (_parentEffect->GetMGX() > 1)
                            {
                                e->SetX((_gx / (_parentEffect->GetMGX() - 1) * _parentEffect->GetCurrentWidth()) - _parentEffect->GetHandleX());
                            }
                            else
                            {
                                e->SetX((float)(-_parentEffect->GetHandleX()));
                            }

                            if (_parentEffect->GetMGY() > 1)
                            {
                                e->SetY((_gy / (_parentEffect->GetMGY() - 1) * _parentEffect->GetCurrentHeight()) - _parentEffect->GetHandleY());
                            }
                            else
                            {
                                e->SetY((float)(-_parentEffect->GetHandleY()));
                            }

                            if (_parentEffect->GetSpawnDirection() == 1)
                            {
                                _gx += _parentEffect->GetSpawnDirection();
                                if (_gx >= _parentEffect->GetMGX())
                                {
                                    _gx = 0;
                                    _gy += _parentEffect->GetSpawnDirection();
                                    if (_gy >= _parentEffect->GetMGY())
                                        _gy = 0;
                                }
                            }
                        }
                        else
                        {
                            e->SetX(Rnd(_parentEffect->GetCurrentWidth())  - _parentEffect->GetHandleX());
                            e->SetY(Rnd(_parentEffect->GetCurrentHeight()) - _parentEffect->GetHandleY());
                        }

                        if (!e->IsRelative())
                        {
                            Vector2 rotvec = _parent->GetMatrix().TransformVector(Vector2(e->GetX(), e->GetY()));
                            if (_z != 1)
                            {
                                e->SetX(_parent->GetWX() + rotvec.x * _z);
                                e->SetY(_parent->GetWY() + rotvec.y * _z);
                            }
                            else
                            {
                                e->SetX(_parent->GetWX() + rotvec.x);
                                e->SetY(_parent->GetWY() + rotvec.y);
                            }
                        }

                        break;

                    case Effect::TypeEllipse:
                        {
                            float tx = _parentEffect->GetCurrentWidth()  / 2.0f;
                            float ty = _parentEffect->GetCurrentHeight() / 2.0f;
                            float th = 0;

                            if (_parentEffect->GetEmitAtPoints())
                            {
                                if (_parentEffect->GetMGX() == 0)
                                    _parentEffect->SetMGX(1);

                                _gx += _parentEffect->GetSpawnDirection();
                                if (_gx >= _parentEffect->GetMGX())
                                {
                                    _gx = 0;
                                }
                                else if (_gx < 0)
                                {
                                    _gx = (float)(_parentEffect->GetMGX() - 1);
                                }

                                th = _gx * (_parentEffect->GetEllipseArc() / _parentEffect->GetMGX()) + _parentEffect->GetEllipseOffset();
                            }
                            else
                            {
                                th = Rnd(_parentEffect->GetEllipseArc()) + _parentEffect->GetEllipseOffset();
                            }
                            const float radians = th * ((float)M_PI / 180.0f);
                            e->SetX( cosf(radians) * tx - _parentEffect->GetHandleX() + tx);
                            e->SetY(-sinf(radians) * ty - _parentEffect->GetHandleY() + ty);

                            if (!e->IsRelative())
                            {
                                Vector2 rotvec = _parent->GetMatrix().TransformVector(Vector2(e->GetX(), e->GetY()));
                                if (_z != 1)
                                {
                                    e->SetX(_parent->GetWX() + rotvec.x * _z);
                                    e->SetY(_parent->GetWY() + rotvec.y * _z);
                                }
                                else
                                {
                                    e->SetX(_parent->GetWX() + rotvec.x);
                                    e->SetY(_parent->GetWY() + rotvec.y);
                                }
                            }
                        }
                        break;

                    case Effect::TypeLine:
                        if (!_parentEffect->GetTraverseEdge())
                        {
                            if (_parentEffect->GetEmitAtPoints())
                            {
                                if (_parentEffect->GetSpawnDirection() == -1)
                                {
                                    _gx += _parentEffect->GetSpawnDirection();
                                    if (_gx < 0)
                                        _gx = (float)(_parentEffect->GetMGX() - 1);
                                }

                                if (_parentEffect->GetMGX() > 1)
                                {
                                    e->SetX((_gx / (_parentEffect->GetMGX() - 1) * _parentEffect->GetCurrentWidth()) - _parentEffect->GetHandleX());
                                }
                                else
                                {
                                    e->SetX((float)(-_parentEffect->GetHandleX()));
                                }
                                e->SetY((float)(-_parentEffect->GetHandleY()));

                                if (_parentEffect->GetSpawnDirection() == 1)
                                {
                                    _gx += _parentEffect->GetSpawnDirection();
                                    if (_gx >= _parentEffect->GetMGX())
                                        _gx = 0;
                                }
                            }
                            else
                            {
                                e->SetX(Rnd(_parentEffect->GetCurrentWidth()) - _parentEffect->GetHandleX());
                                e->SetY((float)(-_parentEffect->GetHandleY()));
                            }
                        }
                        else
                        {
                            if (_parentEffect->GetDistanceSetByLife())
                            {
                                e->SetX((float)(-_parentEffect->GetHandleX()));
                                e->SetY((float)(-_parentEffect->GetHandleY()));
                            }
                            else
                            {
                                if (_parentEffect->GetEmitAtPoints())
                                {
                                    if (_parentEffect->GetSpawnDirection() == -1)
                                    {
                                        _gx += _parentEffect->GetSpawnDirection();
                                        if (_gx < 0)
                                            _gx = (float)(_parentEffect->GetMGX() - 1);
                                    }

                                    if (_parentEffect->GetMGX() > 1)
                                    {
                                        e->SetX((_gx / (_parentEffect->GetMGX() - 1) * _parentEffect->GetCurrentWidth()) - _parentEffect->GetHandleX());
                                    }
                                    else
                                    {
                                        e->SetX((float)(-_parentEffect->GetHandleX()));
                                    }
                                    e->SetY((float)(-_parentEffect->GetHandleY()));

                                    if (_parentEffect->GetSpawnDirection() == 1)
                                    {
                                        _gx += _parentEffect->GetSpawnDirection();
                                        if (_gx >= _parentEffect->GetMGX())
                                            _gx = 0;
                                    }
                                }
                                else
                                {
                                    e->SetX(Rnd(_parentEffect->GetCurrentWidth()) - _parentEffect->GetHandleX());
                                    e->SetY((float)(-_parentEffect->GetHandleY()));
                                }
                            }
                        }

                        // rotate
                        if (!e->IsRelative())
                        {
                            Vector2 rotvec = _parent->GetMatrix().TransformVector(Vector2(e->GetX(), e->GetY()));
                            if (_z != 1)
                            {
                                e->SetX(_parent->GetWX() + rotvec.x * _z);
                                e->SetY(_parent->GetWY() + rotvec.y * _z);
                            }
                            else
                            {
                                e->SetX(_parent->GetWX() + rotvec.x);
                                e->SetY(_parent->GetWY() + rotvec.y);
                            }
                        }
                        break;
                    }

                    // set the zoom level
                    e->SetZ(_z);

                    // set up the image
                    e->SetAvatar(_image);
                    e->SetHandleX(_handleX);
                    e->SetHandleY(_handleY);
                    e->SetAutocenter(_handleCenter);

                    // set lifetime properties
                    e->SetLifeTime((int)(_currentLife + Rnd(-_currentLifeVariation, _currentLifeVariation) * _parentEffect->GetCurrentLife()));

                    // speed
                    e->SetSpeedVecX(0);
                    e->SetSpeedVecY(0);
                    if (!_bypassSpeed)
                    {
                        e->SetSpeed(_initialAttributes[InitialVelocity]);
                        e->SetVelVariation(Rnd(-_currentSpeedVariation, _currentSpeedVariation));
                        e->SetBaseSpeed((_currentSpeed + e->GetVelVariation()) * _parentEffect->GetCurrentVelocity());
                        //e->_velSeed = Rnd(0, 1.0f);
                        e->SetSpeed(_initialAttributes[InitialVelocity] * e->GetBaseSpeed() * _initialAttributes[InitialGlobalVelocity]);
                    }
                    else
                    {
                        e->SetSpeed(0);
                    }

                    // size
                    e->SetGSizeX(_parentEffect->GetCurrentSizeX());
                    e->SetGSizeY(_parentEffect->GetCurrentSizeY());

                    // width
                    float scaleTemp = _initialAttributes[InitialScaleX];
                    float sizeTemp = 0;
                    e->SetScaleVariationX(Rnd(_currentSizeXVariation));
                    e->SetWidth(e->GetScaleVariationX() + _currentSizeX);
                    if (scaleTemp != 0)
                    {
                        sizeTemp = (e->GetWidth() / _image->GetWidth()) * scaleTemp * e->GetGSizeX();
                    }
                    e->SetScaleX(sizeTemp);

                    if (_uniform)
                    {
                        // height
                        e->SetScaleY(sizeTemp);

                        if (!_bypassStretch)
                        {
                            e->SetScaleY((_initialAttributes[InitialScaleX] * e->GetGSizeX() * (e->GetWidth() + (fabsf(e->GetSpeed()) * _initialAttributes[InitialStretch] * _parentEffect->GetCurrentStretch()))) / _image->GetWidth());
                            if (e->GetScaleY() < e->GetScaleX())
                                e->SetScaleY(e->GetScaleX());
                        }

                        e->SetWidthHeightAABB(_AABB_ParticleMinWidth, _AABB_ParticleMaxWidth, _AABB_ParticleMinWidth, _AABB_ParticleMaxWidth);
                    }
                    else
                    {
                        // height
                        scaleTemp = _initialAttributes[InitialScaleY];
                        sizeTemp = 0;
                        e->SetScaleVariationY(Rnd(_currentSizeYVariation));
                        e->SetHeight(e->GetScaleVariationY() + _currentSizeY);
                        if (scaleTemp != 0)
                        {
                            sizeTemp = (e->GetHeight() / _image->GetHeight()) * scaleTemp * e->GetGSizeY();
                        }
                        e->SetScaleY(sizeTemp);

                        if (!_bypassStretch && e->GetSpeed() != 0)
                        {
                            e->SetScaleY((_initialAttributes[InitialScaleY] * e->GetGSizeY() * (e->GetHeight() + (fabsf(e->GetSpeed()) * _initialAttributes[InitialStretch] * _parentEffect->GetCurrentStretch()))) / _image->GetHeight());
                            if (e->GetScaleY() < e->GetScaleX())
                                e->SetScaleY(e->GetScaleX());
                        }

                        e->SetWidthHeightAABB(_AABB_ParticleMinWidth, _AABB_ParticleMaxWidth, _AABB_ParticleMinHeight, _AABB_ParticleMaxHeight);
                    }

                    // splatter
                    if (!_bypassSplatter)
                    {
                        float splatterTemp = IsAttributeConstant(10)
                            ? initialSplatter
                            : GetCompiledValue(compiledSplatter, curFrame);
                        float splatX = Rnd(-splatterTemp, splatterTemp);
                        float splatY = Rnd(-splatterTemp, splatterTemp);

                        while (Vector2::GetDistance(0, 0, splatX, splatY) >= splatterTemp && splatterTemp > 0)
                        {
                            splatX = Rnd(-splatterTemp, splatterTemp);
                            splatY = Rnd(-splatterTemp, splatterTemp);
                        }

                        if (_z == 1 || e->IsRelative())
                        {
                            e->Move(splatX, splatY);
                        }
                        else
                        {
                            e->Move(splatX * _z, splatY * _z);
                        }
                    }

                    // rotation and direction of travel settings
                    e->MiniUpdate();
                    if (_parentEffect->GetTraverseEdge() && _parentEffect->GetClass() == Effect::TypeLine)
                    {
                        e->SetDirectionLocked(true);
                        e->SetEntityDirection(90.0f);
                    }
                    else
                    {
                        if (_parentEffect->GetClass() != Effect::TypePoint)
                        {
                            if (!_bypassSpeed || _angleType == AngAlign)
                            {
                                e->SetEmissionAngle(_currentEmissionAngle + Rnd(negativeEmissionRange, er));
                                switch (_parentEffect->GetEmissionType())
                                {
                                case Effect::EmInwards:
                                    if (e->IsRelative())
                                        e->SetEmissionAngle(e->GetEmissionAngle() + Vector2::GetDirection(e->GetX(), e->GetY(), 0, 0));
                                    else
                                        e->SetEmissionAngle(e->GetEmissionAngle() + Vector2::GetDirection(e->GetWX(), e->GetWY(), e->GetParent()->GetWX(), e->GetParent()->GetWY()));
                                    break;

                                case Effect::EmOutwards:
                                    if (e->IsRelative())
                                        e->SetEmissionAngle(e->GetEmissionAngle() + Vector2::GetDirection(0, 0, e->GetX(), e->GetY()));
                                    else
                                        e->SetEmissionAngle(e->GetEmissionAngle() + Vector2::GetDirection(e->GetParent()->GetWX(), e->GetParent()->GetWY(), e->GetWX(), e->GetWY()));
                                    break;

                                case Effect::EmInAndOut:
                                    if (_dirAlternater)
                                    {
                                        if (e->IsRelative())
                                            e->SetEmissionAngle(e->GetEmissionAngle() + Vector2::GetDirection(0, 0, e->GetX(), e->GetY()));
                                        else
                                            e->SetEmissionAngle(e->GetEmissionAngle() + Vector2::GetDirection(e->GetParent()->GetWX(), e->GetParent()->GetWY(), e->GetWX(), e->GetWY()));
                                    }
                                    else
                                    {
                                        if (e->IsRelative())
                                            e->SetEmissionAngle(e->GetEmissionAngle() + Vector2::GetDirection(e->GetX(), e->GetY(), 0, 0));
                                        else
                                            e->SetEmissionAngle(e->GetEmissionAngle() + Vector2::GetDirection(e->GetWX(), e->GetWY(), e->GetParent()->GetWX(), e->GetParent()->GetWY()));
                                    }
                                    _dirAlternater = !_dirAlternater;
                                    break;

                                case Effect::EmSpecified:
                                    // nothing
                                    break;
                                }
                            }
                        }
                        else
                        {
                            e->SetEmissionAngle(_currentEmissionAngle + Rnd(negativeEmissionRange, er));
                        }

                        if (!_bypassDirectionvariation)
                        {
                            e->SetDirectionVairation(_currentDirectionVariation);
                            float dv = e->GetDirectionVariation() * _initialAttributes[InitialDirectionVariationOT];
                            e->SetEntityDirection(e->GetEmissionAngle() + _initialAttributes[InitialDirection] + Rnd(-dv, dv));
                        }
                        else
                        {
                            e->SetEntityDirection(e->GetEmissionAngle() + _initialAttributes[InitialDirection]);
                        }
                    }

                    // ------ e->_lockedAngle = _lockedAngle
                    if (!_bypassSpin)
                    {
                        e->SetSpinVariation(Rnd(-_currentSpinVariation, _currentSpinVariation) + _currentSpin);    // @todo dan currentSpin?
                    }

                    // weight
                    if (!_bypassWeight)
                    {
                        e->SetWeight(_initialAttributes[InitialWeight]);
                        e->SetWeightVariation(Rnd(-_currentWeightVariation, _currentWeightVariation));
                        e->SetBaseWeight((_currentWeight + e->GetWeightVariation()) * _parentEffect->GetCurrentWeight());
                    }

                    // -------------------
                    if (_lockedAngle)
                    {
                        if (!_bypassWeight && !_bypassSpeed && !_parentEffect->IsBypassWeight())
                        {
                            const float radians = e->GetEntityDirection() * ((float)M_PI / 180.0f);
                            e->SetSpeedVecX(sinf(radians));
                            e->SetSpeedVecY(cosf(radians));
                            e->SetAngle(Vector2::GetDirection(0, 0, e->GetSpeedVecX(), -e->GetSpeedVecY()));
                        }
                        else
                        {
                            if (_parentEffect->GetTraverseEdge())
                            {
                                e->SetAngle(_parentEffect->GetAngle() + _angleOffset);
                            }
                            else
                            {
                                e->SetAngle(e->GetEntityDirection() + _angle + _angleOffset);
                            }
                        }
                    }
                    else
                    {
                        switch (_angleType)
                        {
                        case AngAlign:
                            if (_parentEffect->GetTraverseEdge())
                                e->SetAngle(_parentEffect->GetAngle() + _angleOffset);
                            else
                                e->SetAngle(e->GetEntityDirection() + _angleOffset);
                            break;

                        case AngRandom:
                            e->SetAngle(Rnd((float)_angleOffset));
                            break;

                        case AngSpecify:
                            e->SetAngle((float)_angleOffset);
                            break;
                        }
                    }

                    // color settings
                    if (_randomColor)
                    {
                        float randomAge = Rnd((float)_compiledR.GetLastFrame()) / (float)e->_lifeTime;
                        e->SetRed((unsigned char)RandomizeR(e, randomAge));
                        e->SetGreen((unsigned char)RandomizeG(e, randomAge));
                        e->SetBlue((unsigned char)RandomizeB(e, randomAge));
                    }
                    else
                    {
                        e->SetRed((unsigned char)_initialAttributes[InitialR]);
                        e->SetGreen((unsigned char)_initialAttributes[InitialG]);
                        e->SetBlue((unsigned char)_initialAttributes[InitialB]);
                    }
                    float particleLife = e->GetAge() / (float)e->_lifeTime;
                    e->SetEntityAlpha(e->GetEmitter()->GetEmitterAlpha(particleLife) * _parentEffect->GetCurrentAlpha());

                    // blend mode
                    e->_blendMode = _blendMode;

                    // animation and framerate
                    e->_animating = _animate;
                    e->_animateOnce = _once;
                    e->_framerate = _initialAttributes[InitialFramerate];
                    if (_randomStartFrame)
                        e->_currentFrame = Rnd((float)e->_avatar->GetFramesCount());
                    else
                        e->_currentFrame = (float)_currentFrame;

                    // add any sub children
                    //e->_runChildren = false;
                    // Effect
                    for (auto it = _effects.begin(); it != _effects.end(); ++it)
                    {
                        Effect* newEffect = new Effect(*static_cast<Effect*>(*it), pm);
                        newEffect->SetParent(e);
                        newEffect->SetParentEmitter(this);
                        newEffect->SetEffectLayer(e->_effectLayer);
                    }
                    _parentEffect->SetParticlesCreated(true);

                    // get the relative angle
                    if (!e->_relative)
                    {  // @todo dan Set(cosf(_angle  ??
                        const float angle = _angle * ((float)M_PI / 180.0f);
                        const float cosine = cosf(angle);
                        const float sine = sinf(angle);
                        e->_matrix.Set(cosine, sine, -sine, cosine);
                        e->_matrix = e->_matrix.Transform(_parent->GetMatrix());
                    }
                    e->_relativeAngle = _parent->GetRelativeAngle() + e->_angle;
                    e->UpdateEntityRadius();
                        
                    // capture old values for tweening
                    e->Capture();

                } // if (e)
            } // for
            _counter -= intCounter;
        }
    } // Emitter::UpdateSpawns()

    void Emitter::ControlParticle( Particle *e )
    {
        float particleLife = e->_age / (float)e->_lifeTime;

        // alpha change
        if (_alphaRepeat > 1)
        {
            e->_rptAgeA += EffectsLibrary::GetCurrentUpdateTime() * _alphaRepeat;
            e->_alpha = GetEmitterAlpha(e->_rptAgeA / (float)e->_lifeTime) * _parentEffect->GetCurrentAlpha();
            if (e->_rptAgeA > e->_lifeTime && e->_aCycles < _alphaRepeat)
            {
                e->_rptAgeA -= e->_lifeTime;
                ++e->_aCycles;
            }
        }
        else
        {
            e->_alpha = GetEmitterAlpha(particleLife) * _parentEffect->GetCurrentAlpha();
        }

        // angle changes
        if (_lockedAngle && _angleType == AngAlign)
        {
            if (e->_directionLocked)
            {
                e->_angle = _parentEffect->GetAngle() + _angle + _angleOffset;
            }
            else
            {
                if (!_bypassWeight && (!_parentEffect->IsBypassWeight() || e->_direction))
                {
                    if (e->_oldWX != e->_wx && e->_oldWY != e->_wy)
                    {
                        if (e->_relative)
                            e->_angle = Vector2::GetDirection(e->_oldX, e->_oldY, e->_x, e->_y);
                        else
                            e->_angle = Vector2::GetDirection(e->_oldWX, e->_oldWY, e->_wx, e->_wy);

                        if (fabsf(e->_oldAngle - e->_angle) > 180)
                        {
                            if (e->_oldAngle > e->_angle)
                                e->_oldAngle -= 360;
                            else
                                e->_oldAngle += 360;
                        }
                    }
                }
                else
                {
                    e->_angle = e->_direction + _angle + _angleOffset;
                }
            }
        }
        else
        {
            if (!_bypassSpin)
                e->_angle += (GetEmitterSpin(particleLife) * e->_spinVariation * _parentEffect->GetCurrentSpin()) / EffectsLibrary::GetCurrentUpdateTime();
        }

        // direction changes and motion randomness
        if (e->_directionLocked)
        {
            e->_direction = 90;
            switch (_parentEffect->GetClass())
            {
            case Effect::TypeLine:
                if (_parentEffect->GetDistanceSetByLife())
                {
                    e->_x = (particleLife * _parentEffect->GetCurrentWidth()) - _parentEffect->GetHandleX();
                }
                else
                {
                    switch (_parentEffect->GetEndBehavior())
                    {
                    case Effect::EndKill:
                        if (e->_x > _parentEffect->GetCurrentWidth() - _parentEffect->GetHandleX() || e->_x < 0 - _parentEffect->GetHandleX())
                            e->_dead = 2;
                        break;

                    case Effect::EndLoopAround:
                        if (e->_x > _parentEffect->GetCurrentWidth() - _parentEffect->GetHandleX())
                        {
                            e->_x = (float)(-_parentEffect->GetHandleX());
                            e->MiniUpdate();
                            e->_oldX = e->_x;
                            e->_oldWX = e->_wx;
                            e->_oldWY = e->_wy;
                        }
                        else if (e->_x < 0 - _parentEffect->GetHandleX())
                        {
                            e->_x = _parentEffect->GetCurrentWidth() - _parentEffect->GetHandleX();
                            e->MiniUpdate();
                            e->_oldX = e->_x;
                            e->_oldWX = e->_wx;
                            e->_oldWY = e->_wy;
                        }
                        break;
					case Effect::EndLetFree:
						break;
                    }
                }
				break;
			default:
				break;
            }
        }
        else
        {
            if (!_bypassDirectionvariation)
            {
                float dv = e->_directionVariation * GetEmitterDirectionVariationOT(particleLife);
                e->_timeTracker += (int)EffectsLibrary::GetUpdateTime();
                if (e->_timeTracker > EffectsLibrary::motionVariationInterval)
                {
                    e->_randomDirection += EffectsLibrary::maxDirectionVariation * Rnd(-dv, dv);
                    e->_randomSpeed += EffectsLibrary::maxVelocityVariation * Rnd(-dv, dv);
                    e->_timeTracker = 0;
                }
            }
            e->_direction = e->_emissionAngle + GetEmitterDirection(particleLife) + e->_randomDirection;
        }

        // size changes
        if (!_bypassScaleX)
        {
            e->_scaleX = (GetEmitterScaleX(particleLife) * e->_gSizeX * e->_width) / _image->GetWidth();
        }
        if (_uniform)
        {
            if (!_bypassScaleX)
                e->_scaleY = e->_scaleX;
        }
        else
        {
            if (!_bypassScaleY)
            {
                e->_scaleY = (GetEmitterScaleY(particleLife) * e->_gSizeY * e->_height) / _image->GetWidth();
            }
        }

        // color changes
        if (!_bypassColor)
        {
            if (!_randomColor)
            {
                if (_colorRepeat > 1)
                {
                    e->_rptAgeC += EffectsLibrary::GetCurrentUpdateTime() * _colorRepeat;
                    float repeatLife = e->_rptAgeC / (float)e->_lifeTime;
                    e->_red = (unsigned char)GetEmitterR(repeatLife);
                    e->_green = (unsigned char)GetEmitterG(repeatLife);
                    e->_blue = (unsigned char)GetEmitterB(repeatLife);
                    if (e->_rptAgeC > e->_lifeTime && e->_cCycles < _colorRepeat)
                    {
                        e->_rptAgeC -= e->_lifeTime;
                        ++e->_cCycles;
                    }
                }
                else
                {
                    e->_red = (unsigned char)GetEmitterR(particleLife);
                    e->_green = (unsigned char)GetEmitterG(particleLife);
                    e->_blue = (unsigned char)GetEmitterB(particleLife);
                }
            }
        }

        // animation
        if (!_bypassFramerate)
            e->_framerate = GetEmitterFramerate(particleLife) * _animationDirection;

        // speed changes
        if (!_bypassSpeed)
        {
            e->_speed = GetEmitterVelocity(particleLife) * e->_baseSpeed * GetEmitterGlobalVelocity(_parentEffect->GetCurrentEffectFrame());
            e->_speed += e->_randomSpeed;
        }
        else
        {
            e->_speed = e->_randomSpeed;
        }

        // stretch
        if (!_bypassStretch)
        {
            if (!_bypassWeight && !_parentEffect->IsBypassWeight())
            {
                if (e->_speed != 0)
                {
                    e->_speedVec.x = e->_speedVec.x / EffectsLibrary::GetCurrentUpdateTime();
                    e->_speedVec.y = e->_speedVec.y / EffectsLibrary::GetCurrentUpdateTime() - e->_gravity;
                }
                else
                {
                    e->_speedVec.x = 0;
                    e->_speedVec.y = -e->_gravity;
                }
            }

            AnimImage* image = _image;
            float particleSize;
            float particleDimension;
            float imageDimension;
            if (_uniform)
            {
                particleSize = e->_gSizeX;
                particleDimension = e->_width;
                imageDimension = image->GetWidth();
            }
            else
            {
                particleSize = e->_gSizeY;
                particleDimension = e->_height;
                imageDimension = image->GetHeight();
            }
            e->_scaleY = (GetEmitterScaleX(particleLife) * particleSize * (particleDimension + (fabsf(e->_speed) * GetEmitterStretch(particleLife) * _parentEffect->GetCurrentStretch()))) / imageDimension;

            if (e->_scaleY < e->_scaleX)
                e->_scaleY = e->_scaleX;
        }

        // weight changes
        if (!_bypassWeight)
            e->_weight = GetEmitterWeight(particleLife) * e->_baseWeight;
    }

    float Emitter::RandomizeR( Particle *e, float randomAge )
    {
        if (_constantAttributes & (1U << 19))
            return _initialAttributes[InitialR];

        const CompiledEmitterArray& compiled = _compiledR;
        int frame = 0;
        if (randomAge > 0)
            frame = (int)(randomAge * compiled.GetLife());
        unsigned int sample = compiled.GetLastFrame();
        sample = (sample >= (unsigned int)frame) ? frame : sample;
        return compiled.GetValues()[sample * compiled.GetStride()];
    }

    float Emitter::RandomizeG( Particle *e, float randomAge )
    {
        if (_constantAttributes & (1U << 20))
            return _initialAttributes[InitialG];

        const CompiledEmitterArray& compiled = _compiledG;
        int frame = 0;
        if (randomAge > 0)
            frame = (int)(randomAge * compiled.GetLife());
        unsigned int sample = compiled.GetLastFrame();
        sample = (sample >= (unsigned int)frame) ? frame : sample;
        return compiled.GetValues()[sample * compiled.GetStride()];
    }

    float Emitter::RandomizeB( Particle *e, float randomAge )
    {
        if (_constantAttributes & (1U << 21))
            return _initialAttributes[InitialB];

        const CompiledEmitterArray& compiled = _compiledB;
        int frame = 0;
        if (randomAge > 0)
            frame = (int)(randomAge * compiled.GetLife());
        unsigned int sample = compiled.GetLastFrame();
        sample = (sample >= (unsigned int)frame) ? frame : sample;
        return compiled.GetValues()[sample * compiled.GetStride()];
    }

    void Emitter::DrawCurrentFrame( float x /*= 0*/, float y /*= 0*/, float w /*= 128.0f*/, float h /*= 128.0f*/ )
    {
        if (_image)
        {
            /*
            SetAlpha(1.0f);
            SetBlend(_blendMode);
            SetImageHandle(_image->GetImage(), 0, 0);
            SetColor(255, 255, 255);
            SetScale(w / _image->GetWidth(), _image->GetHeight());
            _image->Draw(x, y, _frame);
            */
        }
    }

    void Emitter::CompileAll()
    {
        // base
        if (_compiledLife.Compile(_cLife, _initialAttributes[InitialLife])) _constantAttributes |= 1U << 0;
        if (_compiledLifeVariation.Compile(_cLifeVariation, _initialAttributes[InitialLifeVariation])) _constantAttributes |= 1U << 1;
        if (_compiledAmount.Compile(_cAmount, _initialAttributes[InitialAmount])) _constantAttributes |= 1U << 2;
        if (_compiledSizeX.Compile(_cSizeX, _initialAttributes[InitialSizeX])) _constantAttributes |= 1U << 3;
        if (_compiledSizeY.Compile(_cSizeY, _initialAttributes[InitialSizeY])) _constantAttributes |= 1U << 4;
        if (_compiledBaseSpeed.Compile(_cBaseSpeed, _initialAttributes[InitialBaseSpeed])) _constantAttributes |= 1U << 5;
        if (_compiledBaseWeight.Compile(_cBaseWeight, _initialAttributes[InitialBaseWeight])) _constantAttributes |= 1U << 6;
        if (_compiledBaseSpin.Compile(_cBaseSpin, _initialAttributes[InitialBaseSpin])) _constantAttributes |= 1U << 7;
        if (_compiledEmissionAngle.Compile(_cEmissionAngle, _initialAttributes[InitialEmissionAngle])) _constantAttributes |= 1U << 8;
        if (_compiledEmissionRange.Compile(_cEmissionRange, _initialAttributes[InitialEmissionRange])) _constantAttributes |= 1U << 9;
        if (_compiledSplatter.Compile(_cSplatter, _initialAttributes[InitialSplatter])) _constantAttributes |= 1U << 10;
        if (_compiledVelVariation.Compile(_cVelVariation, _initialAttributes[InitialVelVariation])) _constantAttributes |= 1U << 18;
        if (_compiledWeightVariation.Compile(_cWeightVariation, _initialAttributes[InitialWeightVariation])) _constantAttributes |= 1U << 24;
        if (_compiledAmountVariation.Compile(_cAmountVariation, _initialAttributes[InitialAmountVariation])) _constantAttributes |= 1U << 25;
        if (_compiledSizeXVariation.Compile(_cSizeXVariation, _initialAttributes[InitialSizeXVariation])) _constantAttributes |= 1U << 26;
        if (_compiledSizeYVariation.Compile(_cSizeYVariation, _initialAttributes[InitialSizeYVariation])) _constantAttributes |= 1U << 29;
        if (_compiledSpinVariation.Compile(_cSpinVariation, _initialAttributes[InitialSpinVariation])) _constantAttributes |= 1U << 16;
        if (_compiledDirectionVariation.Compile(_cDirectionVariation, _initialAttributes[InitialDirectionVariation])) _constantAttributes |= 1U << 17;
        // Particle-lifetime attributes share one cache-friendly interleaved table.
        float longestLife = (_cLifeVariation.GetMaxValue() + _cLife.GetMaxValue()) * _parentEffect->GetLifeMaxValue();

        unsigned char compileAlpha = _compiledAlpha.PrepareOverTime(_cAlpha, _initialAttributes[InitialAlpha], (int)longestLife);
        if (!compileAlpha) _constantAttributes |= 1U << 11;
        unsigned char compileR = _compiledR.PrepareOverTime(_cR, _initialAttributes[InitialR], (int)longestLife);
        if (!compileR) _constantAttributes |= 1U << 19;
        unsigned char compileG = _compiledG.PrepareOverTime(_cG, _initialAttributes[InitialG], (int)longestLife);
        if (!compileG) _constantAttributes |= 1U << 20;
        unsigned char compileB = _compiledB.PrepareOverTime(_cB, _initialAttributes[InitialB], (int)longestLife);
        if (!compileB) _constantAttributes |= 1U << 21;
        unsigned char compileScaleX = _compiledScaleX.PrepareOverTime(_cScaleX, _initialAttributes[InitialScaleX], (int)longestLife);
        if (!compileScaleX) _constantAttributes |= 1U << 22;
        unsigned char compileScaleY = _compiledScaleY.PrepareOverTime(_cScaleY, _initialAttributes[InitialScaleY], (int)longestLife);
        if (!compileScaleY) _constantAttributes |= 1U << 23;
        unsigned char compileSpin = _compiledSpin.PrepareOverTime(_cSpin, _initialAttributes[InitialSpin], (int)longestLife);
        if (!compileSpin) _constantAttributes |= 1U << 12;
        unsigned char compileVelocity = _compiledVelocity.PrepareOverTime(_cVelocity, _initialAttributes[InitialVelocity], (int)longestLife);
        if (!compileVelocity) _constantAttributes |= 1U << 13;
        unsigned char compileWeight = _compiledWeight.PrepareOverTime(_cWeight, _initialAttributes[InitialWeight], (int)longestLife);
        if (!compileWeight) _constantAttributes |= 1U << 14;
        unsigned char compileDirection = _compiledDirection.PrepareOverTime(_cDirection, _initialAttributes[InitialDirection], (int)longestLife);
        if (!compileDirection) _constantAttributes |= 1U << 27;
        unsigned char compileDirectionVariation = _compiledDirectionVariationOT.PrepareOverTime(_cDirectionVariationOT,
            _initialAttributes[InitialDirectionVariationOT], (int)longestLife);
        if (!compileDirectionVariation) _constantAttributes |= 1U << 31;
        unsigned char compileFramerate = _compiledFramerate.PrepareOverTime(_cFramerate, _initialAttributes[InitialFramerate], (int)longestLife);
        if (!compileFramerate) _constantAttributes |= 1U << 28;
        unsigned char compileStretch = _compiledStretch.PrepareOverTime(_cStretch, _initialAttributes[InitialStretch], (int)longestLife);
        if (!compileStretch) _constantAttributes |= 1U << 15;

        int arrayCount = compileAlpha + compileR + compileG + compileB
            + compileScaleX + compileScaleY + compileSpin + compileVelocity
            + compileWeight + compileDirection + compileDirectionVariation
            + compileFramerate + compileStretch;
        int arraySize = (int)ceilf(longestLife * EffectsLibrary::GetLookupFrequencyOverTimeInverse()) + 2;
        if (arrayCount)
            _attributeArrays = new float[arrayCount * arraySize];

        int arrayIndex = 0;
        if (compileAlpha) _compiledAlpha.CompileOverTime(_cAlpha, (int)longestLife, arrayCount, _attributeArrays + arrayIndex++, arraySize);
        if (compileR) _compiledR.CompileOverTime(_cR, (int)longestLife, arrayCount, &_attributeArrays[arrayIndex++], arraySize);
        if (compileG) _compiledG.CompileOverTime(_cG, (int)longestLife, arrayCount, &_attributeArrays[arrayIndex++], arraySize);
        if (compileB) _compiledB.CompileOverTime(_cB, (int)longestLife, arrayCount, &_attributeArrays[arrayIndex++], arraySize);
        if (compileScaleX) _compiledScaleX.CompileOverTime(_cScaleX, (int)longestLife, arrayCount, &_attributeArrays[arrayIndex++], arraySize);
        if (compileScaleY) _compiledScaleY.CompileOverTime(_cScaleY, (int)longestLife, arrayCount, &_attributeArrays[arrayIndex++], arraySize);
        if (compileSpin) _compiledSpin.CompileOverTime(_cSpin, (int)longestLife, arrayCount, &_attributeArrays[arrayIndex++], arraySize);
        if (compileVelocity) _compiledVelocity.CompileOverTime(_cVelocity, (int)longestLife, arrayCount, &_attributeArrays[arrayIndex++], arraySize);
        if (compileWeight) _compiledWeight.CompileOverTime(_cWeight, (int)longestLife, arrayCount, &_attributeArrays[arrayIndex++], arraySize);
        if (compileDirection) _compiledDirection.CompileOverTime(_cDirection, (int)longestLife, arrayCount, &_attributeArrays[arrayIndex++], arraySize);
        if (compileDirectionVariation) _compiledDirectionVariationOT.CompileOverTime(_cDirectionVariationOT, (int)longestLife, arrayCount, &_attributeArrays[arrayIndex], arraySize);
        if (compileFramerate) _compiledFramerate.CompileOverTime(_cFramerate, (int)longestLife, arrayCount, &_attributeArrays[arrayIndex], arraySize);
        if (compileStretch) _compiledStretch.CompileOverTime(_cStretch, (int)longestLife, arrayCount, &_attributeArrays[arrayIndex], arraySize);

        // global adjusters
        if (_compiledGlobalVelocity.Compile(_cGlobalVelocity, _initialAttributes[InitialGlobalVelocity])) _constantAttributes |= 1U << 30;

        // Effect
        for (auto it = _effects.begin(); it != _effects.end(); ++it)
        {
            (*it)->CompileAll();
        }

        AnalyseEmitter();
    }

    void Emitter::CompileQuick()
    {
        _compiledAlpha.Resize(1);
        _compiledAlpha.SetCompiled(0, GetEmitterAlpha(0));

        _compiledR.Resize(1);
        _compiledG.Resize(1);
        _compiledB.Resize(1);
        _compiledR.SetCompiled(0, GetEmitterR(0));
        _compiledG.SetCompiled(0, GetEmitterG(0));
        _compiledB.SetCompiled(0, GetEmitterB(0));

        _compiledScaleX.Resize(1);
        _compiledScaleY.Resize(1);
        _compiledScaleX.SetCompiled(0, GetEmitterScaleX(0));
        _compiledScaleY.SetCompiled(0, GetEmitterScaleY(0));

        _compiledVelocity.Resize(1);
        _compiledVelocity.SetCompiled(0, GetEmitterVelocity(0));

        _compiledWeight.Resize(1);
        _compiledWeight.SetCompiled(0, GetEmitterWeight(0));

        _compiledDirection.Resize(1);
        _compiledDirection.SetCompiled(0, GetEmitterDirection(0));

        _compiledDirectionVariationOT.Resize(1);
        _compiledDirectionVariationOT.SetCompiled(0, GetEmitterDirectionVariationOT(0));

        _compiledFramerate.Resize(1);
        _compiledFramerate.SetCompiled(0, GetEmitterFramerate(0));

        _compiledStretch.Resize(1);
        _compiledStretch.SetCompiled(0, GetEmitterStretch(0));

        _compiledSplatter.Resize(1);
        _compiledSplatter.SetCompiled(0, GetEmitterSplatter(0));
    }

    void Emitter::AnalyseEmitter()
    {
        ResetBypassers();

        if (!_compiledLifeVariation.GetLastFrame() && !GetEmitterLifeVariation(0))
            _bypassLifeVariation = true;

        if (!GetEmitterStretch(0))
            _bypassStretch = true;

        if (!_compiledFramerate.GetLastFrame() && !_initialAttributes[InitialSplatter])
            _bypassFramerate = true;

        if (!_compiledSplatter.GetLastFrame() && !_initialAttributes[InitialSplatter])
            _bypassSplatter = true;

        if (!_compiledBaseWeight.GetLastFrame() && !_compiledWeightVariation.GetLastFrame() && !GetEmitterBaseWeight(0) && !GetEmitterWeightVariation(0))
            _bypassWeight = true;

        if (!_compiledWeight.GetLastFrame() && !_initialAttributes[InitialWeight])
            _bypassWeight = true;

        if (!_compiledBaseSpeed.GetLastFrame() && !_compiledVelVariation.GetLastFrame() && !GetEmitterBaseSpeed(0) && !GetEmitterVelVariation(0))
            _bypassSpeed = true;

        if (!_compiledBaseSpin.GetLastFrame() && !_compiledSpinVariation.GetLastFrame() && !GetEmitterBaseSpin(0) && !GetEmitterSpinVariation(0))
            _bypassSpin = true;

        if (!_compiledDirectionVariation.GetLastFrame() && !GetEmitterDirectionVariation(0))
            _bypassDirectionvariation = true;

        if ((unsigned int)_cR.GetAttributesCount() <= 1)
        {
            _bRed = _initialAttributes[InitialR] != 0;
            _bGreen = _initialAttributes[InitialG] != 0;
            _bBlue = _initialAttributes[InitialB] != 0;
            _bypassColor = true;
        }

        if ((unsigned int)_cScaleX.GetAttributesCount() <= 1)
            _bypassScaleX = true;

        if ((unsigned int)_cScaleY.GetAttributesCount() <= 1)
            _bypassScaleY = true;
    }

    void Emitter::ResetBypassers()
    {
        _bypassWeight = false;
        _bypassSpeed = false;
        _bypassSpin = false;
        _bypassDirectionvariation = false;
        _bypassColor = false;
        _bRed = false;
        _bGreen = false;
        _bBlue = false;
        _bypassScaleX = false;
        _bypassScaleY = false;
        _bypassLifeVariation = false;
        _bypassFramerate = false;
        _bypassStretch = false;
        _bypassSplatter = false;
    }

    float Emitter::GetLongestLife() const
    {
        float longestLife = ( _cLifeVariation.GetMaxValue() + _cLife.GetMaxValue() ) * _parentEffect->GetLifeMaxValue();
        /*
        float longestLife = 0;

        if (_cLife.GetLastFrame() >= _cLifeVariation.GetLastFrame() && _cLife.GetLastFrame() >= _parentEffect->GetLifeLastFrame())
        {
            for (int frame = 0; frame <= (int)_cLife.GetLastFrame(); ++frame)
            {
                float tempLife = (GetEmitterLifeVariation((float)frame) + GetEmitterLife((float)frame)) * _parentEffect->GetLife((float)frame);
                if (tempLife > longestLife) longestLife = tempLife;
            }
        }

        if (_cLifeVariation.GetLastFrame() >= _cLife.GetLastFrame() && _cLifeVariation.GetLastFrame() >= _parentEffect->GetLifeLastFrame())
        {
            for (int frame = 0; frame <= (int)_cLifeVariation.GetLastFrame(); ++frame)
            {
                float tempLife = (GetEmitterLifeVariation((float)frame) + GetEmitterLife((float)frame)) * _parentEffect->GetLife((float)frame);
                if (tempLife > longestLife) longestLife = tempLife;
            }
        }

        if (_parentEffect->GetLifeLastFrame() >= _cLife.GetLastFrame() && _parentEffect->GetLifeLastFrame() >= _cLifeVariation.GetLastFrame())
        {
            for (int frame = 0; frame <= (int)_parentEffect->GetLifeLastFrame(); ++frame)
            {
                float tempLife = (GetEmitterLifeVariation((float)frame) + GetEmitterLife((float)frame)) * _parentEffect->GetLife((float)frame);
                if (tempLife > longestLife) longestLife = tempLife;
            }
        }
*/
        return longestLife;
    }

    const std::list<Effect*>& Emitter::GetEffects() const
    {
        return _effects;
    }

    bool Emitter::IsDying() const
    {
        return _dying;
    }

    void Emitter::SetPath( const char *path )
    {
        _path = path;
    }

} // namespace TLFX
