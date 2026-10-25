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
#include <stdio.h>
#include <stdlib.h>
#include <new>
#include <limits.h>
#include <errno.h>
#include "CKLBDatabase.h"
#include "CPFInterface.h"
#include "encryptFile.h"
#include "DatabasePlatform.h"

namespace {
DatabasePlatform* gDatabasePlatform = NULL;
unsigned gOpenEncryptedFiles = 0;
struct DatabaseIOLock {
	DatabasePlatform* platform;
	DatabaseIOLock() : platform(gDatabasePlatform) { if (platform) platform->lockIO(); }
	~DatabaseIOLock() { if (platform) platform->unlockIO(); }
};
}
extern bool gEncryptedVfsReady;
bool registerDatabasePlatform(DatabasePlatform* platform) {
	if (gOpenEncryptedFiles || (platform && (gDatabasePlatform || gEncryptedVfsReady))) return false;
	gDatabasePlatform = platform;
	return true;
}
DatabasePlatform* databasePlatform() { return gDatabasePlatform; }
const char* resolveDatabaseOpenPath(const char* uri, int flags,
	bool promoteWritable, bool* readOnly) {
	DatabaseIOLock lock;
	bool ignored = false;
	if (gDatabasePlatform) {
		const char* result = gDatabasePlatform->resolve(uri, flags,
			promoteWritable, readOnly ? *readOnly : ignored);
		if (!result) CPFInterface::getInstance().platform().logging(
			"Database resolution failed for %s: %s", uri ? uri : "(temporary)",
			gDatabasePlatform->error());
		return result;
	}
	return CPFInterface::getInstance().platform().getFullPath(uri, readOnly);
}
#if defined(__WIIU__)
#include "Playground/WiiU/WiiUSqlite.h"
#endif

extern void KLBUnregisterObjectName(void* object, const char* className);
extern void KLBRegisterObjectName(void* object, const char* className, int flags);

// Get a private global symbol owning all the file systems of SQLite.
extern "C" {
	sqlite3_vfs*	gVfsList;
}

// Global Table of IO Methods for encrypted SQLite.
sqlite3_io_methods		gSQLiteEncryptIO;
int						gBaseSizeOsFile;
bool					gInitOnce = true;
bool					gEncryptedVfsReady = false;
int (*gOpenDefaultSQLite)	(sqlite3_vfs*, const char *zName, sqlite3_file*,int flags, int *pOutFlags);
#if defined(__WIIU__)
sqlite3_vfs			gWiiUEncryptedVfs;
sqlite3_vfs			gWiiUDurabilityVfs;
sqlite3_vfs*			gWiiUBaseVfs = NULL;
sqlite3_vfs*			gWiiUDurabilityBaseVfs = NULL;
const char*			gWiiUEncryptedVfsName = "playground-wiiu-encrypted";
#endif

#define	getFileDecrypt(a)			(WrapperFileDecrypt*)(&(((unsigned char*)a)[gBaseSizeOsFile]))

struct WrapperFileDecrypt {
	// Decrypt
	// File ptr
	// + Key, decrypt flag, ...
	CDecryptBaseClass	m_decrypt;
	int					m_size;
	void*				m_file;
	bool				m_no_op;
	u32					m_headerSize;
	char*                   m_path;
};

// === SQLite OS Methods ===
int fEncryptOpen					(sqlite3_vfs*, const char *zName, sqlite3_file*, int flags, int *pOutFlags);

#if defined(__WIIU__)
static int wiiuEncryptedFullPathname(sqlite3_vfs* vfs, const char* name,
	int outputSize, char* output)
{
	// WUT/newlib device paths are already absolute. SQLite's Unix VFS treats
	// the colon form as relative and prefixes the process working directory,
	// after which the engine's encrypted-file wrapper cannot reopen the asset.
	if (name && (strncmp(name, "fs:/", 4) == 0 ||
			   strncmp(name, "/vol/", 5) == 0)) {
		size_t length = strlen(name);
		if (length + 1 > (size_t)outputSize) {
			return SQLITE_CANTOPEN;
		}
		memcpy(output, name, length + 1);
		return SQLITE_OK;
	}
	sqlite3_vfs* base = vfs == &gWiiUDurabilityVfs
		? gWiiUDurabilityBaseVfs : gWiiUBaseVfs;
	return base->xFullPathname(base, name, outputSize, output);
}
#endif

