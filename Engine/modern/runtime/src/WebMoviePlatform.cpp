#include "Playground/Runtime/RuntimePlatform.h"

#include "CKLBTextureMovie.h"
#include "CPFInterface.h"

#include <GLES2/gl2.h>
#include <emscripten.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace playground::runtime {
namespace {

std::atomic<int> NextMovieId{1};

class WebMovie final : public IMovieInterface {
public:
  ~WebMovie() override {
    if (m_texture)
      glDeleteTextures(1, &m_texture);
    if (m_id) {
      MAIN_THREAD_EM_ASM({
        const movies = globalThis.playgroundMovies;
        const movie = movies && movies.get($0);
        if (movie) {
          movie.video.pause();
          URL.revokeObjectURL(movie.url);
          movies.delete($0);
        }
      }, m_id);
    }
  }

  bool initialize(const char *url, int requestedWidth, int requestedHeight) {
    if (!url || !url[0])
      return false;
    const char *resolved = CPFInterface::getInstance().platform().getFullPath(url);
    std::ifstream input(resolved ? resolved : url, std::ios::binary);
    delete[] resolved;
    if (!input)
      return false;
    m_encoded.assign(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
    if (m_encoded.empty())
      return false;
    m_id = NextMovieId.fetch_add(1);
    m_requestedWidth = requestedWidth;
    m_requestedHeight = requestedHeight;
    const int created = MAIN_THREAD_EM_ASM_INT({
      try {
        if (!globalThis.playgroundMovies) globalThis.playgroundMovies = new Map();
        const bytes = HEAPU8.slice($1, $1 + $2);
        const url = URL.createObjectURL(new Blob([bytes]));
        const video = document.createElement('video');
        video.preload = 'auto';
        video.playsInline = true;
        video.crossOrigin = 'anonymous';
        video.src = url;
        video.load();
        globalThis.playgroundMovies.set($0, {
          video, url, canvas: new OffscreenCanvas(1, 1), context: null,
          requestedWidth: $3, requestedHeight: $4
        });
        return 1;
      } catch (error) {
        console.error('movie creation failed', error);
        return 0;
      }
    }, m_id, m_encoded.data(), m_encoded.size(), requestedWidth,
       requestedHeight);
    return created != 0;
  }

  u32 getTextureTarget() override { return GL_TEXTURE_2D; }
  bool isInfoReady() override {
    refreshInfo();
    return m_infoReady;
  }
  bool isFrameReady() override { return m_frameReady; }
  void refreshTexture(u32 *textureName) override {
    if (m_frameReady) {
      if (!m_texture) {
        glGenTextures(1, &m_texture);
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, m_pixels.data());
      } else {
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width, m_height, GL_RGBA,
                        GL_UNSIGNED_BYTE, m_pixels.data());
      }
      m_frameReady = false;
    }
    if (textureName)
      *textureName = m_texture;
  }
  u32 getTextureName() override { return m_texture; }
  void getUV(float *uv) override {
    if (uv) {
      uv[0] = 0.0f;
      uv[1] = 0.0f;
      uv[2] = 1.0f;
      uv[3] = 1.0f;
    }
  }
  void getSize(s32 *size) override {
    refreshInfo();
    if (size) {
      size[0] = m_width;
      size[1] = m_height;
    }
  }
  void update() override {
    if (!refreshInfo() || m_pixels.empty())
      return;
    const int result = MAIN_THREAD_EM_ASM_INT({
      const movie = globalThis.playgroundMovies.get($0);
      if (!movie || movie.video.readyState < HTMLMediaElement.HAVE_CURRENT_DATA)
        return 0;
      try {
        movie.context.drawImage(movie.video, 0, 0, $2, $3);
        const frame = movie.context.getImageData(0, 0, $2, $3);
        HEAPU8.set(frame.data, $1);
        return 1;
      } catch (error) {
        console.error('movie frame transfer failed', error);
        return -1;
      }
    }, m_id, m_pixels.data(), m_width, m_height);
    m_frameReady = result > 0;
  }
  void setPlay() override {
    MAIN_THREAD_EM_ASM({
      const movie = globalThis.playgroundMovies.get($0);
      if (movie) movie.video.play().catch(error =>
        console.warn('movie playback requires user activation', error));
    }, m_id);
  }
  void setPause() override {
    MAIN_THREAD_EM_ASM({
      const movie = globalThis.playgroundMovies.get($0);
      if (movie) movie.video.pause();
    }, m_id);
  }
  void resetLoop() override {
    MAIN_THREAD_EM_ASM({
      const movie = globalThis.playgroundMovies.get($0);
      if (movie) { movie.video.currentTime = 0; movie.video.play().catch(() => {}); }
    }, m_id);
  }
  void release() override { setPause(); }

private:
  bool refreshInfo() {
    if (m_infoReady)
      return true;
    int dimensions[2]{};
    const int ready = MAIN_THREAD_EM_ASM_INT({
      const movie = globalThis.playgroundMovies.get($0);
      if (!movie || movie.video.readyState < HTMLMediaElement.HAVE_METADATA)
        return 0;
      HEAP32[$1 >> 2] = movie.requestedWidth > 0
          ? movie.requestedWidth : movie.video.videoWidth;
      HEAP32[($1 + 4) >> 2] = movie.requestedHeight > 0
          ? movie.requestedHeight : movie.video.videoHeight;
      return 1;
    }, m_id, dimensions);
    if (!ready || dimensions[0] <= 0 || dimensions[1] <= 0)
      return false;
    m_width = dimensions[0];
    m_height = dimensions[1];
    m_pixels.resize(static_cast<std::size_t>(m_width) * m_height * 4);
    MAIN_THREAD_EM_ASM({
      const movie = globalThis.playgroundMovies.get($0);
      movie.canvas.width = $1;
      movie.canvas.height = $2;
      movie.context = movie.canvas.getContext('2d', {alpha: false,
                                                      willReadFrequently: true});
    }, m_id, m_width, m_height);
    m_encoded.clear();
    m_encoded.shrink_to_fit();
    m_infoReady = true;
    return true;
  }

  std::vector<unsigned char> m_encoded;
  std::vector<unsigned char> m_pixels;
  int m_id{};
  int m_requestedWidth{};
  int m_requestedHeight{};
  int m_width{};
  int m_height{};
  GLuint m_texture{};
  bool m_infoReady{};
  bool m_frameReady{};
};

} // namespace

IMovieInterface *RuntimePlatform::createMoviePlayer(const char *url, int width,
                                                    int height) {
  auto *movie = new WebMovie;
  if (!movie->initialize(url, width, height)) {
    delete movie;
    return nullptr;
  }
  return movie;
}

void RuntimePlatform::destroyMoviePlayer(IMovieInterface *movie) {
  delete movie;
}

} // namespace playground::runtime
