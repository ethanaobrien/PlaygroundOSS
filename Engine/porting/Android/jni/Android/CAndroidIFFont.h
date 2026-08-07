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

#include "CPFInterface.h"

class CAndroidIFFont : public IFontIF
{
public:
    virtual void* getFont(int size, u32 type);
    virtual void deleteFont(void* font);
    virtual bool renderText(const char* text, void* font, u32 color,
                            u16 width, u16 height, u8* buffer,
                            s16 stride, s16 baseX, s16 baseY, u32 pixelBytes,
                            float scaleX, float scaleY);
    virtual bool getTextInfo(const char* text, void* font, STextInfo* info,
                             float scaleX, float scaleY);
};
