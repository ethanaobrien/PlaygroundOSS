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
#include "KLBOpenSLAudioPlayer.h"
#include "CAndroidPathConv.h"
#include "ivorbisfile.h"
#include <unistd.h>

KLBOpenSLOldSoundAssetLoader::KLBOpenSLOldSoundAssetLoader()
{
	assets = (KLBOpenSLOldSoundAsset**) calloc(KLBOpenSLOldEngine::MAX_SOUND_ASSETS, sizeof(KLBOpenSLOldSoundAsset**));
}

KLBOpenSLOldSoundAssetLoader::~KLBOpenSLOldSoundAssetLoader()
{
	stopObservation();
	free(assets);
}

KLBOpenSLOldSoundAsset* KLBOpenSLOldSoundAssetLoader::openFile(char const *path, bool isFullBuffered)
{
    // DEBUG_PRINT("AUDIO; openFile");
    // DEBUG_PRINT("path: %s", path);
    const char* target_path = CKLBPathConv::getInstance().fullpath(path, ".ogg");
	KLBOpenSLOldSoundAsset* asset = NULL;
	if (access(target_path, F_OK) != -1) {
		asset = new KLBOpenSLOldSoundAsset(path, isFullBuffered);
	}
	delete [] target_path;
	return asset;
}

void KLBOpenSLOldSoundAssetLoader::startObservation() {
	if (observationThread == NULL)
	{
		observationThread = CPFInterface::getInstance().platform().createThread(KLBOpenSLOldSoundAssetLoader::FileObservationThreadParam, this);
	}
}

int KLBOpenSLOldSoundAssetLoader::FileObservationThreadParam(void * hThread, void * data)
{
	KLBOpenSLOldSoundAssetLoader* me = (KLBOpenSLOldSoundAssetLoader*)data;
	while( 1 )
	{
		for (int i = 0; i < KLBOpenSLOldEngine::MAX_SOUND_ASSETS; ++i)
		{
			// TODO: reuse unregistered assets
			KLBOpenSLOldSoundAsset* targetPtr = *(me->assets + i);
			if (targetPtr != NULL)
			{
				// DEBUG_PRINT("targetPtr: %d", targetPtr);
			}
		}

		usleep(30000); // sleep for 30ms
	}

	return 0; // exit
}

void KLBOpenSLOldSoundAssetLoader::registerAsset(KLBOpenSLOldSoundAsset *pAsset) {
	for (int i = 0; i < KLBOpenSLOldEngine::MAX_SOUND_ASSETS; ++i)
	{
		// TODO: reuse unregistered assets
		KLBOpenSLOldSoundAsset* targetPtr = *(assets + i);
		if (targetPtr == NULL)
		{
			targetPtr = pAsset;
			return;
		}
	}
	// buffer queue is full
	// TODO: remove unused asset from memory and retry
}

void KLBOpenSLOldSoundAssetLoader::stopObservation() {
	if (observationThread != NULL)
	{
		CPFInterface::getInstance().platform().deleteThread(observationThread);
		observationThread = NULL;
	}
}

void KLBOpenSLOldSoundAssetLoader::unregisterAsset(KLBOpenSLOldSoundAsset *pAsset) {
	for (int i = 0; i < KLBOpenSLOldEngine::MAX_SOUND_ASSETS; ++i)
	{
		// TODO: reuse unregistered assets
		KLBOpenSLOldSoundAsset* targetPtr = *(assets + i);
		if (targetPtr != NULL && targetPtr == pAsset)
		{
			targetPtr = NULL;
			return;
		}
	}
	// pointer not found
	// TODO: error handling
}

