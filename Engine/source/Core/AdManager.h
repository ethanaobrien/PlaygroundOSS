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
#ifndef AdManager_h
#define AdManager_h

#include <list>
#include "CKLBLuaTask.h"

class CKLBAdManager;

// Platform-specific rewarded-ad service. Android provides the concrete
// implementation; this interface only records the target-proven service ABI.
class IAdManager
{
public:
	virtual ~IAdManager() {}
	virtual void onAdResult(int command, int parameter, const char* message);
	virtual void preloadAd(bool rewarded, const char* placement) = 0;
	virtual void showAd() = 0;
	static IAdManager* getInstance();
	static IAdManager* getInstance(CKLBAdManager* owner);
	static void release();

protected:
	explicit IAdManager(CKLBAdManager* owner) : m_owner(owner) {}
	CKLBAdManager* m_owner;
};

class CKLBAdManager : public CKLBLuaTask
{
friend class IAdManager;
private:
	typedef int (CKLBAdManager::*Command)(CLuaState& lua);

	struct AdResult {
		int			command;
		int			parameter;
		const char*	message;

		AdResult(int command, int parameter, const char* message);
		~AdResult();
	};

	Command					m_commands[2];
	const char*				m_callbackSuccess;
	const char*				m_callbackFailure;
	std::list<AdResult*>	m_results;
	IAdManager*				m_adService;

	CKLBAdManager();
	virtual ~CKLBAdManager();

	int cmdPreloadAd(CLuaState& lua);
	int cmdShowAd(CLuaState& lua);
	void onAdResult(int command, int parameter, const char* message);
	friend class CKLBTaskFactory<CKLBAdManager>;

public:
	u32 getClassID();
	bool initScript(CLuaState& lua);
	int commandScript(CLuaState& lua);
	void execute(u32 deltaT);
	void die();
};

#endif // AdManager_h
