#include "TLFXEffect.h"
#include "TLFXEffectsLibrary.h"
#include "TLFXParticleManager.h"
#include "TLFXEmitter.h"
#include "TLFXParticle.h"

#include <cassert>
#include <algorithm>

namespace TLFX
{

    Effect::Effect()
        : Entity()
        , _class(TypePoint)
        , _currentEffectFrame(0)
        , _handleCenter(false)
        , _source(NULL)
        , _lockAspect(true)
        , _particlesCreated(false)
        , _suspendTime(0)
        , _gx(0)
        , _gy(0)
        , _mgx(0)
        , _mgy(0)
        , _emitAtPoints(false)
        , _emissionType(EmInwards)
        , _effectLength(0)
        , _parentEmitter(NULL)
        , _spawnAge(0)
        , _index(0)
        , _particleCount(0)
        , _idleTime(0)
        , _traverseEdge(false)
        , _endBehavior(EndKill)
        , _distanceSetByLife(false)
        , _reverseSpawn(false)
        , _spawnDirection(1)
        , _dying(false)
        , _allowSpawning(true)
        , _ellipseArc(360.0f)
        , _ellipseOffset(0)
        , _effectLayer(0)
        , _doesNotTimeout(false)

        , _particleManager(NULL)

        , _frames(32)
        , _animWidth(128)
        , _animHeight(128)
        , _looped(false)
        , _animX(0)
        , _animY(0)
        , _seed(0)
        , _zoom(1.0f)
        , _frameOffset(0)

        , _arrayOwner(true)

        , _currentLife(0)
        , _currentAmount(0)
        , _currentSizeX(0)
        , _currentSizeY(0)
        , _currentVelocity(0)
        , _currentSpin(0)
        , _currentWeight(0)
        , _currentWidth(0)
        , _currentHeight(0)
        , _currentAlpha(0)
        , _currentEmissionAngle(0)
        , _currentEmissionRange(0)
        , _currentStretch(0)
        , _currentGlobalZ(0)

        , _overrideSize(false)
        , _overrideEmissionAngle(false)
        , _overrideEmissionRange(false)
        , _overrideAngle(false)
        , _overrideLife(false)
        , _overrideAmount(false)
        , _overrideVelocity(false)
        , _overrideSpin(false)
        , _overrideSizeX(false)
        , _overrideSizeY(false)
        , _overrideWeight(false)
        , _overrideAlpha(false)
        , _overrideStretch(false)
        , _overrideGlobalZ(false)

        , _bypassWeight(false)

        , _isSuper(false)
    {
        _inUse.resize(10);

        _amountGraph.SetRange(EffectsLibrary::globalPercentMin, EffectsLibrary::globalPercentMax);
        _lifeGraph.SetRange(EffectsLibrary::globalPercentMin, EffectsLibrary::globalPercentMax);
        _sizeXGraph.SetRange(EffectsLibrary::globalPercentMin, EffectsLibrary::globalPercentMax);
        _sizeYGraph.SetRange(EffectsLibrary::globalPercentMin, EffectsLibrary::globalPercentMax);
        _velocityGraph.SetRange(EffectsLibrary::globalPercentMin, EffectsLibrary::globalPercentMax);
        _weightGraph.SetRange(EffectsLibrary::globalPercentMin, EffectsLibrary::globalPercentMax);
        _spinGraph.SetRange(EffectsLibrary::globalPercentMin, EffectsLibrary::globalPercentMax);
        _alphaGraph.SetRange(0, 1.0f);
        _emissionAngleGraph.SetRange(EffectsLibrary::angleMin, EffectsLibrary::angleMax);
        _emissionRangeGraph.SetRange(EffectsLibrary::emissionRangeMin, EffectsLibrary::emissionRangeMax);
        _widthGraph.SetRange(EffectsLibrary::dimensionsMin, EffectsLibrary::dimensionsMax);
        _heightGraph.SetRange(EffectsLibrary::dimensionsMin, EffectsLibrary::dimensionsMax);
        _effectAngleGraph.SetRange(EffectsLibrary::angleMin, EffectsLibrary::angleMax);
        _stretchGraph.SetRange(EffectsLibrary::globalPercentMin, EffectsLibrary::globalPercentMax);
        _globalZGraph.SetRange(EffectsLibrary::globalPercentMin, EffectsLibrary::globalPercentMax);
        _updateHandler = static_cast<UpdateHandler>(&Effect::Update);
        _bypassers = 0;
    }

