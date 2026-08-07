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
#include "CLoveLiveEntrance.h"

CLoveLiveEntrance&
CLoveLiveEntrance::getInstance()
{
	static CLoveLiveEntrance instance;
	return instance;
}

CLoveLiveEntrance::CLoveLiveEntrance()
{
}

CLoveLiveEntrance::~CLoveLiveEntrance()
{
}

bool
CLoveLiveEntrance::initLocalSystem(CKLBAssetManager& /* assetManager */)
{
	return true;
}

void
CLoveLiveEntrance::localFinish()
{
}

bool
GameSetup(void)
{
	CLoveLiveEntrance& entrance = CLoveLiveEntrance::getInstance();
	CPFInterface& interface = CPFInterface::getInstance();
	interface.setClientRequest(&entrance);

	bool installed = interface.platform().registerFont(
		"MotoyaLMaru W3 mono",
		"file://install/MTLmr3m.ttf",
		true,
		true);
	klb_assert(installed,
		"[LoveLive base] GAME FONT MTLmr3m.ttf NOT INSTALLED");
	return true;
}
