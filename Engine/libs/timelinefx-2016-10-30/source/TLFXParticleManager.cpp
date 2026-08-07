#include "TLFXParticleManager.h"
#include "TLFXEffect.h"
#include "TLFXParticle.h"
#include "TLFXEmitter.h"
#include "TLFXAnimImage.h"
#include "TLFXEffectsLibrary.h"

#include <cassert>
#include <cmath>

namespace TLFX
{
    const int   ParticleManager::particleLimit = 5000;
	bool        ParticleManager::createParticlesAsNeeded = true;
    float       ParticleManager::_globalAmountScale = 1.0f;

    ParticleManager::ParticleManager(int particles /*= particleLimit*/, int layers /*= 1*/)
        : _angle(0)
        , _oldAngle(0)

        , _vpW(0)
        , _vpH(0)
        , _vpX(0)
        , _vpY(0)
        , _tv(0)
        , _tx(0)
        , _ty(0)
        , _tz(0)
        , _px(0)
        , _py(0)
        , _angleTweened(0)

        , _localAmountScale(1.0f)

        , _camtx(0)
        , _camty(0)
        , _camtz(0)

        , _spawningAllowed(true)
        , _testCount(0)

        , _paused(false)

        , _currentTime(0)
        , _currentTick(0)
        , _idleTimeLimit(100)

        , _renderCount(0)
        , _currentTween(0)

        , _effectLayers(0)
        , _inUseCount(0)
    {
        _inUse.resize(layers);
        _effects.resize(layers);
        _effectLayers = layers;
        _superEffects.clear();

        for (int el = 0; el < layers; ++el)
        {
            _inUse[el].resize(10);
        }

        for (int c = 0; c < particles; ++c)
        {
            Particle* p = new Particle();
            p->SetOKtoRender(false);                // @todo dan ?
            _unused.push(p);
        }
    }

    ParticleManager::~ParticleManager()
    {
        ClearAll();
        ClearInUse();
        while (!_unused.empty())
        {
            delete _unused.top();
            _unused.pop();
        }
        /*
        for (auto it = _inUse.begin(); it != _inUse.end(); ++it)
        {
            for (auto it2 = it->begin(); it2 != it->end(); ++it2)
            {
                for (auto it3 = it2->begin(); it3 != it2->end(); ++it3)
                {
                    delete *it3;
                }
            }
        }

        for (auto it = _effects.begin(); it != _effects.end(); ++it)
        {
            for (auto it2 = it->begin(); it2 != it->end(); ++it2)
            {
                delete *it2;
            }
        }
        */
    }

    void ParticleManager::Update()
    {
        if (!_paused)
        {
            for (int el = 0; el < _effectLayers; ++el)
            {
                // Effect
                for (auto it =_effects[el].begin(); it != _effects[el].end(); )
                {
                    if (!(*it)->UpdateSelected())
                    {
                        //RemoveEffect(*it);
                        auto x = *it;
                        delete x;
                        _effects[el].erase(it++);
                    }
                    else
                        ++it;
                }
            }
            _currentTime += EffectsLibrary::GetUpdateTime();
            ++_currentTick;
            TLFXLOG(PARTICLES, ("tick: %d time: %f", _currentTick, GetCurrentTime()));
        }
    }

    Particle* ParticleManager::GrabParticle( Effect *effect, bool pool, int layer /*= 0*/ )
    {
		Particle *p = NULL;
        if (!_unused.empty())
        {
            p = _unused.top();
            _unused.pop();
		}
		else if(createParticlesAsNeeded)
		{
			p = new Particle();
		}

		if(p)
		{
            p->SetLayer(layer);
            p->SetGroupParticles(pool);

            if (pool)
                effect->AddInUse(layer, p);
            else
			{
				auto& plist = _inUse[effect->GetEffectLayer()][layer];
				plist.push_back(p);
				p->SetIter(--plist.end());
			}

            ++_inUseCount;

            return p;
        }

        return NULL;
    }

    void ParticleManager::ReleaseParticle( Particle *p )
    {
        --_inUseCount;
        _unused.push(p);
        if (!p->IsGroupParticles())
        {
            auto& plist = _inUse[p->GetEffectLayer()][p->GetLayer()];
			plist.erase(p->GetIter());
        }
    }

