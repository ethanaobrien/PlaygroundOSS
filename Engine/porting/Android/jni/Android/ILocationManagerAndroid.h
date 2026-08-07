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
#ifndef ILOCATIONMANAGERANDROID_H
#define ILOCATIONMANAGERANDROID_H

#include "CKLBLocationManager.h"

class ILocationManagerAndroid : public ILocationManager
{
public:
    explicit ILocationManagerAndroid(CKLBLocationManager* owner)
        : ILocationManager(owner)
    {
    }

    virtual void requireLocation();
    virtual bool stopLocation();
    virtual int getPermissionStatus();
    virtual void requirePermission();

private:
    //! The shipped object is 24 bytes, not 16: ILocationManager::create
    //! asks operator new for 0x18 while the identical Android notification
    //! singleton asks for 0x10.  The pointer-width slot at +0x10 is never
    //! written by the constructor and never read by any body of this class,
    //! so its size is target-proven and its type and name are not.
    void* m_platformHandle;
};

#endif // ILOCATIONMANAGERANDROID_H
