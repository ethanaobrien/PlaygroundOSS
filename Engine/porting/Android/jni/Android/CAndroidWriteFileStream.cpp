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
//  CAndroidWriteFileStream.cpp
//
//
#include <fcntl.h>
#include <sys/types.h> 
#include <sys/stat.h> 
#include <unistd.h>
#include <stdio.h>

#include "CAndroidPathConv.h"
#include "CAndroidWriteFileStream.h"

bool KLBCreateDirectories(const char * path);

CAndroidWriteFileStream::CAndroidWriteFileStream(bool encrypt)
: m_decrypter(0)
, m_fullpath(0)
, m_encrypt(encrypt)
, m_fp(0)
, m_eStat(CLOSED)
{}

CAndroidWriteFileStream::~CAndroidWriteFileStream()
{
    if(m_fp) fclose(m_fp);
    m_eStat = CLOSED;
    delete [] m_fullpath;
}

CAndroidWriteFileStream *
CAndroidWriteFileStream::openStream(const char * path, bool encrypt)
{
    CAndroidWriteFileStream * pStream = new CAndroidWriteFileStream(encrypt);

    // 論理パスを物理パスに変換する。解決できなければ書き込み先が無い。
    pStream->m_fullpath = 0;
    CKLBPathConv& pathconv = CKLBPathConv::getInstance();
    pStream->m_fullpath = pathconv.fullpath(path);
    if(!pStream->m_fullpath) {
        // ファイルセグメントが指定されていない
        pStream->m_eStat = CAN_NOT_WRITE;
        return pStream;
    }

    // 書き込み先の物理ディレクトリを用意してから、新規にファイルを開く。
    KLBCreateDirectories(pStream->m_fullpath);
    pStream->m_fp = fopen(pStream->m_fullpath, "wb");
    if(!pStream->m_fp) {
        pStream->m_eStat = CAN_NOT_WRITE;
        return pStream;
    }
    pStream->m_eStat = NORMAL;

    // 暗号ヘッダをファイル先頭に書き出し、同じ鍵で以降の書き込みを暗号化する。
    u8 header[16];
    u32 headerSize;
    pStream->m_decrypter.encryptSetup((const u8 *)path, header, &headerSize);
    fwrite(header, headerSize, 1, pStream->m_fp);
    pStream->m_decrypter.decryptSetup((const u8 *)path, header, &headerSize);
    pStream->m_decrypter.finishSetup(header + 4, 0);
    return pStream;
}

IWriteStream::ESTATUS
CAndroidWriteFileStream::getStatus()
{
    return m_eStat;
}

s32
CAndroidWriteFileStream::getPosition()
{
    return ftell(m_fp) - m_decrypter.getHeaderSize();
}

void
CAndroidWriteFileStream::writeU8(u8 value)
{
    if(m_encrypt) {
        m_decrypter.decryptBlck(&value, 1);
    }
    if(EOF == fputc(value, m_fp)) {
        m_eStat = CAN_NOT_WRITE;
    }
}

void
CAndroidWriteFileStream::writeU16(u16 value)
{
    u8 arr[2];
    arr[0] = (value >> 8) & 0xff;
    arr[1] = value & 0xff;
    if(m_encrypt) {
        m_decrypter.decryptBlck(arr, 2);
    }
    if(1 > fwrite(arr, 2, 1, m_fp)) {
        m_eStat = CAN_NOT_WRITE;
    }
}

void
CAndroidWriteFileStream::writeU32(u32 value)
{
    u8 arr[4];
    arr[0] = (value >> 24) & 0xff;
    arr[1] = (value >> 16) & 0xff;
    arr[2] = (value >> 8) & 0xff;
    arr[3] = value & 0xff;
    if(m_encrypt) {
        m_decrypter.decryptBlck(arr, 4);
    }
    if(1 > fwrite(arr, 4, 1, m_fp)) {
        m_eStat = CAN_NOT_WRITE;
    }
}

void
CAndroidWriteFileStream::writeFloat(float fval)
{
    if(m_encrypt) {
        m_decrypter.decryptBlck(&fval, sizeof(float));
    }
    if(1 > fwrite(&fval, sizeof(float), 1, m_fp)) {
        m_eStat = CAN_NOT_WRITE;
    }
}

void
CAndroidWriteFileStream::writeBlock(void* buffer, u32 byteSize)
{
    if(m_encrypt) {
        m_decrypter.decryptBlck(buffer, byteSize);
    }
    if(1 > fwrite(buffer, byteSize, 1, m_fp)) {
        m_eStat = CAN_NOT_WRITE;
    }
}
