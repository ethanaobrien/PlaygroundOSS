/*
   Copyright 2013 KLab Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0
*/

#include "RuntimeStateStore.h"

#include <array>
#include <cstdio>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace playground::runtime {
namespace {

constexpr std::array<unsigned char, 8> StateMagic{'K', 'L', 'B', 'S',
                                                  'T', 'A', 'T', '1'};
constexpr std::uint32_t MaximumEntryCount = 16384;
constexpr std::uint32_t MaximumFieldLength = 16 * 1024 * 1024;

bool writeU32(FILE *file, std::uint32_t value) {
  unsigned char bytes[] = {static_cast<unsigned char>(value),
                           static_cast<unsigned char>(value >> 8),
                           static_cast<unsigned char>(value >> 16),
                           static_cast<unsigned char>(value >> 24)};
  return std::fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
}

bool readU32(FILE *file, std::uint32_t &value) {
  unsigned char bytes[4];
  if (std::fread(bytes, 1, sizeof(bytes), file) != sizeof(bytes))
    return false;
  value = static_cast<std::uint32_t>(bytes[0]) |
          (static_cast<std::uint32_t>(bytes[1]) << 8) |
          (static_cast<std::uint32_t>(bytes[2]) << 16) |
          (static_cast<std::uint32_t>(bytes[3]) << 24);
  return true;
}

FILE *openFile(const std::filesystem::path &path, const char *mode) {
#if defined(_WIN32)
  const wchar_t *wideMode = mode[0] == 'r' ? L"rb" : L"wb";
  return _wfopen(path.c_str(), wideMode);
#else
  return std::fopen(path.c_str(), mode);
#endif
}

bool flushFile(FILE *file) {
  if (std::fflush(file) != 0)
    return false;
#if defined(_WIN32)
  return _commit(_fileno(file)) == 0;
#else
  return fsync(fileno(file)) == 0;
#endif
}

bool replaceFile(const std::filesystem::path &source,
                 const std::filesystem::path &destination,
                 std::error_code &error) {
#if defined(_WIN32)
  if (MoveFileExW(source.c_str(), destination.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    error.clear();
    return true;
  }
  error =
      std::error_code(static_cast<int>(GetLastError()), std::system_category());
  return false;
#else
  std::filesystem::rename(source, destination, error);
  return !error;
#endif
}

std::filesystem::path sibling(const std::filesystem::path &path,
                              const char *suffix) {
  std::filesystem::path result = path;
  result += suffix;
  return result;
}

bool syncParentDirectory(const std::filesystem::path &path) {
#if defined(_WIN32)
  // MoveFileExW(..., MOVEFILE_WRITE_THROUGH) is the Windows publication
  // boundary used by replaceFile().
  (void)path;
  return true;
#else
  const std::filesystem::path parent = path.parent_path();
  const int descriptor =
      open(parent.empty() ? "." : parent.c_str(), O_RDONLY | O_DIRECTORY);
  if (descriptor < 0)
    return false;
  const bool valid = fsync(descriptor) == 0;
  close(descriptor);
  return valid;
#endif
}

bool loadFile(const std::filesystem::path &path,
              std::unordered_map<std::string, std::string> &loaded) {
  FILE *file = openFile(path, "rb");
  if (!file)
    return false;
  std::array<unsigned char, StateMagic.size()> magic{};
  std::uint32_t count = 0;
  bool valid =
      std::fread(magic.data(), 1, magic.size(), file) == magic.size() &&
      magic == StateMagic && readU32(file, count) && count <= MaximumEntryCount;
  std::unordered_map<std::string, std::string> candidate;
  for (std::uint32_t index = 0; valid && index < count; ++index) {
    std::uint32_t keyLength = 0;
    std::uint32_t valueLength = 0;
    valid = readU32(file, keyLength) && readU32(file, valueLength) &&
            keyLength <= MaximumFieldLength &&
            valueLength <= MaximumFieldLength;
    if (!valid)
      break;
    std::string key(keyLength, '\0');
    std::string value(valueLength, '\0');
    valid = std::fread(key.data(), 1, key.size(), file) == key.size() &&
            std::fread(value.data(), 1, value.size(), file) == value.size();
    if (valid)
      candidate[std::move(key)] = std::move(value);
  }
  if (std::fclose(file) != 0)
    valid = false;
  if (valid)
    loaded = std::move(candidate);
  return valid;
}

} // namespace

RuntimeStateStore::RuntimeStateStore(std::filesystem::path path, Commit commit)
    : m_path(std::move(path)), m_commit(std::move(commit)) {
  load();
}

std::string RuntimeStateStore::get(const std::string &key) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  const auto value = m_values.find(key);
  return value == m_values.end() ? std::string() : value->second;
}

