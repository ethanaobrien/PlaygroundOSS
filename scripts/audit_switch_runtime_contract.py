#!/usr/bin/env python3
"""Fail closed when the linked Switch runtime regresses to discarded paths."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--nm", required=True, type=Path)
    args = parser.parse_args()

    repository = Path(__file__).resolve().parents[1]
    generic_database_source = "\n".join(
        (repository / path).read_text(encoding="utf-8-sig")
        for path in (
            "Engine/source/Database/CKLBDatabase.cpp",
            "Engine/source/Database/CKLBLuaDB.cpp",
        ))
    generic_forbidden = (
        "Playground/Switch/",
        "pgstate",
        "pgcache",
        "file://state/",
        "file://external/",
        "basic_setting",
        "new_basic_settings",
        "unit_list",
        "commitSwitchDatabaseStorage",
    )
    generic_leaks = [value for value in generic_forbidden
                     if value in generic_database_source]
    if generic_leaks:
        raise SystemExit(
            "generic database engine contains Switch/game storage policy: " +
            ", ".join(generic_leaks))
    if ("DatabaseOpenMode" not in generic_database_source or
            "flags |= SQLITE_OPEN_CREATE" in generic_database_source or
            "flags &= ~SQLITE_OPEN_READONLY" in generic_database_source):
        raise SystemExit(
            "database frontends must preserve caller flags and pass explicit "
            "open intent to the platform")

    switch_policy_source = "\n".join(
        path.read_text(encoding="utf-8-sig")
        for root in (
            repository / "Engine/modern/runtime/switch",
            repository / "Engine/modern/host/switch",
        )
        for path in root.rglob("*")
        if path.suffix in {".c", ".cpp", ".h"})
    switch_policy_forbidden = (
        "basic_setting",
        "new_basic_settings",
        "unit_list",
        "asset://db/",
    )
    switch_policy_leaks = [value for value in switch_policy_forbidden
                           if value in switch_policy_source]
    if switch_policy_leaks:
        raise SystemExit(
            "Switch platform contains game database policy: " +
            ", ".join(switch_policy_leaks))
    database_literals = sorted(set(re.findall(
        r'"[^"\n]*\.(?:db_?|sqlite|sqlite3)"', switch_policy_source,
        flags=re.IGNORECASE)))
    if database_literals:
        raise SystemExit(
            "Switch platform contains hard-coded database filenames: " +
            ", ".join(database_literals))

    switch_path_source = (
        repository / "Engine/modern/runtime/switch/src/SwitchPath.cpp"
    ).read_text(encoding="utf-8-sig")
    if ("options.mapStoragePath = "
            "playground::switch_runtime::encodeStoragePath" not in
            switch_policy_source or
            "ComponentLimit = 64" not in switch_path_source or
            'ReservedPrefix = "~pg"' not in switch_path_source or
            "base64UrlEncode" not in switch_path_source or
            "base64UrlDecode" not in switch_path_source or
            "listStorageDirectory" not in switch_path_source):
        raise SystemExit(
            "Switch storage must losslessly encode Horizon-incompatible "
            "components and decode directory listings without changing the "
            "logical application namespace")

    runtime_platform_source = (
        repository / "Engine/modern/runtime/src/RuntimePlatform.cpp"
    ).read_text(encoding="utf-8-sig")
    shared_database_uris = sorted(set(re.findall(
        r'"(?:asset|file)://[^"\n]*(?:/db/|\.(?:db_?|sqlite|sqlite3))[^"\n]*"',
        generic_database_source + "\n" + runtime_platform_source,
        flags=re.IGNORECASE)))
    if shared_database_uris:
        raise SystemExit(
            "engine/runtime contains a game-owned database URI: " +
            ", ".join(shared_database_uris))
    if ("asset://db/unit/unit.db_" in runtime_platform_source or
            "m_integrityDatabaseUri" not in runtime_platform_source):
        raise SystemExit(
            "the shared runtime must consume game-owned integrity subjects "
            "from build configuration")
    root_cmake = (repository / "CMakeLists.txt").read_text(encoding="utf-8-sig")
    if not re.search(
            r'set\(PLAYGROUND_APP_INTEGRITY_DATABASE_URI\s+""\s+CACHE\s+STRING',
            root_cmake):
        raise SystemExit(
            "the engine must not provide a game database as its default "
            "integrity subject")
    state_store_source = (
        repository / "Engine/modern/runtime/src/RuntimeStateStore.cpp"
    ).read_text(encoding="utf-8-sig")
    switch_directory_sync = re.search(
        r"#elif defined\(__SWITCH__\)(.*?)#else",
        state_store_source[state_store_source.find(
            "bool syncParentDirectory("):],
        flags=re.DOTALL,
    )
    if (not switch_directory_sync or
            "return true;" not in switch_directory_sync.group(1) or
            "O_DIRECTORY" in switch_directory_sync.group(1)):
        raise SystemExit(
            "Switch state publication must not open unsupported directory "
            "descriptors")
    restrict_begin = runtime_platform_source.find(
        "void restrictStateFile(")
    restrict_end = runtime_platform_source.find(
        "class DesktopReadStream", restrict_begin)
    restrict_source = runtime_platform_source[restrict_begin:restrict_end]
    if (restrict_begin < 0 or restrict_end < 0 or
            "!defined(__SWITCH__)" not in restrict_source):
        raise SystemExit(
            "Switch state files must not call unsupported chmod")
    database_path_begin = runtime_platform_source.find(
        "std::string RuntimePlatform::resolveDatabasePath(")
    database_path_end = runtime_platform_source.find(
        "IReadStream *RuntimePlatform::openReadStream(", database_path_begin)
    database_path_source = runtime_platform_source[
        database_path_begin:database_path_end]
    copy_file = database_path_source.find("std::filesystem::copy_file")
    publish_rename = database_path_source.find("std::filesystem::rename",
                                               copy_file)
    archive_commit = database_path_source.find("m_commitState()",
                                               publish_rename)
    if (database_path_begin < 0 or database_path_end < 0 or
            "mode != DatabaseOpenMode::ReadOnly" not in database_path_source or
            "m_stateRoot" not in database_path_source or
            "m_storageMutationMutex" not in database_path_source or
            "resolvePath(input, readOnly)" not in database_path_source or
            ".pg-copyup-" not in database_path_source or
            min(copy_file, publish_rename, archive_commit) < 0 or
            not copy_file < publish_rename < archive_commit or
            "std::filesystem::remove(destination" not in
            database_path_source):
        raise SystemExit(
            "database path resolution must use explicit open intent for "
            "atomic, durable account-state copy-up and preserve read-only "
            "overlay resolution")
    for forbidden in (
            ".db\"", ".sqlite\"", "basic_setting", "unit_list",
            "relative.find('/')", "relative.find(\"/\")"):
        if forbidden in database_path_source:
            raise SystemExit(
                "database path resolution contains an ownership classifier: " +
                forbidden)
    if ('return ":memory:";' not in database_path_source or
            database_path_source.count('return ":memory:";') != 1 or
            "if (resolved.empty())" in database_path_source):
        raise SystemExit(
            "only explicitly unnamed Switch databases may use SQLite's "
            "connection-local in-memory behavior")
    if generic_database_source.count(
            'Database path could not be resolved: %s') != 2:
        raise SystemExit(
            "named database frontends must fail closed when path resolution "
            "does not find an object")
    game_application_source = (
        repository / "Engine/source/Core/CKLBGameApplication.cpp"
    ).read_text(encoding="utf-8-sig")
    embedded_default_database_uris = sorted(set(re.findall(
        r'"(?:asset|file)://[^"\n]*(?:/db/|\.(?:db_?|sqlite|sqlite3))[^"\n]*"',
        game_application_source, flags=re.IGNORECASE)))
    if (embedded_default_database_uris or
            "setDefaultDatabaseUri" not in game_application_source or
            "ENGINE_USE_DEFAULTDB requires an application-supplied "
            "database URI" not in game_application_source):
        raise SystemExit(
            "the engine default-database feature must consume an explicit "
            "application URI and contain no game database identity")
    rename_begin = runtime_platform_source.find(
        "int RuntimePlatform::irename(")
    rename_end = runtime_platform_source.find(
        "const char *RuntimePlatform::getBundleVersion", rename_begin)
    rename_source = runtime_platform_source[rename_begin:rename_end]
    lower_commit = rename_source.find("m_commitContent")
    native_rename = rename_source.find("std::rename", lower_commit)
    if (rename_begin < 0 or rename_end < 0 or
            min(lower_commit, native_rename) < 0 or not (
                lower_commit < native_rename) or
            "cleanup failed before rename" not in rename_source):
        raise SystemExit(
            "external overlay rename must durably clean lower names before "
            "moving authoritative account state")
    for required in (
            "bool RuntimePlatform::listFiles(",
            "collect(physical(joinUriPath(m_stateRoot, relative)))",
            "collect(physical(joinUriPath(m_contentRoot, relative)))",
            "(stateExists && !removePhysical(state))",
            "(contentExists && !removePhysical(content))"):
        if required not in runtime_platform_source:
            raise SystemExit(
                "external overlay contract is incomplete: " + required)

    encrypted_vfs_source = (
        repository / "Engine/source/Database/CKLBDatabase.cpp"
    ).read_text(encoding="utf-8-sig")
    close_candidates = [match.start() for match in re.finditer(
        r"int fEncryptClose", encrypted_vfs_source)]
    close_begin = close_candidates[-1] if close_candidates else -1
    close_end = encrypted_vfs_source.find("int fEncryptRead", close_begin)
    close_source = encrypted_vfs_source[close_begin:close_end]
    close_file = close_source.find("platform().ifclose")
    close_commit = close_source.find("commitDatabasePath")
    if (close_begin < 0 or close_end < 0 or close_file < 0 or
            close_commit < 0 or close_file > close_commit or
            "platform.ifsync" not in close_source):
        raise SystemExit(
            "dirty Switch databases must sync the file and close before the "
            "archive commit")
    write_stream_begin = runtime_platform_source.find(
        "~DesktopWriteStream() override")
    write_stream_end = runtime_platform_source.find(
        "ESTATUS getStatus() override", write_stream_begin)
    write_stream_close = runtime_platform_source[
        write_stream_begin:write_stream_end]
    if (write_stream_begin < 0 or write_stream_end < 0 or
            "if (!m_failed && m_commit" not in write_stream_close):
        raise SystemExit(
            "failed output streams must not publish a partial file")
    shutdown_begin = runtime_platform_source.find(
        "void RuntimePlatform::shutdownNetwork()")
    shutdown_end = runtime_platform_source.find(
        "CurlObjectInternal *RuntimePlatform::createNetworkOperation()",
        shutdown_begin)
    shutdown_source = runtime_platform_source[shutdown_begin:shutdown_end]
    shutdown_order = tuple(shutdown_source.find(value) for value in (
        "sharedSwitchNetwork().shutdownAndWait(",
        "CurlObjectInternal::flushCookieStorage(",
        "CurlObjectInternal::shutdownLibrary()",
    ))
    if (shutdown_begin < 0 or shutdown_end < 0 or any(
            position < 0 for position in shutdown_order) or not (
                shutdown_order[0] < shutdown_order[1] < shutdown_order[2])):
        raise SystemExit(
            "Switch network teardown must quiesce transfer leases before "
            "cookie flush and curl-global cleanup")

    switch_host_source = (
        repository / "Engine/modern/host/switch/tools/switch_engine_main.cpp"
    ).read_text(encoding="utf-8-sig")
    bootstrap_source = (
        repository / "Engine/modern/bootstrap/src/AssetBootstrap.cpp"
    ).read_text(encoding="utf-8-sig")
    switch_storage_source = (
        repository / "Engine/modern/runtime/switch/src/SwitchStorage.cpp"
    ).read_text(encoding="utf-8-sig")
    if ("options.maximumInternalNameLength = 64" not in switch_host_source or
            "options.resetReplaceableStorage" not in switch_host_source or
            'CompactStagingName = ".staging"' not in bootstrap_source or
            "layout.generation = options.metadata.sha256" not in
            bootstrap_source):
        raise SystemExit(
            "Switch asset bootstrap must retain the full SHA-256 while "
            "keeping its own staging/generation names within Horizon's "
            "hardware-tested path budget")
    reset_begin = switch_storage_source.find(
        "bool SwitchStorage::resetCache(")
    reset_end = switch_storage_source.find(
        "bool SwitchStorage::removeCacheTree(", reset_begin)
    reset_source = switch_storage_source[reset_begin:reset_end]
    reset_order = tuple(reset_source.find(value) for value in (
        "fsdevUnmountDevice(CacheMount)",
        "deleteCacheStorage(CacheIndex)",
        "createCacheStorage(applicationId)",
        "fsdevMountCacheStorage(CacheMount",
        "createRoots(m_roots, error)",
        "commitCache(error)",
    ))
    if (reset_begin < 0 or reset_end < 0 or any(
            position < 0 for position in reset_order) or not all(
                left < right for left, right in zip(
                    reset_order, reset_order[1:]))):
        raise SystemExit(
            "Switch replaceable-storage recovery must unmount, delete, "
            "recreate, remount, initialize and commit CacheStorage in order")
    if ("m_stateMounted = false" in reset_source or
            "fsdevUnmountDevice(StateMount)" in reset_source):
        raise SystemExit(
            "CacheStorage recovery must never reset selected-account SaveData")
    release_begin = switch_host_source.find(
        "void releasePlatformResources(EngineContext &context)")
    release_end = switch_host_source.find(
        "bool prepareStorageAndAssets(", release_begin)
    release_source = switch_host_source[release_begin:release_end]
    release_order = tuple(release_source.find(value) for value in (
        "sharedSwitchNetwork().shutdownAndWait(",
        "context.platform.reset()",
        "context.storage.unmount(",
    ))
    if (release_begin < 0 or release_end < 0 or any(
            position < 0 for position in release_order) or not (
                release_order[0] < release_order[1] < release_order[2])):
        raise SystemExit(
            "Switch host teardown must quiesce transfer leases before "
            "platform destruction and storage unmount")
    pause_begin = switch_host_source.find(
        "void setPaused(EngineContext &context, bool paused)")
    pause_end = switch_host_source.find("bool onStart(", pause_begin)
    pause_source = switch_host_source[pause_begin:pause_end]
    uninitialized = pause_source.find("if (!context.initialized)")
    bootstrap_state_commit = pause_source.find(
        "context.storage.commitState", uninitialized)
    bootstrap_cache_commit = pause_source.find(
        "context.storage.commitCache", uninitialized)
    initialized_client = pause_source.find(
        "CPFInterface &interface", uninitialized)
    if (pause_begin < 0 or pause_end < 0 or min(
            uninitialized, bootstrap_state_commit, bootstrap_cache_commit,
            initialized_client) < 0 or not (
                uninitialized < bootstrap_state_commit <
                bootstrap_cache_commit < initialized_client)):
        raise SystemExit(
            "Switch bootstrap HOME/sleep handling must commit mounted storage "
            "before the initialized engine lifecycle exists")

    switch_network_source = (
        repository / "Engine/modern/runtime/switch/src/SwitchNetwork.cpp"
    ).read_text(encoding="utf-8-sig")
    submit = switch_network_source.find("nifmRequestSubmitAndWait(&g_request)")
    request_result = switch_network_source.find(
        "nifmLaHandleNetworkRequestResult(&g_request)", submit)
    if submit < 0 or request_result < 0 or request_result < submit:
        raise SystemExit(
            "Switch network must run Horizon's application network-result "
            "flow after submitting its request")

    switch_notifications_source = (
        repository /
        "Engine/modern/runtime/switch/src/SwitchNotifications.cpp"
    ).read_text(encoding="utf-8-sig")
    consume_begin = switch_notifications_source.find(
        "bool consumeTriggeredNotifications(")
    consume_end = switch_notifications_source.find(
        "\n}\n\n} // namespace playground::switch_runtime", consume_begin)
    consume_source = switch_notifications_source[consume_begin:consume_end]
    if switch_notifications_source.count(
            'resultError("notifLoadApplicationParameter"') < 2:
        raise SystemExit(
            "Switch alarm lookup and expiry cleanup must report application-"
            "parameter IPC failures instead of treating them as foreign tags")
    event_get = consume_source.find("notifGetNotificationSystemEvent")
    event_poll = consume_source.find("eventWait(&available, 0)", event_get)
    pop = consume_source.find(
        "notifTryPopNotifiedApplicationParameter", event_poll)
    if (consume_begin < 0 or consume_end < 0 or
            min(event_get, event_poll, pop) < 0 or not (
                event_get < event_poll < pop) or
            "KernelError_TimedOut" not in consume_source or
            'resultError("notifTryPopNotifiedApplicationParameter"' not in
            consume_source):
        raise SystemExit(
            "Switch notification consumption must poll the channel event, "
            "treat only timeout as an empty queue, and report pop failures")

    switch_audio_source = (
        repository /
        "Engine/modern/runtime/switch/src/SwitchAudioOutput.cpp"
    ).read_text(encoding="utf-8-sig")
    wait = switch_audio_source.find("audoutWaitPlayFinish")
    timeout = switch_audio_source.find("KernelError_TimedOut", wait)
    wait_failure = switch_audio_source.find(
        '"audoutWaitPlayFinish failed', timeout)
    start_failure = switch_audio_source.find(
        "if (R_FAILED(audoutStartAudioOut()))")
    service_close = switch_audio_source.find("audoutExit();", start_failure)
    memory_release = switch_audio_source.find(
        "releaseAllocations();", start_failure)
    if (min(wait, timeout, wait_failure, start_failure, service_close,
           memory_release) < 0 or not (
               wait < timeout < wait_failure and
               start_failure < service_close < memory_release)):
        raise SystemExit(
            "Switch audio must distinguish wait timeout from service failure "
            "and close Horizon before freeing submitted buffers")
    resume_begin = switch_network_source.find("bool SwitchNetwork::resume(")
    resume_end = switch_network_source.find(
        "void SwitchNetwork::shutdownAndWait()", resume_begin)
    clear_suspend = "m_suspendRequested.store(false"
    if (switch_network_source.count(clear_suspend) != 1 or not (
            resume_begin >= 0 and resume_end >= 0 and
            clear_suspend in switch_network_source[resume_begin:resume_end])):
        raise SystemExit(
            "only explicit Switch lifecycle resume may clear network "
            "suspension")

    switch_notification_source = (
        repository /
        "Engine/modern/runtime/switch/src/SwitchNotifications.cpp"
    ).read_text(encoding="utf-8-sig")
    for operation in ("findTag", "deleteExpired"):
        begin = switch_notification_source.find("bool " + operation + "(")
        end = switch_notification_source.find("\n}\n", begin)
        body = switch_notification_source[begin:end]
        if begin < 0 or "sameUser(settings[index].uid, user)" not in body:
            raise SystemExit(
                "Switch notification management must be scoped to the "
                "launch-selected account: " + operation)
    if "notifUpdateAlarmSetting(&setting" not in switch_notification_source:
        raise SystemExit(
            "Switch notification replacement must update an existing alarm "
            "without a delete/register gap")

    switch_system_source = (
        repository / "Engine/modern/runtime/switch/src/SwitchSystem.cpp"
    ).read_text(encoding="utf-8-sig")
    motion_read_begin = switch_system_source.find("bool readMotionAngles(")
    motion_read_end = switch_system_source.find(
        "bool showSoftwareKeyboard(", motion_read_begin)
    motion_read_source = switch_system_source[motion_read_begin:motion_read_end]
    if (motion_read_begin < 0 or motion_read_end < 0 or
            "HidSixAxisSensorAttribute_IsConnected" not in motion_read_source or
            "stopMotionSensors();" not in motion_read_source or
            "startMotionSensors()" not in motion_read_source):
        raise SystemExit(
            "Switch motion input must reject and reacquire disconnected HID "
            "handles")

    switch_album_source = (
        repository / "Engine/modern/runtime/switch/src/SwitchAlbum.cpp"
    ).read_text(encoding="utf-8-sig")
    selected_album_user = switch_album_source.find(
        "accountGetPreselectedUser(&user)")
    user_album_save = switch_album_source.find(
        "capssuSaveScreenShotWithUserIds(", selected_album_user)
    if selected_album_user < 0 or user_album_save < selected_album_user:
        raise SystemExit(
            "Switch application screenshots must be associated with the "
            "launch-selected account")

    switch_input_source = (
        repository / "Engine/modern/host/switch/src/SwitchHost.c"
    ).read_text(encoding="utf-8-sig")
    if ("host->padConnected" not in switch_input_source or
            "PLAYGROUND_SWITCH_POINTER_CANCEL" not in switch_input_source):
        raise SystemExit(
            "Switch controller disconnect must cancel a synthetic held touch")

    image = args.elf.read_bytes()
    required_strings = (
        b"unix-none",
        b"playground-switch-paths",
        b"playground-encrypted-assets",
        b"pgstate",
        b"pgcache",
    )
    forbidden_strings = (
        b"playground-switch-durability",
        b"yuzu 1734",
        b"Yuzu 1734",
        b"SQLite VFS read failed",
        b"SQLite VFS write failed",
    )
    missing_strings = [value.decode() for value in required_strings
                       if value not in image]
    present_forbidden = [value.decode() for value in forbidden_strings
                         if value in image]
    if missing_strings:
        raise SystemExit("Switch runtime strings are missing: " +
                         ", ".join(missing_strings))
    if present_forbidden:
        raise SystemExit("discarded Switch compatibility remains linked: " +
                         ", ".join(present_forbidden))

    symbols = subprocess.run(
        [str(args.nm), "-C", str(args.elf)], check=True, text=True,
        capture_output=True).stdout
    required_symbols = (
        "initializeEncryptedSQLiteVFS()",
        "shutdownEncryptedSQLiteVFS()",
        "playground::runtime::RuntimePlatform::getDatabasePath(",
        "playground::runtime::RuntimePlatform::commitDatabasePath(",
        "playground::switch_runtime::initializeSwitchSqlitePaths(",
        "playground::switch_runtime::SwitchNetwork::suspend(",
        "playground::switch_runtime::SwitchNetwork::shutdownAndWait(",
        "playground::switch_runtime::SwitchNetwork::rollbackInitialization(",
        "notifRegisterAlarmSetting",
        "notifUpdateAlarmSetting",
        # notifGetNotificationSystemEvent is an inline wrapper around this
        # applet entry point, so the linked image exposes the underlying name.
        "appletGetNotificationStorageChannelEvent",
        "eventWait",
        "notifTryPopNotifiedApplicationParameter",
        "notifDeleteAlarmSetting",
        "accountGetPreselectedUser",
        "capssuSaveScreenShotWithUserIds",
        "nifmLaHandleNetworkRequestResult",
        "fsdevMountSaveData",
        "fsdevMountCacheStorage",
        "fsFsDeleteDirectoryRecursively",
        "fsFsDeleteFile",
        "fsFsGetFreeSpace",
        "playground::switch_runtime::encodeStoragePath(",
        "playground::switch_runtime::listStorageDirectory(",
        "ftruncate",
    )
    missing_symbols = [value for value in required_symbols
                       if value not in symbols]
    if missing_symbols:
        raise SystemExit("Switch runtime symbols are missing: " +
                         ", ".join(missing_symbols))

    print("Switch runtime contract audit passed: game-agnostic encrypted SQLite, "
          "typed database copy-up, account storage overlay, native FS, native "
          "network recovery, account-scoped Album/alarms, reconnecting input, "
          "and suspend leases linked")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
