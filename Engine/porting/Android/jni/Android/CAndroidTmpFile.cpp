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
//
//  CAndroidTmpFile.cpp
//  GameEngine
//
//
//
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h> 
#include <sys/stat.h>
#include "CAndroidPathConv.h"
#include "CAndroidTmpFile.h"
#include "CPFInterface.h"

bool KLBCreateDirectories(const char * path);

CAndroidTmpFile::CAndroidTmpFile(const char * path) : m_fullpath(0), m_closed(false)
{
    m_fullpath = CKLBPathConv::getInstance().fullpath(path);
    // 平成24年11月27日(火)
    // ファイルが存在しない場合に該当ファイルが生成されない事への対応と
    // 権限の付与を行いました。
    if(KLBCreateDirectories(m_fullpath)) {
        m_fd = open(m_fullpath, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
        CPFInterface::getInstance().platform().excludePathFromBackup(m_fullpath);
    }
}

CAndroidTmpFile::~CAndroidTmpFile()
{
    closeTmp();
    delete [] m_fullpath;
}

size_t
CAndroidTmpFile::writeTmp(void *ptr, size_t size)
{
    return (m_fd > 0) ? write(m_fd, ptr, size) : 0;
}

int
CAndroidTmpFile::closeTmp()
{
    if(!m_closed && m_fd > 0) {
        m_closed = true;
        return close(m_fd);
    }
    return -1;
}