    Effect::Effect( const Effect& o, ParticleManager* pm, bool copyDirectory /*= false*/ )
        : Entity(o)
        , _class(o._class)
        , _currentEffectFrame(o._currentEffectFrame)
        , _handleCenter(o._handleCenter)
        , _source(o._source)
        , _lockAspect(o._lockAspect)
        , _particlesCreated(o._particlesCreated)
        , _suspendTime(o._suspendTime)
        , _gx(o._gx)
        , _gy(o._gy)
        , _mgx(o._mgx)
        , _mgy(o._mgy)
        , _emitAtPoints(o._emitAtPoints)
        , _emissionType(o._emissionType)
        , _effectLength(o._effectLength)
        , _parentEmitter(o._parentEmitter)
        , _spawnAge(o._spawnAge)
        , _index(o._index)
        , _particleCount(o._particleCount)
        , _idleTime(o._idleTime)
        , _traverseEdge(o._traverseEdge)
        , _endBehavior(o._endBehavior)
        , _distanceSetByLife(o._distanceSetByLife)
        , _reverseSpawn(o._reverseSpawn)
        , _spawnDirection(o._spawnDirection)
        , _dying(o._dying)
        , _allowSpawning(o._allowSpawning)
        , _ellipseArc(o._ellipseArc)
        , _ellipseOffset(o._ellipseOffset)
        , _effectLayer(o._effectLayer)
        , _doesNotTimeout(o._doesNotTimeout)

        , _particleManager(pm)

        , _frames(o._frames)
        , _animWidth(o._animWidth)
        , _animHeight(o._animHeight)
        , _looped(o._looped)
        , _animX(o._animX)
        , _animY(o._animY)
        , _seed(o._seed)
        , _zoom(o._zoom)
        , _frameOffset(o._frameOffset)

        , _bypassValues(o._bypassValues)

        , _cLife(o._cLife)
        , _cAmount(o._cAmount)
        , _cSizeX(o._cSizeX)
        , _cSizeY(o._cSizeY)
        , _cVelocity(o._cVelocity)
        , _cWeight(o._cWeight)
        , _cSpin(o._cSpin)
        , _cAlpha(o._cAlpha)
        , _cEmissionAngle(o._cEmissionAngle)
        , _cEmissionRange(o._cEmissionRange)
        , _cWidth(o._cWidth)
        , _cHeight(o._cHeight)
        , _cEffectAngle(o._cEffectAngle)
        , _cStretch(o._cStretch)
        , _cGlobalZ(o._cGlobalZ)

        , _arrayOwner(false)                // this copy (instance) borrows the template's lookup storage

        , _currentLife(o._currentLife)
        , _currentAmount(o._currentAmount)
        , _currentSizeX(o._currentSizeX)
        , _currentSizeY(o._currentSizeY)
        , _currentVelocity(o._currentVelocity)
        , _currentSpin(o._currentSpin)
        , _currentWeight(o._currentWeight)
        , _currentWidth(o._currentWidth)
        , _currentHeight(o._currentHeight)
        , _currentAlpha(o._currentAlpha)
        , _currentEmissionAngle(o._currentEmissionAngle)
        , _currentEmissionRange(o._currentEmissionRange)
        , _currentStretch(o._currentStretch)
        , _currentGlobalZ(o._currentGlobalZ)

        , _overrideSize(o._overrideSize)
        , _overrideEmissionAngle(o._overrideEmissionAngle)
        , _overrideEmissionRange(o._overrideEmissionRange)
        , _overrideAngle(o._overrideAngle)
        , _overrideLife(o._overrideLife)
        , _overrideAmount(o._overrideAmount)
        , _overrideVelocity(o._overrideVelocity)
        , _overrideSpin(o._overrideSpin)
        , _overrideSizeX(o._overrideSizeX)
        , _overrideSizeY(o._overrideSizeY)
        , _overrideWeight(o._overrideWeight)
        , _overrideAlpha(o._overrideAlpha)
        , _overrideStretch(o._overrideStretch)
        , _overrideGlobalZ(o._overrideGlobalZ)

