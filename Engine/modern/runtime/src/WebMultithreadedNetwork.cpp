#include "MultithreadedNetwork.h"

#include <emscripten.h>
#include <emscripten/fetch.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

EM_JS(int, playgroundWebPageUsesHttps, (), {
  const currentLocation = globalThis.location;
  if (!currentLocation)
    return 0;
  return currentLocation.protocol === "https:" ||
                 currentLocation.origin.startsWith("https://")
             ? 1
             : 0;
});

namespace {

std::mutex CookieMutex;
std::string CookiePath;

uint64_t nowNanoseconds() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace

struct CurlObjectInternal::WebData {
  struct FormPart {
    std::string name;
    std::vector<char> value;
  };
  std::vector<std::string> headers;
  std::vector<FormPart> form;
  std::vector<char> post;
  std::string url;
  std::string userAgent;
  void *callbackContext{};
  void *headerCallback{};
  void *writeCallback{};
  long httpCode{};
  CurlTransferMetrics metrics{};
  bool usePost{};
};

CurlObjectInternal::CurlObjectInternal()
    : m_web(new WebData), m_postConfigured(false), m_progressContext(nullptr),
      m_progressCallback(nullptr), m_abortPredicate(nullptr),
      m_abortContext(nullptr), m_configuredAtNanoseconds(0),
      m_performStartedAtNanoseconds(0), m_performCount(0) {}

CurlObjectInternal *CurlObjectInternal::create() {
  return new CurlObjectInternal;
}

void CurlObjectInternal::destroy(CurlObjectInternal *operation) {
  delete operation;
}

bool CurlObjectInternal::initializeLibrary() { return true; }
void CurlObjectInternal::shutdownLibrary() {}

bool CurlObjectInternal::configureCookieStorage(const char *path) {
  std::lock_guard<std::mutex> lock(CookieMutex);
  CookiePath = path ? path : "";
  // Browser cookies remain under the origin's cookie policy. The path is
  // retained as a versioned state marker so desktop/Switch state layouts stay
  // compatible without duplicating browser-managed credentials.
  if (!CookiePath.empty()) {
    std::ofstream marker(CookiePath, std::ios::binary | std::ios::app);
    return static_cast<bool>(marker);
  }
  return false;
}

bool CurlObjectInternal::flushCookieStorage(bool *changed) {
  if (changed)
    *changed = false;
  return true;
}

bool CurlObjectInternal::clearCookieStorage() {
  // The game only uses server cookies as an adjunct to its signed session.
  // Expire every cookie visible to this path; HttpOnly cookies remain owned by
  // the server and are invalidated by its logout response.
  MAIN_THREAD_EM_ASM({
    var items = document.cookie.split(';');
    for (var i = 0; i < items.length; ++i) {
      var name = items[i].split('=', 1)[0].trim();
      if (name) document.cookie = name + '=; Max-Age=0; Path=/; SameSite=Lax';
    }
  });
  return true;
}

void CurlObjectInternal::reset() {
  freeFormHeaders();
  m_web->post.clear();
  m_web->url.clear();
  m_web->httpCode = 0;
  m_web->usePost = false;
  m_postConfigured = false;
  m_performCount = 0;
}

void CurlObjectInternal::cleanup() {}

void CurlObjectInternal::freeFormHeaders() {
  m_web->headers.clear();
  m_web->form.clear();
}

void CurlObjectInternal::appendHeader(const char *header) {
  if (header)
    m_web->headers.emplace_back(header);
}

void CurlObjectInternal::setPostFields() {
  m_postConfigured = true;
  m_web->usePost = true;
}

void CurlObjectInternal::setPostData(long contentLength, const void *data) {
  m_web->post.assign(static_cast<const char *>(data),
                     static_cast<const char *>(data) +
                         std::max<long>(contentLength, 0));
  m_postConfigured = true;
  m_web->usePost = true;
}

void CurlObjectInternal::addFormData(const char *name, long contentLength,
                                     const void *data) {
  WebData::FormPart part;
  part.name = name ? name : "";
  part.value.assign(static_cast<const char *>(data),
                    static_cast<const char *>(data) +
                        std::max<long>(contentLength, 0));
  m_web->form.push_back(std::move(part));
}

void CurlObjectInternal::setupConnection(const char *url, const char *proxy,
                                         void *callbackContext,
                                         void *progressCallback,
                                         void *headerCallback,
                                         void *writeCallback) {
  m_web->url = url ? url : "";
  // An HTTPS page cannot issue active HTTP requests: browsers reject them as
  // mixed content before Fetch reaches the network. Upgrade absolute HTTP
  // URLs at this platform boundary so legacy configuration and server-provided
  // links use the transport already required by the Web host. Never downgrade
  // HTTPS when developing from an HTTP localhost page.
  if (playgroundWebPageUsesHttps() && m_web->url.compare(0, 7, "http://") == 0)
    m_web->url.replace(0, 7, "https://");
  // The legacy second argument is a User-Agent string despite its old proxy
  // name. Browsers forbid setting User-Agent; preserve it as an application
  // header that servers may inspect without violating Fetch's forbidden list.
  m_web->userAgent = proxy ? proxy : "";
  m_web->callbackContext = callbackContext;
  m_web->headerCallback = headerCallback;
  m_web->writeCallback = writeCallback;
  m_progressContext = callbackContext;
  m_progressCallback = progressCallback;
  m_configuredAtNanoseconds = nowNanoseconds();
}

int CurlObjectInternal::perform() {
  ++m_performCount;
  m_performStartedAtNanoseconds = nowNanoseconds();
  if (m_abortPredicate && m_abortPredicate(m_abortContext))
    return 42; // CURLE_ABORTED_BY_CALLBACK

  std::string boundary;
  if (m_web->usePost && m_web->post.empty() && !m_web->form.empty()) {
    boundary = "----PlaygroundOSSFormBoundary7MA4YWxkTrZu0gW";
    std::ostringstream body;
    for (const auto &part : m_web->form) {
      body << "--" << boundary << "\r\n"
           << "Content-Disposition: form-data; name=\"" << part.name
           << "\"\r\n\r\n";
      body.write(part.value.data(), static_cast<std::streamsize>(part.value.size()));
      body << "\r\n";
    }
    body << "--" << boundary << "--\r\n";
    const std::string text = body.str();
    m_web->post.assign(text.begin(), text.end());
    m_web->headers.emplace_back("Content-Type: multipart/form-data; boundary=" +
                                boundary);
  }

  std::vector<const char *> requestHeaders;
  requestHeaders.reserve((m_web->headers.size() + 1) * 2 + 1);
  std::vector<std::string> headerNames;
  std::vector<std::string> headerValues;
  for (const auto &header : m_web->headers) {
    const std::size_t separator = header.find(':');
    if (separator == std::string::npos)
      continue;
    std::string name = header.substr(0, separator);
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                   [](unsigned char byte) {
                     return static_cast<char>(std::tolower(byte));
                   });
    // libcurl's empty Expect header disables HTTP/1.1 100-continue. Fetch
    // controls that handshake itself and browsers forbid author code from
    // setting Expect at all.
    if (lowerName == "expect")
      continue;
    headerNames.push_back(std::move(name));
    std::size_t value = separator + 1;
    while (value < header.size() && header[value] == ' ')
      ++value;
    headerValues.push_back(header.substr(value));
  }
  if (!m_web->userAgent.empty()) {
    headerNames.emplace_back("X-Playground-User-Agent");
    headerValues.push_back(m_web->userAgent);
  }
  for (std::size_t i = 0; i < headerNames.size(); ++i) {
    requestHeaders.push_back(headerNames[i].c_str());
    requestHeaders.push_back(headerValues[i].c_str());
  }
  requestHeaders.push_back(nullptr);