bool RuntimeStateStore::set(const std::string &key, const std::string &value) {
  std::lock_guard<std::mutex> lock(m_mutex);
  const auto old = m_values.find(key);
  const bool existed = old != m_values.end();
  const std::string previous = existed ? old->second : std::string();
  m_values[key] = value;
  if (saveLocked())
    return true;
  if (existed)
    m_values[key] = previous;
  else
    m_values.erase(key);
  return false;
}

bool RuntimeStateStore::erase(const std::string &key) {
  std::lock_guard<std::mutex> lock(m_mutex);
  const auto old = m_values.find(key);
  if (old == m_values.end())
    return true;
  const std::string previous = old->second;
  m_values.erase(old);
  if (saveLocked())
    return true;
  m_values[key] = previous;
  return false;
}

bool RuntimeStateStore::load() {
  std::lock_guard<std::mutex> lock(m_mutex);
  std::unordered_map<std::string, std::string> loaded;
  if (loadFile(m_path, loaded)) {
    m_values = std::move(loaded);
    return true;
  }

  const std::filesystem::path previous = sibling(m_path, ".previous");
  if (loadFile(previous, loaded)) {
    m_values = std::move(loaded);
    std::error_code error;
    std::filesystem::remove(m_path, error);
    error.clear();
    if (replaceFile(previous, m_path, error)) {
      if (m_commit)
        m_commit();
      else
        syncParentDirectory(m_path);
    }
    return true;
  }

  // A missing store is the normal first-launch state. A malformed store is
  // deliberately not interpreted as an empty successful state.
  std::error_code error;
  return !std::filesystem::exists(m_path, error) &&
         !std::filesystem::exists(previous, error);
}

bool RuntimeStateStore::saveLocked() const {
  std::error_code error;
  std::filesystem::create_directories(m_path.parent_path(), error);
  if (error)
    return false;

  const std::filesystem::path temporary = sibling(m_path, ".tmp");
  const std::filesystem::path previous = sibling(m_path, ".previous");
  FILE *file = openFile(temporary, "wb");
  if (!file)
    return false;
#if !defined(_WIN32)
  chmod(temporary.c_str(), S_IRUSR | S_IWUSR);
#endif

  bool valid = std::fwrite(StateMagic.data(), 1, StateMagic.size(), file) ==
                   StateMagic.size() &&
               writeU32(file, static_cast<std::uint32_t>(m_values.size()));
  for (const auto &entry : m_values) {
    if (!valid || entry.first.size() > MaximumFieldLength ||
        entry.second.size() > MaximumFieldLength) {
      valid = false;
      break;
    }
    valid = writeU32(file, static_cast<std::uint32_t>(entry.first.size())) &&
            writeU32(file, static_cast<std::uint32_t>(entry.second.size())) &&
            std::fwrite(entry.first.data(), 1, entry.first.size(), file) ==
                entry.first.size() &&
            std::fwrite(entry.second.data(), 1, entry.second.size(), file) ==
                entry.second.size();
  }
  if (valid)
    valid = flushFile(file);
  if (std::fclose(file) != 0)
    valid = false;
  if (!valid) {
    std::filesystem::remove(temporary, error);
    return false;
  }

  const bool hadPreviousGeneration = std::filesystem::exists(m_path, error);
  if (error)
    valid = false;
  if (valid) {
    std::filesystem::remove(previous, error);
    if (error)
      valid = false;
  }
  if (valid && hadPreviousGeneration)
    valid = replaceFile(m_path, previous, error);
  if (valid)
    valid = replaceFile(temporary, m_path, error);
  if (!valid) {
    if (hadPreviousGeneration && !std::filesystem::exists(m_path)) {
      std::error_code restoreError;
      replaceFile(previous, m_path, restoreError);
    }
    std::filesystem::remove(temporary, error);
    return false;
  }

  const bool published = m_commit ? m_commit() : syncParentDirectory(m_path);
  if (!published) {
    // Keep the mounted view consistent with the caller's in-memory rollback.
    // A second commit is best-effort: if the device remains unavailable, the
    // next successful SaveData commit will still see the restored old view.
    std::filesystem::remove(m_path, error);
    error.clear();
    if (hadPreviousGeneration)
      replaceFile(previous, m_path, error);
    if (m_commit)
      m_commit();
    else
      syncParentDirectory(m_path);
    return false;
  }

  // The new generation is already durable. Removing the recovery copy is
  // cleanup only; a failed cleanup commit merely leaves a harmless fallback.
  std::filesystem::remove(previous, error);
  if (!error) {
    if (m_commit)
      m_commit();
    else
      syncParentDirectory(m_path);
  }
  return true;
}

} // namespace playground::runtime