// === SQLite IO Methods ===
int fEncryptClose					(sqlite3_file*);
int fEncryptRead					(sqlite3_file*, void*, int iAmt, sqlite3_int64 iOfst);
int fEncryptWrite					(sqlite3_file*, const void*, int iAmt, sqlite3_int64 iOfst);
int fEncryptTruncate				(sqlite3_file*, sqlite3_int64 size);
int fEncryptSync					(sqlite3_file*, int flags);
int fEncryptFileSize				(sqlite3_file*, sqlite3_int64 *pSize);
int fEncryptLock					(sqlite3_file*, int);
int fEncryptUnlock					(sqlite3_file*, int);
int fEncryptCheckReservedLock		(sqlite3_file*, int *pResOut);
int fEncryptFileControl				(sqlite3_file*, int op, void *pArg);
int fEncryptSectorSize				(sqlite3_file*);
int fEncryptDeviceCharacteristics	(sqlite3_file*);
int fEncryptShmMap					(sqlite3_file*, int iPg, int pgsz, int, void volatile**);
int fEncryptShmLock					(sqlite3_file*, int offset, int n, int flags);
void 
	fEncryptShmBarrier				(sqlite3_file*);
int fEncryptShmUnmap				(sqlite3_file*, int deleteFlag);

int fEncryptClose					(sqlite3_file* file)
{
	DatabaseIOLock lock;
	WrapperFileDecrypt* fileDecrypt = getFileDecrypt(file);
	if (!fileDecrypt->m_no_op) {
		if (file->pMethods) --gOpenEncryptedFiles;
		if (gDatabasePlatform) gDatabasePlatform->closing(&fileDecrypt->m_file);
		if (fileDecrypt->m_file)
			CPFInterface::getInstance().platform().ifclose(fileDecrypt->m_file);
		fileDecrypt->m_decrypt.~CDecryptBaseClass();
		free(fileDecrypt->m_path);
	}

	return SQLITE_OK;
}

int fEncryptRead					(sqlite3_file* file, void* buff, int iAmt, sqlite3_int64 iOfst)
{
	DatabaseIOLock lock;
	WrapperFileDecrypt* fileDecrypt = getFileDecrypt(file);
	if (!fileDecrypt->m_file) return SQLITE_IOERR_READ;
	if (iAmt < 0 || iOfst < 0 || iOfst > 0xffffffffLL ||
		iOfst + iAmt > 0x100000000LL ||
		iOfst + fileDecrypt->m_headerSize > LONG_MAX) return SQLITE_IOERR_SEEK;

	if (!fileDecrypt->m_no_op) {
		fileDecrypt->m_decrypt.gotoOffset((u32)iOfst);

		IPlatformRequest& pltf = CPFInterface::getInstance().platform();
		if (pltf.ifseek(fileDecrypt->m_file,
			(long)(iOfst + fileDecrypt->m_headerSize), SEEK_SET) != 0)
			return SQLITE_IOERR_SEEK;
		u32 readSize = pltf.ifread(buff, 1, iAmt, fileDecrypt->m_file);

		fileDecrypt->m_decrypt.decryptBlck(buff, readSize);

		if ((s32)readSize < iAmt) {
			/* Unread parts of the buffer must be zero-filled */
			memset(&((char*)buff)[readSize], 0, iAmt-readSize);
			return SQLITE_IOERR_SHORT_READ;
		}

		// printf("fEncryptRead Amount[%i] Offset[%i] \n", iAmt, iOfst);
	} else {
		// No op for read.
		return SQLITE_IOERR;
	}

	return SQLITE_OK;
}