  emscripten_fetch_attr_t attributes;
  emscripten_fetch_attr_init(&attributes);
  std::strcpy(attributes.requestMethod, m_web->usePost ? "POST" : "GET");
  attributes.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY |
                          EMSCRIPTEN_FETCH_SYNCHRONOUS;
  attributes.requestHeaders = requestHeaders.data();
  attributes.requestData = m_web->post.empty() ? nullptr : m_web->post.data();
  attributes.requestDataSize = m_web->post.size();
  // Match Fetch's default credentials policy: same-origin API requests use
  // browser cookies, while cross-origin CDN downloads remain anonymous.
  attributes.withCredentials = EM_FALSE;
  emscripten_fetch_t *fetch =
      emscripten_fetch(&attributes, m_web->url.c_str());
  if (!fetch) {
    std::fprintf(stderr,
                 "web network: emscripten_fetch rejected %s before dispatch\n",
                 m_web->url.c_str());
    return 2;
  }

  m_web->httpCode = fetch->status;
  const auto started = std::chrono::steady_clock::now();
  using HeaderCallback = std::size_t (*)(void *, std::size_t, std::size_t, void *);
  using WriteCallback = std::size_t (*)(char *, std::size_t, std::size_t, void *);
  bool headerFailed = false;
  if (m_web->headerCallback) {
    const auto callback =
        reinterpret_cast<HeaderCallback>(m_web->headerCallback);
    const std::string statusLine =
        "HTTP/1.1 " + std::to_string(fetch->status) + "\r\n";
    headerFailed = callback(const_cast<char *>(statusLine.data()), 1,
                            statusLine.size(), m_web->callbackContext) !=
                   statusLine.size();
    const int length = emscripten_fetch_get_response_headers_length(fetch);
    std::string headers(static_cast<std::size_t>(std::max(length, 0)) + 1, '\0');
    emscripten_fetch_get_response_headers(fetch, headers.data(), headers.size());
    headers.resize(std::strlen(headers.c_str()));
    std::size_t cursor = 0;
    while (!headerFailed && cursor < headers.size()) {
      const std::size_t end = headers.find('\n', cursor);
      std::string line = headers.substr(
          cursor, end == std::string::npos ? std::string::npos : end - cursor);
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      line += "\r\n";
      headerFailed = callback(line.data(), 1, line.size(),
                              m_web->callbackContext) != line.size();
      if (end == std::string::npos)
        break;
      cursor = end + 1;
    }
    if (!headerFailed) {
      char terminator[] = "\r\n";
      headerFailed = callback(terminator, 1, 2, m_web->callbackContext) != 2;
    }
  }
  std::size_t written = fetch->numBytes;
  if (m_web->writeCallback && fetch->numBytes) {
    written = reinterpret_cast<WriteCallback>(m_web->writeCallback)(
        const_cast<char *>(fetch->data), 1, fetch->numBytes,
        m_web->callbackContext);
  }
  bool progressFailed = false;
  if (m_progressCallback) {
    using ProgressCallback = int (*)(void *, double, double, double, double);
    progressFailed = reinterpret_cast<ProgressCallback>(m_progressCallback)(
        m_progressContext, static_cast<double>(fetch->totalBytes),
        static_cast<double>(fetch->numBytes),
        static_cast<double>(m_web->post.size()),
        static_cast<double>(m_web->post.size())) != 0;
  }
  const auto finished = std::chrono::steady_clock::now();
  m_web->metrics.totalSeconds =
      std::chrono::duration<double>(finished - started).count();
  m_web->metrics.firstByteSeconds = m_web->metrics.totalSeconds;
  m_web->metrics.downloadedBytes = static_cast<double>(fetch->numBytes);
  m_web->metrics.averageBytesPerSecond = m_web->metrics.totalSeconds > 0
      ? m_web->metrics.downloadedBytes / m_web->metrics.totalSeconds : 0;
  m_web->metrics.connectionCount = 1;
  m_web->metrics.attemptNumber = m_performCount;
  // Match libcurl's transport contract: an HTTP 4xx/5xx response is still a
  // successfully completed transfer.  The engine consumes getHttpCode() and
  // the response body to decide how to handle server errors.
  const bool success = fetch->status != 0 && written == fetch->numBytes &&
                       !headerFailed && !progressFailed;
  if (!success) {
    std::fprintf(stderr, "web fetch failed: status=%d bytes=%llu url=%s\n",
                 fetch->status,
                 static_cast<unsigned long long>(fetch->numBytes),
                 m_web->url.c_str());
  }
  const bool writeFailed = written != fetch->numBytes || headerFailed;
  emscripten_fetch_close(fetch);
  return success ? 0 : (progressFailed ? 42 : writeFailed ? 23 : 22);
}

long CurlObjectInternal::getHttpCode() { return m_web->httpCode; }

CurlTransferMetrics CurlObjectInternal::getTransferMetrics() const {
  CurlTransferMetrics metrics = m_web->metrics;
  if (m_performStartedAtNanoseconds >= m_configuredAtNanoseconds) {
    metrics.queueSeconds = static_cast<double>(m_performStartedAtNanoseconds -
                                               m_configuredAtNanoseconds) /
                           1000000000.0;
  }
  return metrics;
}

void CurlObjectInternal::setAbortPredicate(bool (*predicate)(void *),
                                           void *context) {
  m_abortPredicate = predicate;
  m_abortContext = context;
}

void CurlObjectInternal::configureTransportLimits(long, long, long, long) {}

int CurlObjectInternal::progressDispatch(void *, double, double, double,
                                         double) {
  return 0;
}
