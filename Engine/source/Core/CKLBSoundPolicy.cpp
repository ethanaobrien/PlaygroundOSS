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
#include "CKLBSoundPolicy.h"
#include "CPFInterface.h"
#include "CKLBScriptEnv.h"

#include <string.h>

static CKLBTaskFactory<CKLBSoundPolicy> factory(
	"UTIL_SoundPolicy", 0x00280053);

CKLBSoundPolicy::CKLBSoundPolicy()
: CKLBLuaTask()
, m_policyText(NULL)
, m_policyMutex(NULL)
, m_installPrefix(NULL)
, m_externalPrefix(NULL)
{
}

CKLBSoundPolicy::~CKLBSoundPolicy()
{
}

u32 CKLBSoundPolicy::getClassID()
{
	return CLASS_ID;
}

CKLBSoundPolicy*
CKLBSoundPolicy::create(CKLBTask* pParentTask, const char* policyPath)
{
	CKLBSoundPolicy* pTask = KLBNEW(CKLBSoundPolicy);
	if(!pTask) { return NULL; }

	if(!pTask->init(pParentTask, policyPath)) {
		KLBDELETE(pTask);
		return NULL;
	}
	return pTask;
}

bool
CKLBSoundPolicy::init(CKLBTask* pParentTask, const char* policyPath)
{
	m_motionCount = 0;
	m_policyCount = 0;
	m_activeSound = NULL;
	m_policyMutex = CPFInterface::getInstance().platform().allocMutex();

	return m_policyMutex && loadPolicy(policyPath) && regist(pParentTask, P_INPUT);
}

bool
CKLBSoundPolicy::initScript(CLuaState& lua)
{
	if(lua.numArgs() != 1) { return false; }
	return init(NULL, lua.getString(1));
}

void
CKLBSoundPolicy::die()
{
	IPlatformRequest& platform = CPFInterface::getInstance().platform();
	if(m_policyMutex) {
		platform.freeMutex(m_policyMutex);
		m_policyMutex = NULL;
	}
	KLBDELETEA(m_installPrefix);
	m_installPrefix = NULL;
	KLBDELETEA(m_externalPrefix);
	m_externalPrefix = NULL;
	KLBDELETEA(m_policyText);
	m_policyText = NULL;
}

void
CKLBSoundPolicy::releasePolicyText()
{
	KLBDELETEA(m_policyText);
	m_policyText = NULL;
}

void
CKLBSoundPolicy::sortPolicyOffsets()
{
	sortPolicyOffsets(0, m_policyCount - 1);
}

void
CKLBSoundPolicy::sortPolicyOffsets(s32 first, s32 last)
{
	do {
		char* pivot = m_policyText + m_policyOffsets[(first + last) / 2];
		s32 left = first;
		s32 right = last;

		while(left <= right) {
			char* base = m_policyText;
			while(strcmp(base + m_policyOffsets[left], pivot) < 0) { ++left; }
			while(strcmp(base + m_policyOffsets[right], pivot) > 0) { --right; }
			if(left <= right) {
				u32 offset = m_policyOffsets[left];
				m_policyOffsets[left] = m_policyOffsets[right];
				m_policyOffsets[right] = offset;
				++left;
				--right;
			}
		}

		if(right > first) { sortPolicyOffsets(first, right); }
		first = left;
	} while(first < last);
}

s32
CKLBSoundPolicy::searchPolicy(const char* key, size_t length, s32 first, s32 last) const
{
	s32 result = -1;
	if(last < first) { return result; }

	char* base = m_policyText;
	for(;;) {
		s32 middle;
		s32 comparison;
		for(;;) {
			middle = (first + last) >> 1;
			comparison = strncmp(base + m_policyOffsets[middle], key, length);
			if(comparison < 1) { break; }
			last = middle - 1;
			if(middle <= first) { return -1; }
		}
		if(comparison >= 0) {
			result = middle;
			break;
		}
		++middle;
		if(last < middle) { break; }
		first = middle;
	}
	return result;
}

