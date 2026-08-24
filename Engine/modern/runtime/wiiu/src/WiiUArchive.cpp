#include "Playground/WiiU/WiiUArchive.h"

#include "ioapi.h"
#include "unzip.h"


#include <cerrno>
#include <fcntl.h>
#include <limits>
#include <new>
#include <unistd.h>

namespace playground::wiiu_runtime {
namespace {

struct DescriptorArchive {
  int descriptor{-1};
  int error{};
};

voidpf ZCALLBACK openDescriptor(voidpf, const void *filename, int mode) {
  if ((mode & ZLIB_FILEFUNC_MODE_READWRITEFILTER) != ZLIB_FILEFUNC_MODE_READ)
    return nullptr;
  const int descriptor = open(static_cast<const char *>(filename), O_RDONLY);
  if (descriptor < 0)
    return nullptr;
  auto *archive = new (std::nothrow) DescriptorArchive;
  if (!archive) {
    close(descriptor);
    return nullptr;
  }
  archive->descriptor = descriptor;
  return archive;
}

uLong ZCALLBACK readDescriptor(voidpf, voidpf stream, void *buffer,
                               uLong size) {
  auto &archive = *static_cast<DescriptorArchive *>(stream);
  const ssize_t count = read(archive.descriptor, buffer, size);
  if (count >= 0)
    return static_cast<uLong>(count);
  archive.error = errno;
  return 0;
}

uLong ZCALLBACK writeDescriptor(voidpf, voidpf, const void *, uLong) {
  return 0;
}

ZPOS64_T ZCALLBACK tellDescriptor(voidpf, voidpf stream) {
  auto &archive = *static_cast<DescriptorArchive *>(stream);
  const off_t position = lseek(archive.descriptor, 0, SEEK_CUR);
  if (position >= 0)
    return static_cast<ZPOS64_T>(position);
  archive.error = errno;
  return std::numeric_limits<ZPOS64_T>::max();
}

long ZCALLBACK seekDescriptor(voidpf, voidpf stream, ZPOS64_T offset,
                              int origin) {
  auto &archive = *static_cast<DescriptorArchive *>(stream);
  int nativeOrigin = SEEK_SET;
  if (origin == ZLIB_FILEFUNC_SEEK_CUR)
    nativeOrigin = SEEK_CUR;
  else if (origin == ZLIB_FILEFUNC_SEEK_END)
    nativeOrigin = SEEK_END;
  else if (origin != ZLIB_FILEFUNC_SEEK_SET) {
    archive.error = EINVAL;
    return -1;
  }
  if (offset > static_cast<ZPOS64_T>(std::numeric_limits<off_t>::max())) {
    archive.error = EOVERFLOW;
    return -1;
  }
  if (lseek(archive.descriptor, static_cast<off_t>(offset), nativeOrigin) >= 0)
    return 0;
  archive.error = errno;
  return -1;
}

int ZCALLBACK closeDescriptor(voidpf, voidpf stream) {
  auto *archive = static_cast<DescriptorArchive *>(stream);
  const int result = close(archive->descriptor);
  delete archive;
  return result;
}

int ZCALLBACK errorDescriptor(voidpf, voidpf stream) {
  return static_cast<DescriptorArchive *>(stream)->error;
}

} // namespace

void *openArchive(const std::filesystem::path &path, std::string &error) {
  zlib_filefunc64_def functions{};
  functions.zopen64_file = openDescriptor;
  functions.zread_file = readDescriptor;
  functions.zwrite_file = writeDescriptor;
  functions.ztell64_file = tellDescriptor;
  functions.zseek64_file = seekDescriptor;
  functions.zclose_file = closeDescriptor;
  functions.zerror_file = errorDescriptor;
  unzFile handle = unzOpen2_64(path.string().c_str(), &functions);
  if (!handle)
    error = "CafeOS could not initialize the bundled AppAssets ZIP";
  return handle;
}

} // namespace playground::wiiu_runtime
