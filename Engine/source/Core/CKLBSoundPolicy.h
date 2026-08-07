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
#ifndef CKLBSoundPolicy_h
#define CKLBSoundPolicy_h

#include "CKLBLuaTask.h"

class CKLBSoundPolicy : public CKLBLuaTask
{
	friend class CKLBTaskFactory<CKLBSoundPolicy>;
protected:
	CKLBSoundPolicy();
	virtual ~CKLBSoundPolicy();

public:
	static CKLBSoundPolicy* create(CKLBTask* pParentTask, const char* policyPath);

	virtual u32 getClassID();
	virtual bool initScript(CLuaState& lua);
	virtual void execute(u32 deltaT);
	virtual void die();

private:
	struct Motion {
		void* sound;
		s32 elapsed;
		s32 step;
		s32 volume;
		u8 finished;
		s8 phase;
		s16 fadeInDelay;
		s16 fadeOutDelay;
		s16 fadeInDuration;
		s16 fadeOutDuration;
		s8 targetVolume;
		u8 flags;
	};

	bool init(CKLBTask* pParentTask, const char* policyPath);
	bool loadPolicy(const char* policyPath);
	void releasePolicyText();
	void sortPolicyOffsets();
	void sortPolicyOffsets(s32 first, s32 last);
	s32 searchPolicy(const char* key, size_t length, s32 first, s32 last) const;
	s32 findPolicy(const char* soundPath) const;
	static s32 parsePolicyField(const char* policy, s32 field);

	static const u32 CLASS_ID = 0x00280053;
	static const u32 MAX_MOTIONS = 20;
	static const u32 MAX_POLICIES = 5000;

	Motion      m_motions[MAX_MOTIONS];
	u16         m_motionCount;
	char*       m_policyText;
	void*       m_activeSound;
	void*       m_policyMutex;
	const char* m_installPrefix;
	const char* m_externalPrefix;
	u16         m_installPrefixLength;
	u16         m_externalPrefixLength;
	u32         m_policyOffsets[MAX_POLICIES];
	u16         m_policyCount;
};

#endif // CKLBSoundPolicy_h
