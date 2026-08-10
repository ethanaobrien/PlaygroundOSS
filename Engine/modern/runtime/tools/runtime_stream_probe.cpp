#include "Playground/Runtime/DesktopPlatform.h"
#include "FileSystem.h"
#include "encryptFile.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv)
{
    if(argc != 3) {
        std::fprintf(stderr, "usage: %s install-root virtual-path\n", argv[0]);
        return 64;
    }
    playground::runtime::DesktopPlatform platform(argv[1], "/tmp/playground-stream-probe");
    initNMAsset(0);
    IReadStream* stream = platform.openReadStream(argv[2], true, 8);
    int result = 1;
    if(stream && stream->getStatus() == IReadStream::NORMAL) {
        const s32 size = stream->getSize();
        std::vector<u8> bytes(size > 0 ? static_cast<size_t>(size) : 0);
        bool read = size >= 0 && stream->readBlock(bytes.data(), static_cast<u32>(bytes.size()));
        std::printf("size=%d read=%s prefix=", size, read ? "true" : "false");
        for(size_t i = 0; i < bytes.size() && i < 16; ++i) std::printf("%02x", bytes[i]);
        std::putchar('\n');
        result = read ? 0 : 2;
    }
    delete stream;
    std::fflush(nullptr);
    std::_Exit(result);
}