int fEncryptWrite					(sqlite3_file* file, const void* data, int iAmt, sqlite3_int64 iOfst)
{
	DatabaseIOLock lock;
	WrapperFileDecrypt* fileDecrypt = getFileDecrypt(file);
	if (!fileDecrypt->m_file) return SQLITE_IOERR_WRITE;
	if (iAmt < 0 || iOfst < 0 || iOfst > 0xffffffffLL ||
		iOfst + iAmt > 0x100000000LL ||
		iOfst + fileDecrypt->m_headerSize > LONG_MAX) return SQLITE_IOERR_SEEK;

	if (!fileDecrypt->m_no_op) {
		IPlatformRequest& pltf = CPFInterface::getInstance().platform();
		int err = pltf.ifseek(fileDecrypt->m_file, (long)(iOfst + fileDecrypt->m_headerSize), SEEK_SET);

		if (!err) {
			if (iAmt != 0) {
				// Recompute key at offset.
				fileDecrypt->m_decrypt.gotoOffset((u32)iOfst);

				// Allocate encrypted buffer
				u8* buff = (u8*)malloc(iAmt);

				if (buff) {
					// Copy original buffer
					memcpy(buff,data,iAmt);
					// Encrypt Buffer. (NOTE : could optimize memcpy by having a SOURCE AND DEST BUFFER)
					fileDecrypt->m_decrypt.decryptBlck(buff, iAmt);
					const u32 written = pltf.ifwrite(buff, 1, iAmt, fileDecrypt->m_file);
					const int flushed = pltf.ifflush(fileDecrypt->m_file);
					free(buff);
					if (written != (u32)iAmt) return SQLITE_IOERR_WRITE;
					if (flushed != 0) return SQLITE_IOERR_FSYNC;
					if (gDatabasePlatform) gDatabasePlatform->written(fileDecrypt->m_file);
				} else {
					return SQLITE_NOMEM;
				}
			}
			return SQLITE_OK;
		}
			return SQLITE_IOERR_SEEK;
	} else {
		return SQLITE_OK;
	}
}

int fEncryptTruncate				(sqlite3_file* /*file*/, sqlite3_int64 /*size*/)
{
	return SQLITE_IOERR_TRUNCATE;
}

int fEncryptSync					(sqlite3_file* file, int /*flags*/)
{
	DatabaseIOLock lock;
	WrapperFileDecrypt* state = getFileDecrypt(file);
	if (state->m_no_op || !state->m_file) return SQLITE_IOERR_FSYNC;
	if (CPFInterface::getInstance().platform().ifflush(state->m_file) != 0)
		return SQLITE_IOERR_FSYNC;
	if (gDatabasePlatform) {
		const int result = gDatabasePlatform->sync(state->m_path, state->m_file);
		if (result != SQLITE_OK) CPFInterface::getInstance().platform().logging(
			"Database sync failed for %s: %s", state->m_path, gDatabasePlatform->error());
		return result;
	}
	return SQLITE_OK;
}

int fEncryptFileSize				(sqlite3_file* file, sqlite3_int64 *pSize)
{
	DatabaseIOLock lock;
	if (pSize) {
		WrapperFileDecrypt* fileDecrypt = getFileDecrypt(file);
		if (!fileDecrypt->m_file) return SQLITE_IOERR_FSTAT;
		if (!fileDecrypt->m_no_op) {
			IPlatformRequest& pltf = CPFInterface::getInstance().platform();
			int err = pltf.ifseek(fileDecrypt->m_file, 0, SEEK_END);
			if (err == 0) {
				const sqlite3_int64 physicalSize = pltf.iftell(fileDecrypt->m_file);
				if (physicalSize < fileDecrypt->m_headerSize) {
					return SQLITE_IOERR_FSTAT;
				}
				*pSize = physicalSize - fileDecrypt->m_headerSize;
			} else {
				return SQLITE_IOERR_FSTAT;
			}
		} else {
			*pSize = 0;
		}
		// printf("fEncryptFileSize %i \n", *pSize);
	}
	return SQLITE_OK;
}

int fEncryptLock					(sqlite3_file* /*file*/, int /*lock*/)
{
	// WrapperFileDecrypt* fileDecrypt = (WrapperFileDecrypt*)file;
	// printf("fEncryptLock %i \n", lock);
	return SQLITE_OK;
}

int fEncryptUnlock					(sqlite3_file* /*file*/, int /*unlock*/)
{
	// WrapperFileDecrypt* fileDecrypt = (WrapperFileDecrypt*)file;
	// printf("fEncryptUnLock %i \n", unlock);
	return SQLITE_OK;
}

int fEncryptCheckReservedLock		(sqlite3_file* /*file*/, int* pResOut)
{
	// WrapperFileDecrypt* fileDecrypt = (WrapperFileDecrypt*)file;
	*pResOut = 0;
	return SQLITE_OK;
}

int fEncryptFileControl				(sqlite3_file* /*file*/, int /*op*/, void* /*pArg*/)
{
	return SQLITE_NOTFOUND;
}

