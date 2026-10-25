#pragma once
#include <cstdio>
#include <memory>
#include <string>

namespace playground::runtime {
// Optional writer boundary for hosts whose commit applies to an entire mounted
// archive. Callers still decide when publication/commit is required.
class StorageWriter {
public:
  virtual ~StorageWriter() = default;
  virtual std::size_t write(const void*, std::size_t) = 0;
  virtual long position() = 0;
  virtual int close() = 0;
};
class StdioStorageWriter final : public StorageWriter {
public:
  explicit StdioStorageWriter(FILE* file) : m_file(file) {}
  ~StdioStorageWriter() override { close(); }
  std::size_t write(const void* data, std::size_t size) override {
    return m_file ? std::fwrite(data, 1, size, m_file) : 0;
  }
  long position() override { return m_file ? std::ftell(m_file) : -1; }
  int close() override {
    FILE* file = m_file;
    m_file = nullptr;
    return file ? std::fclose(file) : 0;
  }
private:
  FILE* m_file;
};
inline std::unique_ptr<StorageWriter> openStorageWriter(const std::string& path) {
  FILE* file = std::fopen(path.c_str(), "wb");
  return file ? std::make_unique<StdioStorageWriter>(file) : nullptr;
}
}
