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
#include "CPFInterface.h"
#include "CUnZip.h"
#include "CKLBUtility.h"

CKLBSubThreadUnzip::CKLBSubThreadUnzip(const char * zipPath)
: CUnZip()
, m_abnormalFile(false)
, m_extractRoot(NULL)
{
	m_error = false;
	m_status = 0;
	m_zipPath = zipPath;
	m_extractFinish = false;
}

CKLBSubThreadUnzip::~CKLBSubThreadUnzip()
{
	if(m_zipPath) {
		delete [] m_zipPath;
	}
	m_zipPath = NULL;
}

bool
CKLBSubThreadUnzip::unCompress(const char * extractRoot)
{
	m_extractRoot = extractRoot;
	m_hThread = CPFInterface::getInstance().platform().createThread(ThreadExtractEntry, this);
	if(!m_hThread) {
		m_error = true;
		m_status = -6;
		return false;
	}
	return true;
}

s32
CKLBSubThreadUnzip::ThreadExtractEntry(void * hThread, void * data)
{
	(void)hThread;
	CKLBSubThreadUnzip * unzip = static_cast<CKLBSubThreadUnzip *>(data);
	unzip->Open(unzip->m_zipPath);
	if(!unzip->getStatus()) {
		unzip->m_error = true;
		unzip->m_status = -1;
		return 0;
	}
	return unzip->ThreadExtract(unzip->m_extractRoot);
}

s32
CKLBSubThreadUnzip::ThreadExtract(const char * extractRoot)
{
	while(readCurrentFileInfo()) {
		s32 status = extractCurrentFile(extractRoot);
		if(status != 0) {
			m_error = true;
			m_status = status;
			return 0;
		}
		if(!gotoNextFile()) { return 0; }
	}

	m_error = true;
	m_status = -2;
	return 0;
}

s32
CKLBSubThreadUnzip::extractCurrentFile(const char * extractRoot)
{
	m_targetPath = new char[strlen(extractRoot) + sizeof(m_currentPath) + 1];
	strcpy(m_targetPath, extractRoot);
	strcat(m_targetPath, m_currentPath);

	s32 result;
	do {
		result = -9;
		if(!CreateDirectoryReflex(m_targetPath)) { break; }

		if(m_currentPath[m_lenPath - 1] == '/') {
			m_finished_entry++;
			result = 0;
			break;
		}

		result = -3;
		if(unzOpenCurrentFile(m_hUnzip) != UNZ_OK) { break; }

		m_dwSizeWrite = 0;
		if(m_fileInfo.uncompressed_size != 0) {
			m_wrfile = CPFInterface::getInstance().platform().openTmpFile(m_targetPath);
			if(!m_wrfile) {
				unzCloseCurrentFile(m_hUnzip);
				result = -4;
				break;
			}

			unsigned char buffer[BUF_SIZE];
			unsigned long bytesRead;
			do {
				bytesRead = unzReadCurrentFile(m_hUnzip, buffer, sizeof(buffer));
				if(bytesRead) {
					m_dwSizeWrite += m_wrfile->writeTmp(buffer, bytesRead);
				}
			} while(bytesRead);

			if(m_wrfile) {
				result = m_wrfile->closeTmp();
				delete m_wrfile;
				if(result != 0) {
					m_wrfile = NULL;
					result = -5;
					break;
				}
				m_wrfile = NULL;
			}
			if(m_dwSizeWrite != m_fileInfo.uncompressed_size) {
				result = -5;
				break;
			}
		}

		result = -1;
		if(unzCloseCurrentFile(m_hUnzip) != UNZ_OK) { break; }

		char* targetPath = m_targetPath;
		unsigned long sizeWritten = m_dwSizeWrite;
		if(targetPath && sizeWritten == 0 && CKLBUtility::isFileExist(targetPath)) {
			if(CPFInterface::getInstance().platform().removeTmpFile(targetPath) != 0) {
				m_abnormalFile = true;
			}
		}
		m_finished_entry++;
		m_finished_file++;
		result = 0;
	} while(false);

	delete [] m_targetPath;
	m_targetPath = NULL;
	return result;
}

bool
CKLBSubThreadUnzip::isFinishExtract()
{
	if(m_extractFinish) { return true; }

	s32 status = 0;
	IPlatformRequest& platform = CPFInterface::getInstance().platform();
	if(platform.watchThread(m_hThread, &status)) {
		return false;
	}
	platform.deleteThread(m_hThread);
	m_hThread = NULL;
	m_extractFinish = true;
	if(m_abnormalFile) {
		CPFInterface::getInstance().platform().addExtMsg(
			"CKLBSubThreadUnzip:AbnormalFileSymbol", "true", false);
		m_error = true;
		m_status = -7;
	}
	return true;
}

bool
CKLBSubThreadUnzip::afterExtract(
	const char * extractPath, bool isDirectory, size_t size, bool forceRemove)
{
	if((extractPath && size == 0 && !isDirectory
		&& CKLBUtility::isFileExist(extractPath)) || forceRemove) {
		if(CPFInterface::getInstance().platform().removeTmpFile(extractPath) != 0) {
			m_abnormalFile = true;
		}
	}
	return true;
}