int fEncryptSectorSize				(sqlite3_file* /*file*/)
{
	return 4096; // = SQLITE_DEFAULT_SECTOR_SIZE internal value.
}

int fEncryptDeviceCharacteristics	(sqlite3_file* /*file*/)
{
	// printf("fEncryptDeviceCharacteristics %8X\n", SQLITE_IOCAP_UNDELETABLE_WHEN_OPEN | SQLITE_IOCAP_POWERSAFE_OVERWRITE);
	return 0;
}

int fEncryptShmMap					(sqlite3_file*, int /*iPg*/, int /*pgsz*/, int, void volatile**)
{
	return SQLITE_IOERR_SHMMAP;
}

int fEncryptShmLock					(sqlite3_file*, int /*offset*/, int /*n*/, int /*flags*/)
{
	return SQLITE_IOERR_SHMLOCK;
}

void fEncryptShmBarrier				(sqlite3_file*)
{
	// Do nothing.
}

int fEncryptShmUnmap				(sqlite3_file*, int /*deleteFlag*/)
{
	return SQLITE_OK;
}

int fEncryptOpen					(sqlite3_vfs* vfs, const char *zName, sqlite3_file* file, int flags, int *pOutFlags) {
	DatabaseIOLock lock;
	if (gDatabasePlatform) gDatabasePlatform->recordFileError(NULL, 0);
	WrapperFileDecrypt* fileDecrypt = getFileDecrypt(file);

	u32			dwFlagsAndAttributes = 0;
	u32			dwCreationDisposition;
	u32			dwDesiredAccess;
	u32			dwShareMode;
	// void *		zConverted;              /* Filename in OS encoding */
	const char*	zUtf8Name	= zName; /* Filename in UTF-8 encoding */
	int			isExclusive = (flags & SQLITE_OPEN_EXCLUSIVE);
	int			isDelete	= (flags & SQLITE_OPEN_DELETEONCLOSE);
	int			isCreate	= (flags & SQLITE_OPEN_CREATE);
	int			isReadonly	= (flags & SQLITE_OPEN_READONLY);
	int			isReadWrite	= (flags & SQLITE_OPEN_READWRITE);

	int			eType       = flags & 0xFFFFFF00;  /* Type of file to open */

	int			isOpenJournal = (isCreate && (	   eType == SQLITE_OPEN_MASTER_JOURNAL 
												|| eType == SQLITE_OPEN_MAIN_JOURNAL 
												|| eType == SQLITE_OPEN_TEMP_JOURNAL
												|| eType == SQLITE_OPEN_SUBJOURNAL
												|| eType == SQLITE_OPEN_WAL
												));

	if (isOpenJournal) {
		// No-Operation
		fileDecrypt->m_no_op	= true;
		fileDecrypt->m_file		= (void*)0xFFFFFFFF;
		// return SQLITE_OK;
		return SQLITE_ERROR;
	} else {
		fileDecrypt->m_no_op	= false;
	}

	/* Check the following statements are true:
	**
	**   (a) Exactly one of the READWRITE and READONLY flags must be set, and
	**   (b) if CREATE is set, then READWRITE must also be set, and
	**   (c) if EXCLUSIVE is set, then CREATE must also be set.
	**   (d) if DELETEONCLOSE is set, then CREATE must also be set.
	*/
	klb_assert((isReadonly == 0 || isReadWrite==0) && (isReadWrite || isReadonly), "SQLite Encrypt Wrapper");
	klb_assert(isCreate == 0    || isReadWrite, "SQLite Encrypt Wrapper");
	klb_assert(isExclusive == 0 || isCreate,    "SQLite Encrypt Wrapper");
	klb_assert(isDelete == 0    || isCreate,    "SQLite Encrypt Wrapper");

	/* The main DB, main journal, WAL file and master journal are never 
	** automatically deleted. Nor are they ever temporary files.  */
	klb_assert( (!isDelete && zName) || eType!=SQLITE_OPEN_MAIN_DB ,        "SQLite Encrypt Wrapper");
	klb_assert( (!isDelete && zName) || eType!=SQLITE_OPEN_MAIN_JOURNAL ,   "SQLite Encrypt Wrapper");
	klb_assert( (!isDelete && zName) || eType!=SQLITE_OPEN_MASTER_JOURNAL , "SQLite Encrypt Wrapper");
	klb_assert( (!isDelete && zName) || eType!=SQLITE_OPEN_WAL ,            "SQLite Encrypt Wrapper");

	/* Assert that the upper layer has set one of the "file-type" flags. */
	klb_assert( eType==SQLITE_OPEN_MAIN_DB  || eType==SQLITE_OPEN_TEMP_DB 
											|| eType==SQLITE_OPEN_MAIN_JOURNAL || eType==SQLITE_OPEN_TEMP_JOURNAL 
											|| eType==SQLITE_OPEN_SUBJOURNAL   || eType==SQLITE_OPEN_MASTER_JOURNAL 
											|| eType==SQLITE_OPEN_TRANSIENT_DB || eType==SQLITE_OPEN_WAL, "SQLite Encrypt Wrapper"
	);

	/* If the second argument to this function is NULL, generate a 
	** temporary file name to use 
	*/
	if( !zUtf8Name ){
		return SQLITE_CANTOPEN;
		/*
		rc = getTempname(MAX_PATH+2, zTmpname);
		if( rc!=SQLITE_OK ){
		return rc;
		}
		zUtf8Name = zTmpname;
		*/
	}

	/* Database filenames are double-zero terminated if they are not
	** URIs with parameters.  Hence, they can always be passed into
	** sqlite3_uri_parameter().
	*/
	klb_assert( (eType!=SQLITE_OPEN_MAIN_DB) || (flags & SQLITE_OPEN_URI) ||
				zUtf8Name[strlen(zUtf8Name)+1]==0, "SQLite Encrypt Wrapper" );

	/* Convert the filename to the system encoding. */
	/* Not used
	zConverted = convertUtf8Filename(zUtf8Name);
	if( zConverted==0 ){
	return SQLITE_IOERR_NOMEM;
	} */

#define GENERIC_READ		(0x1)
#define GENERIC_WRITE		(0x2)

#define CREATE_NEW					(0x001)
#define OPEN_ALWAYS					(0x002)
#define OPEN_EXISTING				(0x004)
#define FILE_SHARE_READ				(0x008)
#define FILE_SHARE_WRITE			(0x010)
#define FILE_ATTRIBUTE_TEMPORARY	(0x020)
#define FILE_ATTRIBUTE_NORMAL		(0x040)
#define FILE_ATTRIBUTE_HIDDEN		(0x080)
#define FILE_FLAG_DELETE_ON_CLOSE	(0x100)

	if( isReadWrite ){
		dwDesiredAccess = GENERIC_READ | GENERIC_WRITE;
	}else{
		dwDesiredAccess = GENERIC_READ;
	}

	/* SQLITE_OPEN_EXCLUSIVE is used to make sure that a new file is 
	** created. SQLite doesn't use it to indicate "exclusive access" 
	** as it is usually understood.
	*/
	if( isExclusive ){
		/* Creates a new file, only if it does not already exist. */
		/* If the file exists, it fails. */
		dwCreationDisposition = CREATE_NEW;
	}else if( isCreate ){
		/* Open existing file, or create if it doesn't exist */
		dwCreationDisposition = OPEN_ALWAYS;
	}else{
		/* Opens a file, only if it exists. */
		dwCreationDisposition = OPEN_EXISTING;
	}

	dwShareMode = FILE_SHARE_READ | FILE_SHARE_WRITE;

	if( isDelete ) {
	#if SQLITE_OS_WINCE
		dwFlagsAndAttributes = FILE_ATTRIBUTE_HIDDEN;
		isTemp = 1;
	#else
		dwFlagsAndAttributes = FILE_ATTRIBUTE_TEMPORARY
		| FILE_ATTRIBUTE_HIDDEN
		| FILE_FLAG_DELETE_ON_CLOSE;
	#endif
	}else{
		dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL;
	}

	/** Setup of wrapper for file system in all cases **/
	// Also call the original open.
	// Decided to DO NOT call the gOpenDefaultSQLite(vfs, zName, file, flags, pOutFlags);
	file->pMethods = NULL; // Publish methods only after a complete successful open.
	// TODO enable decrypt : 
	const char* openMode;
	if (isReadonly) {
		openMode = "rb";
	} else {
		openMode = "r+b";
	}

	IPlatformRequest& pltf = CPFInterface::getInstance().platform();
	errno = 0;
	fileDecrypt->m_file = NULL;
	if (!gDatabasePlatform || !gDatabasePlatform->openNative(zName, isReadonly != 0, &fileDecrypt->m_file))
		fileDecrypt->m_file = pltf.ifopen(zName, openMode);
	void* f = fileDecrypt->m_file;
	const int openError = errno;
	if (!f && gDatabasePlatform) gDatabasePlatform->recordFileError("open", openError);
	
		if (f) {
			if (pOutFlags)
				*pOutFlags = flags;
		new (&fileDecrypt->m_decrypt) CDecryptBaseClass(14);
		fileDecrypt->m_file	= f;
		fileDecrypt->m_path = (char*)malloc(strlen(zName) + 1);
		if (!fileDecrypt->m_path) {
			if (gDatabasePlatform) gDatabasePlatform->closing(&fileDecrypt->m_file);
			if (fileDecrypt->m_file) pltf.ifclose(fileDecrypt->m_file);
			fileDecrypt->m_decrypt.~CDecryptBaseClass();
			return SQLITE_NOMEM;
		}
		strcpy(fileDecrypt->m_path, zName);
		u8 header[4];
		header[0] = 0;
		header[1] = 0;
		header[2] = 0;
		header[3] = 0;
		if (pltf.ifseek(f, 0, SEEK_SET) != 0) {
			fEncryptClose(file);
			return SQLITE_IOERR_SEEK;
		}
		pltf.ifread(header,1,4,f);
		const char* keyPath = gDatabasePlatform ? gDatabasePlatform->keyPath(zName) : zName;
		fileDecrypt->m_decrypt.decryptSetup((const u8*)keyPath, header, &fileDecrypt->m_headerSize);
		if (fileDecrypt->m_headerSize >= 5) {
			u8 extendedHeader[128] = {};
			if (fileDecrypt->m_headerSize < 8 || fileDecrypt->m_headerSize > sizeof(extendedHeader) + 4 ||
				pltf.ifread(extendedHeader,
				1, fileDecrypt->m_headerSize - sizeof(header),
				f) != fileDecrypt->m_headerSize - sizeof(header)) {
				if (gDatabasePlatform) gDatabasePlatform->recordFileError("encrypted header read/validation", errno);
				fEncryptClose(file);
				return SQLITE_IOERR_SHORT_READ;
			}
			fileDecrypt->m_decrypt.finishSetup(extendedHeader, keyPath);
		}

		// Do not silently use a failed seek/tell as a zero-sized database.
		if (pltf.ifseek(f, 0, SEEK_END) != 0 ||
			pltf.iftell(f) < (long)fileDecrypt->m_headerSize ||
			pltf.ifseek(f, fileDecrypt->m_headerSize, SEEK_SET) != 0) {
			if (gDatabasePlatform) gDatabasePlatform->recordFileError("seek/size/header validation", errno);
			fEncryptClose(file);
			return SQLITE_IOERR_SEEK;
		}

		if (gDatabasePlatform) {
			const int result = gDatabasePlatform->opened(zName, &fileDecrypt->m_file, isReadonly != 0);
			if (result != SQLITE_OK) { fEncryptClose(file); return result; }
		}
		file->pMethods = &gSQLiteEncryptIO;
		++gOpenEncryptedFiles;
		return SQLITE_OK;
	} else {
		if (isCreate && (!isReadonly) && openError == ENOENT) {
			// Force empty file creation
			if (pltf.icreateEmptyFile(zName)) {
				// Call back itself with a working empty file.
				return fEncryptOpen(vfs, zName, file, flags & ~SQLITE_OPEN_CREATE, pOutFlags);
			}
			if (gDatabasePlatform) gDatabasePlatform->recordFileError("create", errno);
		}
	}
	return SQLITE_CANTOPEN;
}

