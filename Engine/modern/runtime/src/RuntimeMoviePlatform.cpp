#include "Playground/Runtime/RuntimePlatform.h"

#include "CKLBTextureMovie.h"

#include <GLES2/gl2.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

namespace playground::runtime {
namespace {

class DesktopMovie final : public IMovieInterface {
public:
  DesktopMovie() = default;
  ~DesktopMovie() override {
    if (m_texture)
      glDeleteTextures(1, &m_texture);
    sws_freeContext(m_scaler);
    av_frame_free(&m_frame);
    av_packet_free(&m_packet);
    avcodec_free_context(&m_codec);
    avformat_close_input(&m_format);
  }

  bool initialize(const char *url, int requestedWidth, int requestedHeight) {
    if (!url || !url[0])
      return false;
    const char *input = url;
    const char *resolved = nullptr;
    if (!std::strstr(url, "://") || !std::strncmp(url, "asset://", 8) ||
        !std::strncmp(url, "file://", 7)) {
      resolved = CPFInterface::getInstance().platform().getFullPath(url);
      input = resolved;
    }
    const int openResult =
        avformat_open_input(&m_format, input, nullptr, nullptr);
    delete[] resolved;
    if (openResult < 0 || avformat_find_stream_info(m_format, nullptr) < 0)
      return false;

    const AVCodec *decoder = nullptr;
    m_streamIndex =
        av_find_best_stream(m_format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (m_streamIndex < 0 || !decoder)
      return false;
    m_codec = avcodec_alloc_context3(decoder);
    if (!m_codec ||
        avcodec_parameters_to_context(
            m_codec, m_format->streams[m_streamIndex]->codecpar) < 0 ||
        avcodec_open2(m_codec, decoder, nullptr) < 0)
      return false;

    m_width = requestedWidth > 0 ? requestedWidth : m_codec->width;
    m_height = requestedHeight > 0 ? requestedHeight : m_codec->height;
    if (m_width <= 0 || m_height <= 0)
      return false;
    m_pixels.resize(static_cast<size_t>(m_width) * m_height * 4);
    m_frame = av_frame_alloc();
    m_packet = av_packet_alloc();
    if (!m_frame || !m_packet)
      return false;

    AVRational rate = av_guess_frame_rate(
        m_format, m_format->streams[m_streamIndex], nullptr);
    m_frameDuration =
        rate.num > 0 && rate.den > 0
            ? std::chrono::nanoseconds(1000000000LL * rate.den / rate.num)
            : std::chrono::milliseconds(33);
    m_nextFrame = std::chrono::steady_clock::now();
    m_infoReady = true;
    return true;
  }

  u32 getTextureTarget() override { return GL_TEXTURE_2D; }
  bool isInfoReady() override { return m_infoReady; }
  bool isFrameReady() override { return m_frameReady; }
  void refreshTexture(u32 *textureName) override {
    if (!m_frameReady) {
      if (textureName)
        *textureName = m_texture;
      return;
    }
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
    if (textureName)
      *textureName = m_texture;
  }
  u32 getTextureName() override { return m_texture; }
  void getUV(float *uv) override {
    if (!uv)
      return;
    uv[0] = 0.0f;
    uv[1] = 0.0f;
    uv[2] = 1.0f;
    uv[3] = 1.0f;
  }
  void getSize(s32 *size) override {
    if (!size)
      return;
    size[0] = m_width;
    size[1] = m_height;
  }
  void update() override {
    if (!m_playing || std::chrono::steady_clock::now() < m_nextFrame)
      return;
    if (decodeFrame()) {
      m_nextFrame += m_frameDuration;
      const auto now = std::chrono::steady_clock::now();
      if (m_nextFrame + m_frameDuration * 4 < now)
        m_nextFrame = now;
    }
  }
  void setPlay() override {
    m_playing = true;
    m_nextFrame = std::chrono::steady_clock::now();
  }
  void setPause() override { m_playing = false; }
  void resetLoop() override {
    avcodec_flush_buffers(m_codec);
    av_seek_frame(m_format, m_streamIndex, 0, AVSEEK_FLAG_BACKWARD);
    m_endReached = false;
    m_playing = true;
    m_nextFrame = std::chrono::steady_clock::now();
  }
  void release() override { m_playing = false; }

private:
  bool decodeFrame() {
    while (true) {
      int result = avcodec_receive_frame(m_codec, m_frame);
      if (result == 0) {
        m_scaler = sws_getCachedContext(
            m_scaler, m_frame->width, m_frame->height,
            static_cast<AVPixelFormat>(m_frame->format), m_width, m_height,
            AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_scaler)
          return false;
        uint8_t *destination[] = {m_pixels.data()};
        int strides[] = {m_width * 4};
        sws_scale(m_scaler, m_frame->data, m_frame->linesize, 0,
                  m_frame->height, destination, strides);
        m_frameReady = true;
        return true;
      }
      if (result != AVERROR(EAGAIN) && result != AVERROR_EOF)
        return false;

      av_packet_unref(m_packet);
      result = av_read_frame(m_format, m_packet);
      if (result < 0) {
        avcodec_send_packet(m_codec, nullptr);
        if (!m_endReached) {
          m_endReached = true;
          continue;
        }
        m_playing = false;
        return false;
      }
      if (m_packet->stream_index != m_streamIndex)
        continue;
      result = avcodec_send_packet(m_codec, m_packet);
      if (result < 0 && result != AVERROR(EAGAIN))
        return false;
    }
  }

  AVFormatContext *m_format{};
  AVCodecContext *m_codec{};
  AVFrame *m_frame{};
  AVPacket *m_packet{};
  SwsContext *m_scaler{};
  std::vector<unsigned char> m_pixels;
  std::chrono::steady_clock::time_point m_nextFrame;
  std::chrono::nanoseconds m_frameDuration{std::chrono::milliseconds(33)};
  int m_streamIndex{-1};
  int m_width{};
  int m_height{};
  GLuint m_texture{};
  bool m_infoReady{};
  bool m_frameReady{};
  bool m_playing{};
  bool m_endReached{};
};

} // namespace

IMovieInterface *RuntimePlatform::createMoviePlayer(const char *url, int width,
                                                    int height) {
  auto *movie = new DesktopMovie;
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