        , _bypassWeight(o._overrideWeight)

        , _path(o._path)

        , _isSuper(o._isSuper)

        // copy automatically: base/entity
        // not copy: Directories, inUse
    {
        _inUse.resize(10);

        SetEllipseArc(o._ellipseArc);
        _dob = pm->GetCurrentTime();
        SetOKtoRender(false);

        if(_isSuper)
        {
            _effects.clear();
            for (auto it = o._effects.begin(); it != o._effects.end(); ++it)
            {
                Effect* subEffect = new Effect(*static_cast<Effect*>(*it), pm);
                AddGroupedEffect(subEffect);
            }
        }
        else
        {
            _children.clear();
            for (auto it = o._children.begin(); it != o._children.end(); ++it)
            {
                Emitter *e = new Emitter(*static_cast<Emitter*>(*it), pm);
                e->SetParentEffect(this);
                e->SetParent(this);
            }
        }

        if (copyDirectory)
        {
            AddEffect(this);
        }
        _updateHandler = static_cast<UpdateHandler>(&Effect::Update);
        _bypassers = o._bypassers;
    }

    Effect::~Effect()
    {
        if (_arrayOwner)
        {
            _cLife.Release();
            _cAmount.Release();
            _cSizeX.Release();
            _cSizeY.Release();
            _cVelocity.Release();
            _cWeight.Release();
            _cSpin.Release();
            _cAlpha.Release();
            _cEmissionAngle.Release();
            _cEmissionRange.Release();
            _cWidth.Release();
            _cHeight.Release();
            _cEffectAngle.Release();
            _cStretch.Release();
            _cGlobalZ.Release();
        }
    }

    void Effect::New()
    {
        for (int i = 0; i < 10; ++i)
        {
            _inUse[i].clear();
        }
    }

    void Effect::SortAll()
    {
        _lifeGraph.Sort();
        _amountGraph.Sort();
        _sizeXGraph.Sort();
        _sizeYGraph.Sort();
        _velocityGraph.Sort();
        _spinGraph.Sort();
        _alphaGraph.Sort();
        _emissionAngleGraph.Sort();
        _emissionRangeGraph.Sort();
        _widthGraph.Sort();
        _heightGraph.Sort();
        _effectAngleGraph.Sort();
        _stretchGraph.Sort();
        _globalZGraph.Sort();
    }

    void Effect::ShowAll()
    {
        // Emitter
        for (auto it = _children.begin(); it != _children.end(); ++it)
        {
            static_cast<Emitter*>(*it)->ShowAll();
        }
    }

    void Effect::HideAll()
    {
        // Emitter
        for (auto it = _children.begin(); it != _children.end(); ++it)
        {
            static_cast<Emitter*>(*it)->HideAll();
        }
    }

    void Effect::ShowOne( Emitter *e )
    {
        // Emitter
        for (auto it = _children.begin(); it != _children.end(); ++it)
        {
            static_cast<Emitter*>(*it)->SetVisible(false);
        }
        e->SetVisible(true);
    }

    int Effect::EmitterCount() const
    {
        return (int)_children.size();
    }

    void Effect::SetParticleManager( ParticleManager *particleManager )
    {
        _particleManager = particleManager;
    }

    /*
    not used ?
    void Effect::AssignShapes( const std::vector<Shape>& shapes )
    {
        // Emitter
        for (auto it = _children.begin(); it != _children.end(); ++it)
        {
            Emitter* e = static_cast<Emitter*>(*it);
            e->SetImage(GetAlternativeSprite(shapes, e->GetImage()->GetName()));
            // Effect
            auto effects = it->GetChildren();
            for (auto it2 = effects.begin(); it != effects.end(); ++it2)
            {
                it2->AssignShapes(shapes);
            }
        }
    }
    */

    AttributeNode* Effect::AddAmount( float f, float v )
    {
        return _amountGraph.Add(f, v);
    }

    AttributeNode* Effect::AddLife( float f, float v )
    {
        return _lifeGraph.Add(f, v);
    }

    AttributeNode* Effect::AddSizeX( float f, float v )
    {
        return _sizeXGraph.Add(f, v);
    }

    AttributeNode* Effect::AddSizeY( float f, float v )
    {
        return _sizeYGraph.Add(f, v);
    }

