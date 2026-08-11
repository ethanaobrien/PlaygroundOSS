/*
   Copyright 2013 KLab Inc.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0
*/

#include "DesktopStateStore.h"

#include <array>
#include <cstdio>
#include <system_error>

#include <sys/stat.h>
#include <unistd.h>

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

} // namespace

DesktopStateStore::DesktopStateStore(std::filesystem::path path)
    : m_path(std::move(path)) {
  load();
}

std::string DesktopStateStore::get(const std::string &key) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  const auto value = m_values.find(key);
  return value == m_values.end() ? std::string() : value->second;
}

bool DesktopStateStore::set(const std::string &key, const std::string &value) {
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

bool DesktopStateStore::erase(const std::string &key) {
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

bool DesktopStateStore::load() {
  std::lock_guard<std::mutex> lock(m_mutex);
  FILE *file = std::fopen(m_path.c_str(), "rb");
  if (!file)
    return true;

  std::array<unsigned char, StateMagic.size()> magic{};
  std::uint32_t count = 0;
  bool valid =
      std::fread(magic.data(), 1, magic.size(), file) == magic.size() &&
      magic == StateMagic && readU32(file, count) && count <= MaximumEntryCount;
  std::unordered_map<std::string, std::string> loaded;
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
      loaded[std::move(key)] = std::move(value);
  }
  std::fclose(file);
  if (valid)
    m_values = std::move(loaded);
  return valid;
}

bool DesktopStateStore::saveLocked() const {
  std::error_code error;
  std::filesystem::create_directories(m_path.parent_path(), error);
  if (error)
    return false;

  std::filesystem::path temporary = m_path;
  temporary += ".tmp";
  FILE *file = std::fopen(temporary.c_str(), "wb");
  if (!file)
    return false;
  chmod(temporary.c_str(), S_IRUSR | S_IWUSR);

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
    valid = std::fflush(file) == 0 && fsync(fileno(file)) == 0;
  if (std::fclose(file) != 0)
    valid = false;
  if (valid) {
    std::filesystem::rename(temporary, m_path, error);
    valid = !error;
  }
  if (!valid)
    std::filesystem::remove(temporary, error);
  return valid;
}

} // namespace playground::runtime
