#include "MultithreadedNetwork.h"

#include <emscripten.h>
#include <emscripten/atomic.h>
#include <emscripten/threading.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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

enum XhrRequestState : std::uint32_t {
  XhrRequestPending,
  XhrRequestProgress,
  XhrRequestDone,
  XhrRequestError,
  XhrRequestAborted,
};

struct alignas(4) XhrRequestControl {
  std::uint32_t state{};
  std::uint32_t httpStatus{};
  std::uint32_t data{};
  std::uint32_t length{};
  std::uint32_t headers{};
  std::uint32_t headersLength{};
  std::uint32_t downloaded{};
  std::uint32_t downloadTotal{};
  std::uint32_t uploaded{};
  std::uint32_t uploadTotal{};
};

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

  const auto started = std::chrono::steady_clock::now();
  XhrRequestControl request;
  MAIN_THREAD_EM_ASM({
    const controlAddress = $0;
    const control = controlAddress >>> 2;
    const xhr = new XMLHttpRequest();
    globalThis.playgroundXhrRequests ||= new Map();
    globalThis.playgroundXhrRequests.set(controlAddress, xhr);
    const publish = state => {
      Atomics.store(HEAP32, control, state);
      Atomics.notify(HEAP32, control);
    };
    const publishProgress = () => {
      const state = Atomics.load(HEAP32, control);
      if (state === 0 || state === 1)
        publish(1);
    };
    const finish = state => {
      globalThis.playgroundXhrRequests.delete(controlAddress);
      publish(state);
    };
    try {
      xhr.open($1 ? 'POST' : 'GET', UTF8ToString($2), true);
      xhr.responseType = 'arraybuffer';
      xhr.withCredentials = false;
      for (let headers = $3 >>> 2; ; headers += 2) {
        const name = HEAPU32[headers];
        if (!name)
          break;
        const value = HEAPU32[headers + 1];
        xhr.setRequestHeader(UTF8ToString(name), UTF8ToString(value));
      }
      xhr.onreadystatechange = () => {
        if (xhr.readyState >= XMLHttpRequest.HEADERS_RECEIVED) {
          try {
            Atomics.store(HEAP32, control + 1, xhr.status);
          } catch (_) {}
          publishProgress();
        }
      };
      xhr.onprogress = event => {
        Atomics.store(HEAP32, control + 6,
          Math.min(event.loaded, 0xffffffff));
        Atomics.store(HEAP32, control + 7,
          event.lengthComputable ? Math.min(event.total, 0xffffffff) : 0);
        publishProgress();
      };
      xhr.upload.onprogress = event => {
        Atomics.store(HEAP32, control + 8,
          Math.min(event.loaded, 0xffffffff));
        Atomics.store(HEAP32, control + 9,
          event.lengthComputable ? Math.min(event.total, 0xffffffff) : 0);
        publishProgress();
      };
      xhr.onload = () => {
        Atomics.store(HEAP32, control + 1, xhr.status);
        const bytes = new Uint8Array(xhr.response || new ArrayBuffer(0));
        const data = _malloc(bytes.byteLength || 1);
        const rawHeaders = xhr.getAllResponseHeaders();
        const headers = stringToNewUTF8(rawHeaders);
        if (!data || !headers) {
          if (data) _free(data);
          if (headers) _free(headers);
          finish(3);
          return;
        }
        HEAPU8.set(bytes, data);
        Atomics.store(HEAP32, control + 2, data);
        Atomics.store(HEAP32, control + 3, bytes.byteLength);
        Atomics.store(HEAP32, control + 4, headers);
        Atomics.store(HEAP32, control + 5, lengthBytesUTF8(rawHeaders));
        Atomics.store(HEAP32, control + 6, bytes.byteLength);
        if (!Atomics.load(HEAP32, control + 7))
          Atomics.store(HEAP32, control + 7, bytes.byteLength);
        finish(2);
      };
      xhr.onerror = xhr.ontimeout = () => finish(3);
      xhr.onabort = () => finish(4);
      const body = $4 && $5 ? HEAPU8.slice($4, $4 + $5) : null;
      xhr.send(body);
    } catch (exception) {
      console.error(`web xhr setup failed: ${exception}`);
      finish(3);
    }
  }, &request, m_web->usePost, m_web->url.c_str(), requestHeaders.data(),
     m_web->post.empty() ? nullptr : m_web->post.data(), m_web->post.size());

  bool progressFailed = false;
  bool sawFirstByte = false;
  auto firstByte = started;
  for (;;) {
    if (m_abortPredicate && m_abortPredicate(m_abortContext)) {
      MAIN_THREAD_EM_ASM({
        const xhr = globalThis.playgroundXhrRequests?.get($0);
        if (xhr)
          xhr.abort();
      }, &request);
      progressFailed = true;
    }
    const auto state = static_cast<XhrRequestState>(
        emscripten_atomic_load_u32(&request.state));
    if (state == XhrRequestPending) {
      emscripten_futex_wait(&request.state, XhrRequestPending, 100.0);
      continue;
    }
    if (state == XhrRequestProgress) {
      if (!sawFirstByte && (request.httpStatus || request.downloaded)) {
        firstByte = std::chrono::steady_clock::now();
        sawFirstByte = true;
      }
      if (m_progressCallback) {
        using ProgressCallback = int (*)(void *, double, double, double, double);
        progressFailed = reinterpret_cast<ProgressCallback>(m_progressCallback)(
            m_progressContext, static_cast<double>(request.downloadTotal),
            static_cast<double>(request.downloaded),
            static_cast<double>(request.uploadTotal),
            static_cast<double>(request.uploaded)) != 0;
      }
      if (progressFailed) {
        MAIN_THREAD_EM_ASM({
          const xhr = globalThis.playgroundXhrRequests?.get($0);
          if (xhr)
            xhr.abort();
        }, &request);
      }
      emscripten_atomic_cas_u32(&request.state, XhrRequestProgress,
                                XhrRequestPending);
      continue;
    }
    break;
  }

  m_web->httpCode = request.httpStatus;
  using HeaderCallback = std::size_t (*)(void *, std::size_t, std::size_t, void *);
  using WriteCallback = std::size_t (*)(char *, std::size_t, std::size_t, void *);
  bool headerFailed = false;
  if (request.state == XhrRequestDone && m_web->headerCallback) {
    const auto callback =
        reinterpret_cast<HeaderCallback>(m_web->headerCallback);
    const std::string statusLine =
        "HTTP/1.1 " + std::to_string(request.httpStatus) + "\r\n";
    headerFailed = callback(const_cast<char *>(statusLine.data()), 1,
                            statusLine.size(), m_web->callbackContext) !=
                   statusLine.size();
    std::string headers(reinterpret_cast<const char *>(request.headers),
                        request.headersLength);
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
  std::size_t written = request.length;
  if (request.state == XhrRequestDone && m_web->writeCallback && request.length) {
    written = reinterpret_cast<WriteCallback>(m_web->writeCallback)(
        reinterpret_cast<char *>(request.data), 1, request.length,
        m_web->callbackContext);
  }
  if (!progressFailed && request.state == XhrRequestDone && m_progressCallback) {
    using ProgressCallback = int (*)(void *, double, double, double, double);
    progressFailed = reinterpret_cast<ProgressCallback>(m_progressCallback)(
        m_progressContext, static_cast<double>(request.downloadTotal),
        static_cast<double>(request.downloaded),
        static_cast<double>(request.uploadTotal),
        static_cast<double>(request.uploaded)) != 0;
  }
  const auto finished = std::chrono::steady_clock::now();
  m_web->metrics.totalSeconds =
      std::chrono::duration<double>(finished - started).count();
  m_web->metrics.firstByteSeconds = sawFirstByte
      ? std::chrono::duration<double>(firstByte - started).count()
      : m_web->metrics.totalSeconds;
  m_web->metrics.downloadedBytes = static_cast<double>(request.length);
  m_web->metrics.averageBytesPerSecond = m_web->metrics.totalSeconds > 0
      ? m_web->metrics.downloadedBytes / m_web->metrics.totalSeconds : 0;
  m_web->metrics.connectionCount = 1;
  m_web->metrics.attemptNumber = m_performCount;
  // Match libcurl's transport contract: an HTTP 4xx/5xx response is still a
  // successfully completed transfer.  The engine consumes getHttpCode() and
  // the response body to decide how to handle server errors.
  const bool success = request.state == XhrRequestDone &&
                       request.httpStatus != 0 && written == request.length &&
                       !headerFailed && !progressFailed;
  if (!success) {
    std::fprintf(stderr, "web xhr failed: status=%u bytes=%u url=%s\n",
                 request.httpStatus, request.length,
                 m_web->url.c_str());
  }
  const bool writeFailed = written != request.length || headerFailed;
  std::free(reinterpret_cast<void *>(request.data));
  std::free(reinterpret_cast<void *>(request.headers));
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