    AttributeNode* Effect::AddVelocity( float f, float v )
    {
        return _velocityGraph.Add(f, v);
    }

    AttributeNode* Effect::AddWeight( float f, float v )
    {
        return _weightGraph.Add(f, v);
    }

    AttributeNode* Effect::AddSpin( float f, float v )
    {
        return _spinGraph.Add(f, v);
    }

    AttributeNode* Effect::AddAlpha( float f, float v )
    {
        return _alphaGraph.Add(f, v);
    }

    AttributeNode* Effect::AddEmissionAngle( float f, float v )
    {
        return _emissionAngleGraph.Add(f, v);
    }

    AttributeNode* Effect::AddEmissionRange( float f, float v )
    {
        return _emissionRangeGraph.Add(f, v);
    }

    AttributeNode* Effect::AddWidth( float f, float v )
    {
        return _widthGraph.Add(f, v);
    }

    AttributeNode* Effect::AddHeight( float f, float v )
    {
        return _heightGraph.Add(f, v);
    }

    AttributeNode* Effect::AddAngle( float f, float v )
    {
        return _effectAngleGraph.Add(f, v);
    }

    AttributeNode* Effect::AddStretch( float f, float v )
    {
        return _stretchGraph.Add(f, v);
    }

    AttributeNode* Effect::AddGlobalZ( float f, float v )
    {
        return _globalZGraph.Add(f, v);
    }

    void Effect::SetClass( int type )
    {
        _class = Effect::TypePoint;
        switch (type)
        {
        case 0: break;          // valid and no need to change as it is default
        case 1: _class = Effect::TypeArea; break;
        case 2: _class = Effect::TypeLine; break;
        case 3: _class = Effect::TypeEllipse; break;
        default:
            assert(false);
        }
    }

    void Effect::SetEmissionType( int type )
    {
        _emissionType = Effect::EmInwards;
        switch (type)
        {
        case 0: break;          // valid and no need to change as it is default
        case 1: _emissionType = Effect::EmOutwards; break;
        case 2: _emissionType = Effect::EmSpecified; break;
        case 3: _emissionType = Effect::EmInAndOut; break;
        default:
            assert(false);
        }
    }

    void Effect::SetEndBehavior( int behavior )
    {
        _endBehavior = Effect::EndKill;
        switch (behavior)
        {
        case 0: break;          // valid and no need to change as it is default
        case 1: _endBehavior = Effect::EndLoopAround; break;
        case 2: _endBehavior = Effect::EndLetFree; break;
        default:
            assert(false);
        }
    }

    void Effect::SetSpawnDirection()
    {
        if (_reverseSpawn)
            _spawnDirection = -1;
        else
            _spawnDirection = 1;
    }

    void Effect::SetAreaSize( float width, float height )
    {
        _overrideSize = true;
        _currentWidth = width;
        _currentHeight = height;
    }

    void Effect::SetLineLength( float length )
    {
        _overrideSize = true;
        _currentWidth = length;
    }

    void Effect::SetEmissionAngle( float angle )
    {
        _overrideEmissionAngle = true;
        _currentEmissionAngle = angle;
    }

    void Effect::SetEffectAngle( float angle )
    {
        _overrideAngle = true;
        _angle = angle;
    }

    void Effect::SetLife( float life )
    {
        _overrideLife = true;
        _currentLife = life;
    }

    void Effect::SetAmount( float amount )
    {
        _overrideAmount = true;
        _currentAmount = amount;
    }

    void Effect::SetVelocity( float velocity )
    {
        _overrideVelocity = true;
        _currentVelocity = velocity;    // @todo dan _currentAmount
    }

    void Effect::SetSpin( float spin )
    {
        _overrideSpin = true;
        _currentSpin = spin;
    }

    void Effect::SetWeight( float weight )
    {
        _overrideWeight = true;
        _currentWeight = weight;
    }

    void Effect::SetEffectParticleSize( float sizeX, float sizeY )
    {
        _overrideSizeX = true;
        _overrideSizeY = true;
        _currentSizeX = sizeX;
        _currentSizeY = sizeY;
    }

    void Effect::SetSizeX( float sizeX )
    {
        _overrideSizeX = true;
        _currentSizeX = sizeX;
    }