bool initializeEncryptedSQLiteVFS() {
	if (gEncryptedVfsReady) {
		return true;
	}
	// 1. Need initialize first to init vfsList global variable containing the default fileSystem.
	if ((sqlite3_initialize() == SQLITE_OK) && gInitOnce) {
		gInitOnce = false;
		// 2. Init Struct with all function pointers.
		gSQLiteEncryptIO.iVersion					= 2;
		gSQLiteEncryptIO.xClose						= fEncryptClose;
		gSQLiteEncryptIO.xRead						= fEncryptRead;
		gSQLiteEncryptIO.xWrite						= fEncryptWrite;
		gSQLiteEncryptIO.xTruncate					= fEncryptTruncate;
		gSQLiteEncryptIO.xSync						= fEncryptSync;
		gSQLiteEncryptIO.xFileSize					= fEncryptFileSize;
		gSQLiteEncryptIO.xLock						= fEncryptLock;
		gSQLiteEncryptIO.xUnlock					= fEncryptUnlock;
		gSQLiteEncryptIO.xCheckReservedLock			= fEncryptCheckReservedLock;
		gSQLiteEncryptIO.xFileControl				= fEncryptFileControl;
		gSQLiteEncryptIO.xSectorSize				= fEncryptSectorSize;
		gSQLiteEncryptIO.xDeviceCharacteristics		= fEncryptDeviceCharacteristics;
		gSQLiteEncryptIO.xShmMap					= fEncryptShmMap;
		gSQLiteEncryptIO.xShmLock					= fEncryptShmLock;
		gSQLiteEncryptIO.xShmBarrier				= fEncryptShmBarrier;
		gSQLiteEncryptIO.xShmUnmap					= fEncryptShmUnmap;

		gVfsList = sqlite3_vfs_find(NULL);

		// 2.1 Patch the fileSystem xOpen function with custom function that wraps sqlist3_file*
		gOpenDefaultSQLite	= gVfsList->xOpen;
		// 2.2 Patch the fileSystem vfs->szOsFile, add our own structure. (need to keep original because we wrap open)
		gBaseSizeOsFile		= (gVfsList->szOsFile + 7) & 0xFFFFFFF8;	// Align 8 byte.
		#if defined(__WIIU__)
		// Register an independent default VFS so SQLite retains WUT's absolute
		// device paths while the engine continues to decrypt database pages via
		// its original IPlatformRequest wrapper.
		gWiiUBaseVfs = gVfsList;
		// WUT's devoptab does not provide POSIX advisory locks. The process is
		// the sole owner of its title/user directory, so unix-none retains real
		// rollback journals and fsync without failing every write transaction.
		gWiiUDurabilityBaseVfs = sqlite3_vfs_find("unix-none");
		if (!gWiiUDurabilityBaseVfs) {
			return false;
		}
		gWiiUDurabilityVfs = *gWiiUDurabilityBaseVfs;
		gWiiUDurabilityVfs.pNext = NULL;
		gWiiUDurabilityVfs.zName =
			playground::wiiu_runtime::wiiuDurabilityVfsName();
		gWiiUDurabilityVfs.xFullPathname = wiiuEncryptedFullPathname;
		if (sqlite3_vfs_register(&gWiiUDurabilityVfs, 0) != SQLITE_OK) {
			gInitOnce = true;
			return false;
		}
		gWiiUEncryptedVfs = *gVfsList;
		gWiiUEncryptedVfs.pNext = NULL;
		gWiiUEncryptedVfs.zName = gWiiUEncryptedVfsName;
		gWiiUEncryptedVfs.xFullPathname = wiiuEncryptedFullPathname;
		gWiiUEncryptedVfs.xOpen = fEncryptOpen;
		gWiiUEncryptedVfs.szOsFile =
			((gBaseSizeOsFile + sizeof(WrapperFileDecrypt)) + 7) & 0xFFFFFFF8;
		if (sqlite3_vfs_register(&gWiiUEncryptedVfs, 1) != SQLITE_OK) {
			sqlite3_vfs_unregister(&gWiiUDurabilityVfs);
			gInitOnce = true;
			return false;
		}
		gVfsList = &gWiiUEncryptedVfs;
		#else
		gVfsList->xOpen		= fEncryptOpen;
		gVfsList->szOsFile	= ((gBaseSizeOsFile + sizeof(WrapperFileDecrypt)) + 7) & 0xFFFFFFF8;
		#endif
		gEncryptedVfsReady = true;
		return true;
	} else {
		return false;
	}
}

