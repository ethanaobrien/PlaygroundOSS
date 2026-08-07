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
#ifndef CLoveLiveEntrance_h
#define CLoveLiveEntrance_h

#include "CKLBGameApplication.h"

class CLoveLiveEntrance : public CKLBGameApplication
{
public:
	static CLoveLiveEntrance& getInstance();

	CLoveLiveEntrance();
	virtual ~CLoveLiveEntrance();

protected:
	virtual bool initLocalSystem(CKLBAssetManager& assetManager);
	virtual void localFinish();
};

#endif // CLoveLiveEntrance_h