    void Effect::SetSizeY( float sizeY )
    {
        _overrideSizeY = true;
        _currentSizeY = sizeY;
    }

    void Effect::SetEffectAlpha( float alpha )
    {
        _overrideAlpha = true;
        _currentAlpha = alpha;
    }

    void Effect::SetEffectEmissionRange( float emissionRange )
    {
        _overrideEmissionRange = true;
        _currentEmissionRange = emissionRange;
    }

    void Effect::SetEllipseArc( float degrees )
    {
        _ellipseArc = degrees;
        _ellipseOffset = 90 - (int)(degrees / 2);
    }

    void Effect::SetZ( float z )
    {
        _overrideGlobalZ = true;
        _z = z;
    }

    void Effect::SetStretch( float stretch )
    {
        _overrideStretch = true;
        _currentStretch = stretch;
    }

    void Effect::SetGroupParticles( bool v )
    {
        // Emitter
        for (auto it = _children.begin(); it != _children.end(); ++it)
        {
            Emitter *e = static_cast<Emitter*>(*it);
            e->SetGroupParticles(v);
            // Effects
            auto effects = e->GetEffects();
            for (auto it2 = effects.begin(); it2 != effects.end(); ++it2)
            {
                Effect *eff = static_cast<Effect*>(*it2);
                eff->SetGroupParticles(v);
            }
        }
    }

    const char * Effect::GetPath() const
    {
        return _path.c_str();
    }

    int Effect::GetParticleCount() const
    {
        int particleCount = 0;
        // Emitter
        for (auto it = _children.begin(); it != _children.end(); ++it)
        {
            // Particle
            auto particles = (*it)->GetChildren();
            for (auto it2 = particles.begin(); it2 != particles.end(); ++it2)
            {
                // Effect
                auto effects = (*it2)->GetChildren();
                for (auto it3 = effects.begin(); it3 != effects.end(); ++it3)
                {
                    Effect *e = static_cast<Effect*>(*it3);
                    particleCount += e->GetParticleCount();
                }
            }
            particleCount += (*it)->GetChildCount();
        }

        return particleCount;
    }

    float Effect::GetEllipseArc() const
    {
        return _ellipseArc;
    }

    bool Effect::HasParticles() const
    {
        // Emitter
        for (auto it = _children.begin(); it != _children.end(); ++it)
        {
            if ((*it)->GetChildCount() > 0) return true;
        }
        return false;
    }

    void Effect::AddEffect(Effect* e)
    {
        _directoryEffects[e->GetPath()] = e;
        // Emitter
        auto children = e->GetChildren();
        for (auto it = children.begin(); it != children.end(); ++it)
        {
            Emitter *e = static_cast<Emitter*>(*it);
            AddEmitter(e);
        }
    }

    void Effect::AddEmitter( Emitter* e )
    {
        _directoryEmitters[e->GetPath()] = e;
        // Effect
        auto effects = e->GetEffects();
        for (auto it = effects.begin(); it != effects.end(); ++it)
        {
            AddEffect(*it);
        }
    }

    Effect* Effect::GetEffect( const char *name ) const
    {
        auto effect = _directoryEffects.find(name);
        if (effect != _directoryEffects.end())
        {
            return effect->second;
        }
        return NULL;
    }

    Emitter* Effect::GetEmitter( const char *name ) const
    {
        auto emitter = _directoryEmitters.find(name);
        if (emitter != _directoryEmitters.end())
        {
            return emitter->second;
        }
        return NULL;
    }

    void Effect::DoNotTimeout( bool value /*= true*/ )
    {
        _doesNotTimeout = value;
        // Emitter
        for (auto it = _children.begin(); it != _children.end(); ++it)
        {
            // Effect
            auto effects = static_cast<Emitter*>(*it)->GetEffects();
            for (auto it2 = effects.begin(); it2 != effects.end(); ++it2)
            {
                static_cast<Effect*>(*it2)->DoNotTimeout(value);
            }
        }
    }

