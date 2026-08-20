#include "MultithreadedNetwork.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

size_t writeBody(void *data, size_t size, size_t count, void *context) {
  const size_t bytes = size * count;
  static_cast<std::string *>(context)->append(static_cast<const char *>(data),
                                               bytes);
  return bytes;
}

size_t discardHeader(void *, size_t size, size_t count, void *) {
  return size * count;
}

bool request(const std::string &url, const char *expected) {
  CurlObjectInternal *operation = CurlObjectInternal::create();
  if (!operation)
    return false;
  std::string body;
  operation->setupConnection(url.c_str(), nullptr, &body, nullptr,
                             reinterpret_cast<void *>(discardHeader),
                             reinterpret_cast<void *>(writeBody));
  const int result = operation->perform();
  const long status = operation->getHttpCode();
  operation->cleanup();
  CurlObjectInternal::destroy(operation);
  if (result != 0 || status != 200 || body != expected)
    std::fprintf(stderr, "cookie request %s result=%d status=%ld body=%s expected=%s\n",
                 url.c_str(), result, status, body.c_str(), expected);
  return result == 0 && status == 200 && body == expected;
}

bool contains(const std::filesystem::path &path, const char *text) {
  std::ifstream input(path, std::ios::binary);
  return input && std::string(std::istreambuf_iterator<char>(input),
                              std::istreambuf_iterator<char>())
                      .find(text) != std::string::npos;
}

[[noreturn]] void finish(int status) {
  std::fflush(nullptr);
  // The legacy engine has process-global destructors that require a fully
  // initialized game client.  This focused network probe intentionally never
  // creates one and explicitly shuts libcurl down before successful exit.
  std::_Exit(status);
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s cookie-directory base-url\n", argv[0]);
    finish(64);
  }
  const std::filesystem::path root = argv[1];
  const std::filesystem::path jar = root / "cookies.txt";
  std::error_code error;
  std::filesystem::create_directories(root, error);
  if (error || !CurlObjectInternal::initializeLibrary() ||
      !CurlObjectInternal::configureCookieStorage(jar.string().c_str()))
    finish(1);

  bool changed = false;
  if (!request(std::string(argv[2]) + "/set", "set") ||
      !request(std::string(argv[2]) + "/check", "present") ||
      !CurlObjectInternal::flushCookieStorage(&changed) || !changed ||
      !contains(jar, "playground_session") ||
      !CurlObjectInternal::flushCookieStorage(&changed) || changed) {
    CurlObjectInternal::shutdownLibrary();
    finish(2);
  }
  CurlObjectInternal::shutdownLibrary();

  if (!CurlObjectInternal::initializeLibrary() ||
      !CurlObjectInternal::configureCookieStorage(jar.string().c_str()) ||
      !request(std::string(argv[2]) + "/check", "present") ||
      !CurlObjectInternal::clearCookieStorage() ||
      !request(std::string(argv[2]) + "/check", "absent") ||
      contains(jar, "playground_session")) {
    CurlObjectInternal::shutdownLibrary();
    finish(3);
  }
  CurlObjectInternal::shutdownLibrary();
  std::puts("runtime-cookie-probe passed");
  finish(0);
}