static int callbackFct (void* ctx,int colNum,char** columnText,char** columnName) {
	return ((CKLBDatabase*)ctx)->callBack(colNum,columnText,columnName);
}

bool CKLBDatabase::init(const char* dbFile, int flags) {
	// At least initialize the encryption module for DBs, even without default DB.
	IPlatformRequest& platform = CPFInterface::getInstance().platform();
	if (platform.useEncryption()) {
		if (!initializeEncryptedSQLiteVFS()) {
			platform.logging("Could not initialize encrypted SQLite VFS");
			return false;
		}
	}

	if ((dbFile == NULL) || (m_dataBase && m_lastDB && strcmp(m_lastDB,dbFile) == 0)) {
		// Database is already known and was last open.
		return true;
	}

	// Else release old Database and old name
	_release();
	size_t size = strlen(dbFile);
	m_lastDB = KLBNEWA(char, size+1);

	if (m_lastDB) {
		// New name set
		memcpy(m_lastDB, dbFile, size);
		m_lastDB[size] = 0;

		// Convert Logical path to OS Dependant path
		bool isReadOnly = false;
		const char* fullPath = resolveDatabaseOpenPath(dbFile, flags, false, &isReadOnly);
		if (!fullPath || !fullPath[0]) {
			platform.logging("Database path could not be resolved: %s", dbFile);
			delete[] fullPath;
			_release();
			return false;
		}

		//int rc = sqlite3_open(fullPath, &m_dataBase);
		#if defined(__WIIU__)
		const char* sqlitePath = fullPath;
		const char* sqliteVfs =
			playground::wiiu_runtime::selectWiiUDatabaseVfs(
				dbFile, fullPath, isReadOnly);
		#else
		const char* sqlitePath = fullPath;
		const char* sqliteVfs = NULL;
		#endif
		int rc = sqlite3_open_v2(sqlitePath, &m_dataBase, flags, sqliteVfs);
		delete[] fullPath;

		if (rc) {
			klb_assertAlways("Can not open db %s", dbFile);
			_release();
			return false;
		}

		char * errMsg;
		m_pragmaJournal = true;
		rc = sqlite3_exec(m_dataBase, "PRAGMA journal_mode = OFF;", callbackFct, this, &errMsg);
		m_pragmaJournal = false;

		if (rc) {
			klb_assertAlways("Impossible to disable journal");
		}

		return true;
	} else {
		return false;
	}
}