    void ParticleManager::DrawParticles( float tween /*= 1.0f*/, int layer /*= -1*/ )
    {
        _currentTween = tween;
        _camtx = 0.0f;
        _camty = 0.0f;
        _camtz = 1.0f;

        // record current GFX states
        /* not used
        float cAlpha = GetAlpha();
        float cRotation = GetRotation();
        float cScaleX = GetScaleX();
        float cScaleY = GetScaleY();
        int cRed = GetR();
        int cGreen = GetG();
        int cBlue = GetB();
        */

        // rendercount = 0
        if (_angle != 0)
        {
            _angleTweened = TweenValues(_oldAngle, _angle, tween);
            const float radians = _angleTweened * ((float)M_PI / 180.0f);
            const float cosine = cosf(radians);
            const float sine = sinf(radians);
            _matrix.Set(cosine, sine, -sine, cosine);
        }

        int layers = 0;
        int startLayer = 0;
        if (layer == -1 || layer >= _effectLayers)
        {
            layers = _effectLayers - 1;
        }
        else
        {
            layers = layer;
            startLayer = layer;
        }

        for (int el = startLayer; el <= layers; ++el)
        {
            for (int i = 0; i < 10; ++i)
            {
                auto& plist = _inUse[el][i];
                for (auto it = plist.begin(); it != plist.end(); ++it)
                {
                    DrawParticle(*it);
                }
            }
        }
        DrawEffects();

        // restore GFX states
        /* not used
        SetAlpha(cAlpha);
        SetRotation(cRotation);
        SetScale(cScaleX, cScaleY);
        SetColor(cR, cG, cB);
        */
    }

    void ParticleManager::DrawBoundingBoxes()
    {
        for (int el = 0; el < _effectLayers; ++el)
        {
            for (auto it = _effects[el].begin(); it != _effects[el].end(); ++it)
            {
                (*it)->DrawBoundingBox();
            }
        }
    }

    void ParticleManager::SetAngle( float angle )
    {
        _oldAngle = _angle;
        _angle = angle;
    }

    void ParticleManager::SetScreenSize( int w, int h )
    {
        _vpW = (float)w;
        _vpH = (float)h;
    }

    void ParticleManager::SetScreenPosition( int x, int y )
    {
        _vpX = (float)x;
        _vpY = (float)y;
    }

    void ParticleManager::SetIdleTimeLimit( int limit )
    {
        _idleTimeLimit = limit;
    }

    float ParticleManager::GetLocalAmountScale() const
    {
        return _localAmountScale;
    }

    void ParticleManager::SetLocalAmountScale(float scale)
    {
        _localAmountScale = scale;
    }

    float ParticleManager::GetGlobalAmountScale()
    {
        return _globalAmountScale;
    }

    void ParticleManager::SetGlobalAmountScale(float scale)
    {
        _globalAmountScale = scale;
    }

    int ParticleManager::GetParticlesInUse() const
    {
        return _inUseCount;
    }

    int ParticleManager::GetParticlesUnused() const
    {
        return (int)_unused.size();
    }
	
	int ParticleManager::GetEffectCount()
	{
		int effectCount = 0;
		for (int el = 0; el < _effectLayers; ++el)
		{
			effectCount += _effects[el].size();
		}
		return effectCount;
	}
	
	std::string ParticleManager::GetEffectNames()
	{
		
		/*
		 if last outer loop and last inner loop
		 	no comma
		 
		 if not last outer loop or not last inner loop
		 */
		std::string effectNames("[ ");
		for(size_t i = 0; i < _effects.size(); ++i)
		{
			//effects += manager->_effects[i].size();
			for(auto it = _effects[i].begin(), end = _effects[i].end(); it != end; ++it)
			{
				effectNames.append((*it)->GetName());
				if(i < _effects.size() - 1 || std::distance(it, end) > 1)
					effectNames.append(", ");
			}
			if(i == _effects.size() - 1)
				effectNames.append(" ]");
			else
				effectNames.append(", ");
		}
		return effectNames;
	}

