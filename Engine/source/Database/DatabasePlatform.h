#ifndef PLAYGROUND_DATABASE_PLATFORM_H
#define PLAYGROUND_DATABASE_PLATFORM_H

#include "sqlite3.h"

// Optional host-owned database boundary. Register before opening any database;
// keep the object alive until every connection is closed. Returned paths use
// the same new[] ownership convention as IPlatformRequest::getFullPath.
class DatabasePlatform {
public:
    virtual ~DatabasePlatform() {}
    virtual const char* resolve(const char* uri, int flags,
                               bool promoteWritable, bool& readOnly) = 0;
    virtual int sync(const char* nativePath, void* stream) = 0;
    // Return true when the host owns native stream acquisition (including
    // failure, reported by a null slot and errno). closing() must release it
    // and clear the slot. All accesses are serialized and seek absolutely.
    virtual bool openNative(const char*, bool, void**) { return false; }
    // A host may suspend native handles at an archive commit boundary while
    // keeping SQLite connections and cipher state alive. The slot is valid
    // until closing(); replacement streams must retain position and access.
    // These callbacks and I/O run under lockIO()/unlockIO().
    virtual int opened(const char*, void**, bool) { return SQLITE_OK; }
    virtual void closing(void**) {}
    // Serialize native I/O with host archive commits. Defaults preserve the
    // established behavior for hosts without a commit coordinator.
    virtual void lockIO() {}
    virtual void unlockIO() {}
    virtual void recordFileError(const char*, int) {}
    // A write reached the native stream; xSync marks the next consistent point.
    virtual void written(void*) {}
    virtual const char* error() const = 0;
    virtual const char* keyPath(const char* nativePath) const { return nativePath; }
};

// Registration does not transfer ownership; a second active registration fails.
bool registerDatabasePlatform(DatabasePlatform* platform);
DatabasePlatform* databasePlatform();
const char* resolveDatabaseOpenPath(const char* uri, int flags,
                                   bool promoteWritable, bool* readOnly);

#endif