void CKLBDatabase::_release() {
	if (m_dataBase) {
		sqlite3_close(m_dataBase);
		m_dataBase = NULL;
	}
	KLBDELETEA(m_lastDB);
	m_lastDB = NULL;
}

int CKLBDatabase::callBack(int /*colNum*/,char** columnText,char** /*columnName*/) {
	if (!m_pragmaJournal) {
		if (columnText[0]) {
			m_result = atoi(columnText[0]);
		} else {
			m_result = 0;
		}
	}
	return 0;
}

s32 CKLBDatabase::lookup(const char* table, const char* resultField, const char* className, const char* filterField, s32 filterValue) {
	char buffer[512];

	m_result = 0xDEADBEEF;

	if (className) {
		if (filterField) {
			sprintf(buffer, "SELECT %s FROM %s WHERE %s=%i AND class='%s';",resultField,table,filterField,filterValue,className);
		} else {
			sprintf(buffer, "SELECT %s FROM %s WHERE class='%s';",resultField,table,className); 
		}
	} else {
		if (filterField) {
			sprintf(buffer, "SELECT %s FROM %s WHERE %s=%i;",resultField,table,filterField,filterValue); 
		} else {
			sprintf(buffer, "SELECT %s FROM %s;",resultField,table /*,className */);
		}
	}

	if (runStatement(buffer)) {
		return m_result;
	} else {
		klb_assertAlways("lookup DB failure");
		return 0;	// 0 on failure.
	}
}

bool CKLBDatabase::runStatement(const char* SQLStatement) {
	char* errMsg;
	if (m_dataBase) {
		int rc = sqlite3_exec(m_dataBase, SQLStatement, callbackFct, this, &errMsg);
		if (rc!=SQLITE_OK) {
			klb_assertAlways("DB Error : %s", errMsg);
			sqlite3_free(errMsg);
			return false;
		}
		return true;
	} else {
		klb_assertAlways("DB Not open");
		return false;
	}
}

CKLBDatabase::CKLBDatabase()
:m_dataBase		(NULL)
,m_lastDB		(NULL)
{
	KLBRegisterObjectName(this, "CKLBDatabase", 0);
}

CKLBDatabase::~CKLBDatabase() {
	KLBUnregisterObjectName(this, "CKLBDatabase");
	_release();
}