    void ParticleManager::AddPreLoadedEffect( Effect* e, int frames, int layer /*= 0*/ )
    {
        int effectLayer = layer;
        if (layer >= _effectLayers)
            effectLayer = 0;

        float tempTime = _currentTime;
        _currentTime -= frames * EffectsLibrary::GetUpdateTime();
        e->ChangeDoB(_currentTime);

        for (int i = 0; i < frames; ++i)
        {
            _currentTime = (frames + 1) * EffectsLibrary::GetUpdateTime();
            e->UpdateSelected();
            if (e->IsDestroyed())
                RemoveEffect(e);
        }
        _currentTime = tempTime;
        e->SetEffectLayer(effectLayer);
        _effects[effectLayer].insert(e);
    }

    void ParticleManager::AddEffect( Effect* e, int layer /*= 0*/ )
    {
        int effectLayer = layer;
        if (layer >= _effectLayers)
            effectLayer = 0;
        e->SetEffectLayer(effectLayer);

        // if the effect is a super effect, then just add the effects in the list
        if(e->IsSuper())
        {
            for (auto it = e->GetEffects().begin(); it != e->GetEffects().end(); ++it)
            {
                Effect* se = static_cast<Effect*>(*it);
                AddEffect(se, effectLayer);
            }
            _superEffects.push_back(e);
        }
        else
        {
            _effects[effectLayer].insert(e);
        }
    }

    void ParticleManager::RemoveEffect( Effect* e )
    {
        if (e->IsSuper())
        {
            for (auto it = e->GetEffects().begin(); it != e->GetEffects().end(); ++it)
            {
                RemoveEffect(*it);
            }
        }
        else
        {
            _effects[e->GetEffectLayer()].erase(e);
        }
    }

    void ParticleManager::ClearInUse()
    {
        for (int el = 0; el < _effectLayers; ++el)
        {
            for (int i = 0; i < 10; ++i)
            {
                auto& plist = _inUse[el][i];
                // Particle
                for (auto it = plist.begin(); it != plist.end(); ++it)
                {
                    _unused.push(*it);
                    --_inUseCount;
                    (*it)->GetEmitter()->GetParentEffect()->RemoveInUse((*it)->GetLayer(), *it);
                    (*it)->Reset();
                }
                plist.clear();
            }
        }
    }

    void ParticleManager::Destroy()
    {
        ClearAll();
        ClearInUse();
    }

    void ParticleManager::ClearAll()
    {
        for (int el = 0; el < _effectLayers; ++el)
        {
            for (auto it = _effects[el].begin(); it != _effects[el].end(); ++it)
            {
                (*it)->Destroy();
                delete *it;
            }
            _effects[el].clear();
        }

        for (size_t i = 0; i < _superEffects.size(); ++i)
        {
            Effect* effect = _superEffects[i];
            effect->GetEffects().clear();
            effect->Destroy();
            delete effect;
        }
        _superEffects.clear();
    }

    void ParticleManager::ClearLayer( int layer )
    {
        assert(layer >= 0 && layer < (int)_effects.size());

        for (auto it = _effects[layer].begin(); it != _effects[layer].end(); ++it)
        {
            (*it)->Destroy();
            delete (*it);
        }
        _effects[layer].clear();
    }

    void ParticleManager::ReleaseSingleParticles()
    {
        for (auto it = _inUse.begin(); it != _inUse.end(); ++it)
        {
            for (auto it2 = it->begin(); it2 != it->end(); ++it2)
            {
                for (auto it3 = it2->begin(); it3 != it2->end(); ++it3)
                {
                    (*it3)->SetReleaseSingleParticles(true);
                }
            }
        }
    }

    void ParticleManager::TogglePause()
    {
        _paused = !_paused;
    }
	void ParticleManager::Pause()
	{
		_paused = true;
	}
	void ParticleManager::Play()
	{
		_paused = false;
	}

    float ParticleManager::TweenValues( float oldValue, float value, float tween )
    {
        return oldValue + (value - oldValue) * tween;
    }

    void ParticleManager::DrawEffects()
    {
        for (auto it = _effects.begin(); it != _effects.end(); ++it)
        {
            for (auto it2 = it->begin(); it2 != it->end(); ++it2)
            {
                DrawEffect(*it2);
            }
        }
    }

    void ParticleManager::DrawEffect( Effect *e )
    {
        for (int i = 0; i < 10; ++i)
        {
            // particle
            const auto& plist = e->GetParticles(i);
            for (auto it = plist.begin(); it != plist.end(); ++it)
            {
                DrawParticle(*it);
                // effect
                auto& subeffects = (*it)->GetChildren();
                for (auto it2 = subeffects.begin(); it2 != subeffects.end(); ++it2)
                {
                    DrawEffect(static_cast<Effect*>(*it2));
                }
            }
        }
    }