s32
CKLBSoundPolicy::findPolicy(const char* soundPath) const
{
	char name[1008];

	const u16* prefixLength = strncmp(soundPath, m_externalPrefix, m_externalPrefixLength)
		? &m_installPrefixLength
		: &m_externalPrefixLength;
	const char* unprefixed = soundPath + *prefixLength;
	const size_t fullLength = strlen(unprefixed);
	const size_t nameLength = fullLength - 4;
	memcpy(name, unprefixed, nameLength);
	name[nameLength] = 0;

	s32 result = -1;
	if(m_policyCount) {
		s32 last = m_policyCount - 1;
		char* base = m_policyText;
		s32 first = 0;
		for(;;) {
			s32 middle;
			s32 comparison;
			for(;;) {
				middle = (first + last) >> 1;
				comparison = strncmp(base + m_policyOffsets[middle], name, nameLength);
				if(comparison < 1) { break; }
				last = middle - 1;
				if(middle <= first) { return -1; }
			}
			if(comparison >= 0) {
				result = middle;
				break;
			}
			++middle;
			if(last < middle) { break; }
			first = middle;
		}
	}
	return result;
}

s32
CKLBSoundPolicy::parsePolicyField(const char* policy, s32 field)
{
	bool found = (field == 0);
	s32 sign;
	s32 value;
	if(field != 0) {
		char character = *policy;
		if(character) {
			s32 commaCount = 0;
			do {
				commaCount += (character == ',');
				const char* next = policy + 1;
				found = (commaCount == field);
				if(found) {
					policy = next;
					break;
				}
				character = policy[1];
				policy = next;
			} while(character);
		}
	}

	sign = 1;
	value = 0;
	if(found) {
		s32 started = 0;
		for(;;) {
			s32 character = *policy;
			if(static_cast<u32>(character - '0') < 10) {
				value = value * 10 + character - '0';
				++policy;
				started = 1;
			} else if(!character) {
				break;
			} else if(character == '-') {
				++policy;
				started = 1;
				sign = -1;
			} else if(started) {
				break;
			} else {
				++policy;
				started = 0;
			}
		}
	}
	return value * sign;
}

/*
 * Sound policy rows are null-terminated records in a comma-separated table.
 *
 * Numeric columns consumed by the runtime are:
 *   2: fade-in duration
 *   3: fade-out duration
 *   4: target volume percentage
 *   5: fade-in delay
 *   6: fade-out delay (-1 reuses the fade-in delay)
 *
 * Rows beginning with '/' are excluded from the searchable policy index.
 */
bool
CKLBSoundPolicy::loadPolicy(const char* policyPath)
{
	IPlatformRequest& platform = CPFInterface::getInstance().platform();
	platform.mutexLock(m_policyMutex);
	releasePolicyText();

	bool result = false;
	IReadStream* stream =
		CPFInterface::getInstance().platform().openReadStream(policyPath, true, (u32)-1);
	if(stream && stream->getStatus() == IReadStream::NORMAL && stream->getSize() > 0) {
		m_installPrefix = platform.getFullPath("file://install/");
		m_installPrefixLength = strlen(m_installPrefix);
		m_externalPrefix = platform.getFullPath("file://external/");
		m_externalPrefixLength = strlen(m_externalPrefix);

		s32 size = stream->getSize();
		m_policyText = KLBNEWA(char, size + 2);
		memset(m_policyText, 0, size + 2);
		stream->readBlock(m_policyText, size);

		for(s32 index = 0; index < size; ++index) {
			u8 character = m_policyText[index];
			if(character < ' ') {
				if(character == '\r' || character == '\n') {
					m_policyText[index] = '\0';
				} else {
					m_policyText[index] = ' ';
				}
			}
		}

		s32 betweenPolicies = 0;
		for(s32 index = 0; index < size; ++index) {
			if(betweenPolicies != 1) {
				if(betweenPolicies == 0) {
					betweenPolicies = 1;
					if(m_policyText[index] != '/' && m_policyCount < MAX_POLICIES) {
						m_policyOffsets[m_policyCount++] = index;
					}
				}
			} else {
				betweenPolicies = 1;
				if(!m_policyText[index]) {
					betweenPolicies = !m_policyText[index + 1];
				}
			}
		}

		sortPolicyOffsets();
		klb_assert(m_policyCount < MAX_POLICIES,
			"REACHED MAX POLICY TABLE LENGTH");
		KLBDELETE(stream);
		result = true;
	}

	platform.mutexUnlock(m_policyMutex);
	return result;
}

