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
#pragma once

#include "CKLBTask.h"

class CLuaState;
class CKLBDebuggerContext;

class CKLBDebugTask : public CKLBTask
{
public:
    CKLBDebugTask();
    virtual ~CKLBDebugTask();

    static CKLBDebugTask* create(
        CKLBTask*            parentTask,
        TASK_PHASE           phase,
        CKLBDebuggerContext* debugger);

    virtual void execute(u32 deltaT);
    virtual bool initScript(CLuaState& lua);
};