    void ParticleManager::DrawParticle( Particle *p )
    {
        if (p->GetAge() != 0 || p->GetEmitter()->IsSingleParticle())
        {
            _px = TweenValues(p->GetOldWX(), p->GetWX(), _currentTween);
            _py = TweenValues(p->GetOldWY(), p->GetWY(), _currentTween);

            if (_angle != 0)
            {
                Vector2 rotVec = _matrix.TransformVector(Vector2(_px, _py));
                _px = rotVec.x;
                _py = rotVec.y;
            }

            if (p->GetAvatar())
            {
                AnimImage *sprite;
                float x, y;

                if (p->GetEmitter()->IsHandleCenter())
                {
                    sprite = p->GetAvatar();
                    x = sprite->GetWidth() / 2.0f;
                    y = sprite->GetHeight() / 2.0f;
                }
                else
                {
                    //SetImageHandle(p->GetAvatar()->GetImage(), p->GetHandleX(), p->GetHandleY());
                    sprite = p->GetAvatar();
                    x = (float)p->GetHandleX();
                    y = (float)p->GetHandleY();
                }

                //SetBlend(p->GetEmitter()->GetBlendMode());
                Emitter::BlendMode blend = p->GetEmitter()->GetBlendMode();

                float rotation;

                if (p->GetEmitter()->IsAngleRelative())
                {
                    if (fabsf(p->GetOldRelativeAngle() - p->GetRelativeAngle()) > 180)
                        _tv = TweenValues(p->GetOldRelativeAngle() - 360, p->GetRelativeAngle(), _currentTween);
                    else
                        _tv = TweenValues(p->GetOldRelativeAngle(), p->GetRelativeAngle(), _currentTween);
                    rotation = _tv + _angleTweened;
                }
                else
                {
                    _tv = TweenValues(p->GetOldAngle(), p->GetAngle(), _currentTween);
                    rotation = _tv + _angleTweened;
                }

                float scaleX, scaleY;

                _tx = TweenValues(p->GetOldScaleX(), p->GetScaleX(), _currentTween);
                _ty = TweenValues(p->GetOldScaleY(), p->GetScaleY(), _currentTween);
                _tz = TweenValues(p->GetOldZ(), p->GetZ(), _currentTween);
                scaleX = _tx * _tz * _camtz;
                scaleY = _ty * _tz * _camtz;

                unsigned char r, g, b;
                float a;
                //SetAlpha(p->GetAlpha());
                //SetColor(p->GetRed(), p->GetGreen(), p->GetBlue());
                a = p->GetEntityAlpha();
                r = p->GetRed();
                g = p->GetGreen();
                b = p->GetBlue();

                if (p->IsAnimating())
                {
                    const float tweenedFrame = TweenValues(p->GetOldCurrentFrame(), p->GetCurrentFrame(), _currentTween);
                    _tv = tweenedFrame;
                    const float frameCount = (float)p->GetAvatar()->GetFramesCount();
                    float wrappedFrame;
                    if (tweenedFrame < 0)
                    {
                        const float candidateFrame = frameCount + fmodf(_tv, frameCount);
                        wrappedFrame = 0.0f;
                        if (!(frameCount <= candidateFrame))
                            wrappedFrame = candidateFrame;
                    }
                    else
                    {
                        wrappedFrame = fmodf(tweenedFrame, frameCount);
                    }
                    _tv = wrappedFrame;
                }
                else
                {
                    _tv = p->GetCurrentFrame();
                }

                DrawSprite(sprite, _px, _py, _tv, x, y, rotation, scaleX, scaleY, r, g, b, a, blend == Emitter::BMLightBlend);
                // ++rendercount
            }
        }
    }

    int ParticleManager::GetIdleTimeLimit() const
    {
        return _idleTimeLimit;
    }

    bool ParticleManager::IsSpawningAllowed() const
    {
        return _spawningAllowed;
    }

    float ParticleManager::GetCurrentTime() const
    {
        return _currentTick * EffectsLibrary::GetUpdateTime();
    }

} // namespace TLFX