void
CKLBSoundPolicy::execute(u32 deltaT)
{
	IPlatformRequest& platform = CPFInterface::getInstance().platform();
	KLBAudioCommand* command = platform.popAudioCommand();
	while(command) {
		if(command->type == 1) {
			if(!command->soundEffect) {
				m_activeSound = command->asset;
			}
			CPFInterface::getInstance().platform().mutexLock(m_policyMutex);
			{
				s32 policyIndex = findPolicy(command->payload.soundPath);
				if(policyIndex >= 0) {
					u32 policyOffset = m_policyOffsets[policyIndex];
					const char* policy = m_policyText + policyOffset;
					Motion& motion = m_motions[m_motionCount++];
					motion.volume = 100 << 16;
					s16 delay = static_cast<s16>(parsePolicyField(policy, 5));
					motion.fadeInDelay = delay;
					s32 fadeOutDelay = parsePolicyField(policy, 6);
					motion.sound = command->voice;
					if(fadeOutDelay != -1) {
						delay = static_cast<s16>(fadeOutDelay);
					}
					motion.fadeOutDelay = delay;
					motion.fadeInDuration =
						static_cast<s16>(parsePolicyField(policy, 2));
					motion.fadeOutDuration =
						static_cast<s16>(parsePolicyField(policy, 3));
					motion.sound = command->voice;
					motion.finished = 0;
					motion.targetVolume =
						static_cast<s8>(parsePolicyField(policy, 4));
					motion.phase = 0;
					motion.elapsed = 0;
				}
			}
			CPFInterface::getInstance().platform().mutexUnlock(m_policyMutex);
		} else if(command->type == 2) {
			if(command->asset == m_activeSound) {
				m_activeSound = NULL;
			}
			Motion* found = NULL;
			for(s32 index = 0; index < m_motionCount; ++index) {
				Motion& motion = m_motions[index];
				if(motion.sound == command->voice) {
					found = &motion;
				}
			}
			if(found) {
				found->elapsed = 0;
				found->phase = 3;
			}
		} else if(command->type == 8) {
			if(!command->soundEffect && !m_activeSound) {
				m_activeSound = command->asset;
			}
		}
		command = platform.popAudioCommand();
	}

	s32 minimumVolume = 100 << 16;
	u16 count = m_motionCount;
	for(s32 index = count - 1; index >= 0; --index) {
		Motion& motion = m_motions[index];
		if(!motion.finished) {
			switch(motion.phase) {
			case 0:
				if(motion.elapsed >= motion.fadeInDelay) {
					motion.phase = 1;
					motion.step =
						((motion.targetVolume << 16) - (100 << 16)) /
						motion.fadeInDuration;
				}
				break;

			case 1:
				motion.volume += motion.step * static_cast<s32>(deltaT);
				if(motion.volume <= (motion.targetVolume << 16)) {
					motion.volume = motion.targetVolume << 16;
					motion.phase = 2;
					motion.step = 0;
				}
				break;

			case 3:
				if(motion.elapsed >= motion.fadeOutDelay) {
					motion.phase = 4;
					motion.step =
						((100 - motion.targetVolume) << 16) /
						motion.fadeOutDuration;
				}
				break;

			case 4:
				motion.volume += motion.step * static_cast<s32>(deltaT);
				if(motion.volume >= (100 << 16)) {
					motion.volume = 100 << 16;
					motion.phase = 5;
					motion.step = 0;
				}
				break;

			case 5:
				memcpy(&motion, &motion + 1,
					(m_motionCount - index - 1) * sizeof(Motion));
				--m_motionCount;
				break;
			}

			if(motion.volume < minimumVolume) {
				minimumVolume = motion.volume;
			}
		}
		motion.elapsed += deltaT;
	}

	if(m_activeSound) {
		platform.setAudioVolume(
			m_activeSound,
			static_cast<float>(minimumVolume >> 16) / 100.0f,
			true);
	}
}