    bool Effect::Update()
    {
        Capture();

        _age = _particleManager->GetCurrentTime() - _dob;

        if (_spawnAge < _age)
            _spawnAge = _age;

        if (_effectLength > 0 && _age > _effectLength)
        {
            _dob = _particleManager->GetCurrentTime();
            _age = 0;
        }

        _currentEffectFrame = _age / EffectsLibrary::GetLookupFrequency();

        if (!_overrideSize)
        {
            switch (_class)
            {
            case TypePoint:
                _currentWidth = 0;
                _currentHeight = 0;
                break;
            case TypeArea:
            case TypeEllipse:
                _currentWidth = GetWidth(_currentEffectFrame);
                _currentHeight = GetHeight(_currentEffectFrame);
                break;
            case TypeLine:
                _currentWidth = GetWidth(_currentEffectFrame);
                _currentHeight = 0;
                break;
            }
        }

        // can be optimized
        if (_handleCenter && _class != TypePoint)
        {
            _handleX = (int)(_currentWidth * 0.5f);
            _handleY = (int)(_currentHeight * 0.5f);
        }

        if (HasParticles() || _doesNotTimeout)
        {
            _idleTime = 0;
        }
        else
        {
            ++_idleTime;
        }

        float sizeX = GetSizeX(_currentEffectFrame);

        if (_parentEmitter)
        {
            if (!_overrideLife)          _currentLife          = GetLife(_currentEffectFrame)     * _parentEmitter->GetParentEffect()->_currentLife;
            if (!_overrideAmount)        _currentAmount        = GetAmount(_currentEffectFrame)   * _parentEmitter->GetParentEffect()->_currentAmount;
            if (_lockAspect)
            {
                if (!_overrideSizeX)     _currentSizeX         = sizeX                            * _parentEmitter->GetParentEffect()->_currentSizeX;
                if (!_overrideSizeY)     _currentSizeY         = _currentSizeX                    * _parentEmitter->GetParentEffect()->_currentSizeY;
            }
            else
            {
                if (!_overrideSizeX)     _currentSizeX         = sizeX                            * _parentEmitter->GetParentEffect()->_currentSizeX;
                if (!_overrideSizeY)     _currentSizeY         = GetSizeY(_currentEffectFrame)    * _parentEmitter->GetParentEffect()->_currentSizeY;
            }
            if (!_overrideVelocity)      _currentVelocity      = GetVelocity(_currentEffectFrame) * _parentEmitter->GetParentEffect()->_currentVelocity;
            if (!_overrideWeight)        _currentWeight        = GetWeight(_currentEffectFrame)   * _parentEmitter->GetParentEffect()->_currentWeight;
            if (!_overrideSpin)          _currentSpin          = GetSpin(_currentEffectFrame)     * _parentEmitter->GetParentEffect()->_currentSpin;
            if (!_overrideAlpha)         _currentAlpha         = GetAlpha(_currentEffectFrame)    * _parentEmitter->GetParentEffect()->_currentAlpha;
            if (!_overrideEmissionAngle) _currentEmissionAngle = GetEmissionAngle(_currentEffectFrame);
            if (!_overrideEmissionRange) _currentEmissionRange = GetEmissionRange(_currentEffectFrame);
            if (!_overrideAngle)         _angle                = GetEffectAngle(_currentEffectFrame);
            if (!_overrideStretch)       _currentStretch       = GetStretch(_currentEffectFrame)  *  _parentEmitter->GetParentEffect()->_currentStretch;
            if (!_overrideGlobalZ)       _currentGlobalZ       = GetGlobalZ(_currentEffectFrame)  *  _parentEmitter->GetParentEffect()->_currentGlobalZ;
        }
        else
        {
            if (!_overrideLife)          _currentLife          = GetLife(_currentEffectFrame);
            if (!_overrideAmount)        _currentAmount        = GetAmount(_currentEffectFrame);
            if (!_overrideSizeX)         _currentSizeX         = sizeX;
            if (!_overrideSizeY)         _currentSizeY         = _lockAspect ? _currentSizeX : GetSizeY(_currentEffectFrame);
            if (!_overrideVelocity)      _currentVelocity      = GetVelocity(_currentEffectFrame);
            if (!_overrideWeight)        _currentWeight        = GetWeight(_currentEffectFrame);
            if (!_overrideSpin)          _currentSpin          = GetSpin(_currentEffectFrame);
            if (!_overrideAlpha)         _currentAlpha         = GetAlpha(_currentEffectFrame);
            if (!_overrideEmissionAngle) _currentEmissionAngle = GetEmissionAngle(_currentEffectFrame);
            if (!_overrideEmissionRange) _currentEmissionRange = GetEmissionRange(_currentEffectFrame);
            if (!_overrideAngle)         _angle                = GetEffectAngle(_currentEffectFrame);
            if (!_overrideStretch)       _currentStretch       = GetStretch(_currentEffectFrame);
            if (!_overrideGlobalZ)       _currentGlobalZ       = GetGlobalZ(_currentEffectFrame);
        }

        if (!_overrideGlobalZ)
            _z = _currentGlobalZ;

        if (_currentWeight == 0)
            _bypassWeight = true;

        if (_parentEmitter)
            _dying = _parentEmitter->IsDying();


         base::Update();

         if (_idleTime > _particleManager->GetIdleTimeLimit())
             _dead = 1;

         if (_dead)
         {
             if (GetChildCount() == 0)
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

    void Effect::SoftKill()
    {
        _dying = true;
    }

    void Effect::HardKill()
    {
        _particleManager->RemoveEffect(this);
        Destroy();
    }

    void Effect::Destroy(bool releaseChildren)
    {
        _parentEmitter = NULL;
        _directoryEffects.clear();
        _directoryEmitters.clear();
        for (int i = 0; i < 10; ++i)
        {
            while (!_inUse[i].empty())
            {
                Particle *p = *_inUse[i].begin();
                p->Reset();
                _particleManager->ReleaseParticle(p);
                RemoveInUse(i, p);
            }
            _inUse[i].clear();              // should be already clear (RemoveInUse erases the items)
        }
        base::Destroy(releaseChildren);
    }

    void Effect::CompileAll()
    {
        if(_isSuper)
        {
            for (auto it = _effects.begin(); it != _effects.end(); ++it)
            {
                Effect* eff = static_cast<Effect*>(*it);
                eff->CompileAll();
            }
        }
        else
        {
            CompileLife();
            CompileAmount();
            CompileSizeX();
            CompileSizeY();
            CompileVelocity();
            CompileWeight();
            CompileSpin();
            CompileAlpha();
            CompileEmissionAngle();
            CompileEmissionRange();
            CompileWidth();
            CompileHeight();
            CompileAngle();
            CompileStretch();
            CompileGlobalZ();

            // Emitter
            for (auto it = _children.begin(); it != _children.end(); ++it)
            {
                Emitter* e = static_cast<Emitter*>(*it);
                e->CompileAll();
            }
        }
    }

    void Effect::CompileQuick()
    {
        if(_isSuper)
        {
            for (auto it = _effects.begin(); it != _effects.end(); ++it)
            {
                Effect* eff = static_cast<Effect*>(*it);
                eff->CompileQuick();
            }
        }
        else
        {
            // Emitter
            for (auto it = _children.begin(); it != _children.end(); ++it)
            {
                Emitter* e = static_cast<Emitter*>(*it);
                e->CompileQuick();
                e->ResetBypassers();
            }
        }
    }

    void Effect::CompileAmount()
    {
        if (_cAmount.Compile(_amountGraph, _bypassValues[BypassValues::Amount]))
            _bypassers |= BypassAmount;
    }

    void Effect::CompileLife()
    {
        if (_cLife.Compile(_lifeGraph, _bypassValues[BypassValues::Life]))
            _bypassers |= BypassLife;
    }

    void Effect::CompileSizeX()
    {
        if (_cSizeX.Compile(_sizeXGraph, _bypassValues[BypassValues::SizeX]))
            _bypassers |= BypassSizeX;
    }

    void Effect::CompileSizeY()
    {
        if (_cSizeY.Compile(_sizeYGraph, _bypassValues[BypassValues::SizeY]))
            _bypassers |= BypassSizeY;
    }

    void Effect::CompileVelocity()
    {
        if (_cVelocity.Compile(_velocityGraph, _bypassValues[BypassValues::Velocity]))
            _bypassers |= BypassVelocity;
    }

    void Effect::CompileWeight()
    {
        if (_cWeight.Compile(_weightGraph, _bypassValues[BypassValues::Weight]))
            _bypassers |= BypassWeight;
    }

    void Effect::CompileSpin()
    {
        if (_cSpin.Compile(_spinGraph, _bypassValues[BypassValues::Spin]))
            _bypassers |= BypassSpin;
    }

    void Effect::CompileAlpha()
    {
        if (_cAlpha.Compile(_alphaGraph, _bypassValues[BypassValues::Alpha]))
            _bypassers |= BypassAlpha;
    }

    void Effect::CompileEmissionAngle()
    {
        if (_cEmissionAngle.Compile(_emissionAngleGraph, _bypassValues[BypassValues::EmissionAngle]))
            _bypassers |= BypassEmissionAngle;
    }

    void Effect::CompileEmissionRange()
    {
        if (_cEmissionRange.Compile(_emissionRangeGraph, _bypassValues[BypassValues::EmissionRange]))
            _bypassers |= BypassEmissionRange;
    }

    void Effect::CompileWidth()
    {
        if (_cWidth.Compile(_widthGraph, _bypassValues[BypassValues::Width]))
            _bypassers |= BypassWidth;
    }

    void Effect::CompileHeight()
    {
        if (_cHeight.Compile(_heightGraph, _bypassValues[BypassValues::Height]))
            _bypassers |= BypassHeight;
    }

    void Effect::CompileAngle()
    {
        if (_cEffectAngle.Compile(_effectAngleGraph, _bypassValues[BypassValues::EffectAngle]))
            _bypassers |= BypassEffectAngle;
    }

    void Effect::CompileStretch()
    {
        if (_cStretch.Compile(_stretchGraph, _bypassValues[BypassValues::Stretch]))
            _bypassers |= BypassStretch;
    }

    void Effect::CompileGlobalZ()
    {
        if (_cGlobalZ.Compile(_globalZGraph, _bypassValues[BypassValues::GlobalZ]))
        {
            _bypassers |= BypassGlobalZ;
            _cGlobalZ.SetCompiled(0, 1.0f);
            _bypassValues[BypassValues::GlobalZ] = 1.0f;
        }
    }

    void Effect::ChangeDoB( float dob )
    {
        _dob = dob;
        // Emitter
        for (auto it = _children.begin(); it != _children.end(); ++it)
        {
            Emitter* e = static_cast<Emitter*>(*it);
            e->ChangeDoB(dob);
        }
    }

    void Effect::AddInUse( int layer, Particle *p )
    {
        assert(layer >= 0 && layer < (int)_inUse.size());

        // the particle is managed by this Effect
        SetGroupParticles(true);
		_inUse[layer].push_back(p);
		p->SetIter(--_inUse[layer].end());
    }

    void Effect::RemoveInUse( int layer, Particle *p )
	{
		auto it = p->GetIter();
        assert(layer >= 0 && layer < (int)_inUse.size() && it != _inUse[layer].end());
		_inUse[layer].erase(it);
    }

    const ParticleList& Effect::GetParticles( int layer ) const
    {
        return _inUse[layer];
    }

    void Effect::Capture()
    {
        if(_isSuper)
        {
            for (auto it = _effects.begin(); it != _effects.end(); ++it)
            {
                Effect* e = static_cast<Effect*>(*it);
                e->Capture();
            }
        }
        else
        {
            _oldZ = _z;
            _oldWX = _wx;
            _oldWY = _wy;
            _oldX = _x;
            _oldY = _y;
            _oldAngle = _angle;
            _oldRelativeAngle = _relativeAngle;
            _oldScaleX = _scaleX;
            _oldScaleY = _scaleY;
            _oldCurrentFrame = _currentFrame;
        }
    }

    void Effect::SetX(float x)
    {
        if(_isSuper)
        {
            for (auto it = _effects.begin(); it != _effects.end(); ++it)
            {
                Effect* e = static_cast<Effect*>(*it);
                e->_x = x;
            }
        }
        _x = x;
    }

    void Effect::SetY(float y)
    {
        if(_isSuper)
        {
            for (auto it = _effects.begin(); it != _effects.end(); ++it)
            {
                Effect* e = static_cast<Effect*>(*it);
                e->_y = y;
            }
        }
        _y = y;
    }

    void Effect::MakeSuper()
    {
        _isSuper = true;
        //_effects = CreateList();
    }

    void Effect::AddGroupedEffect(Effect* e)
    {
        assert(_isSuper); // Throw "This is not a super effect. Use MakeSuper first before adding effects."

        _effects.push_back(e);
        //e->_parent = this;
    }



} // namespace TLFX
