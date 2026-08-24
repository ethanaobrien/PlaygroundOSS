#include "RuntimeWidgets.h"

#include "Playground/Runtime/RuntimePlatform.h"

#include "CPFInterface.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#if defined(__SWITCH__)
#include "Playground/Switch/SwitchSystem.h"
#include <GLES2/gl2.h>
#elif defined(__WIIU__)
#include "Playground/Host/WiiUHost.h"
#include <GLES2/gl2.h>
#elif !defined(__WIIU__)
#include <SDL3/SDL_keycode.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace playground::runtime {
namespace {

class DesktopWidget : public IWidget {
public:
  DesktopWidget(IWidget::CONTROL type, int id, const char *caption, int x,
                int y, int width, int height)
      : m_type(type), m_id(id), m_text(caption ? caption : ""), m_x(x), m_y(y),
        m_width(width), m_height(height) {}
  ~DesktopWidget() override {
#if defined(__EMSCRIPTEN__)
    if (m_webTextInput) {
      MAIN_THREAD_EM_ASM({
        if (globalThis.playgroundDestroyTextInput)
          globalThis.playgroundDestroyTextInput($0);
      }, reinterpret_cast<std::uintptr_t>(this));
    }
#endif
  }

  int getTextLength() override { return static_cast<int>(m_text.size()); }
  bool getText(char *buffer, int length) override {
    if (!buffer || length <= 0)
      return false;
    std::snprintf(buffer, static_cast<size_t>(length), "%s", m_text.c_str());
    return static_cast<int>(m_text.size()) < length;
  }
  bool setText(const char *text) override {
    m_text = text ? text : "";
    constrainLength();
#if defined(__EMSCRIPTEN__)
    updateWebText();
#endif
    return true;
  }
  void move(int x, int y) override {
    m_x = x;
    m_y = y;
#if defined(__EMSCRIPTEN__)
    updateWebGeometry();
#endif
  }
  void resize(int width, int height) override {
    m_width = width;
    m_height = height;
#if defined(__EMSCRIPTEN__)
    updateWebGeometry();
#endif
  }
  void visible(bool visible) override {
    m_visible = visible;
#if defined(__EMSCRIPTEN__)
    updateWebGeometry();
#endif
  }
  void enable(bool enabled) override {
    m_enabled = enabled;
#if defined(__EMSCRIPTEN__)
    updateWebGeometry();
#endif
  }
  int status() override { return m_status; }

  IWidget::CONTROL type() const { return m_type; }
  bool editable() const {
    return (m_type == TEXTBOX || m_type == PASSWDBOX) && m_visible && m_enabled;
  }
  bool contains(float x, float y) const {
    return editable() && x >= static_cast<float>(m_x) &&
           y >= static_cast<float>(m_y) &&
           x < static_cast<float>(m_x + m_width) &&
           y < static_cast<float>(m_y + m_height);
  }
  void append(const char *text) {
    if (text)
      m_text += text;
    constrainLength();
  }
  void eraseLastCodepoint() {
    if (m_text.empty())
      return;
    size_t position = m_text.size() - 1;
    while (position &&
           (static_cast<unsigned char>(m_text[position]) & 0xc0) == 0x80)
      --position;
    m_text.erase(position);
#if defined(__EMSCRIPTEN__)
    updateWebText();
#endif
  }

#if defined(__EMSCRIPTEN__)
  void createWebTextInput() {
    if (m_type != TEXTBOX && m_type != PASSWDBOX)
      return;
    m_webTextInput = true;
    MAIN_THREAD_EM_ASM({
      if (globalThis.playgroundCreateTextInput) {
        globalThis.playgroundCreateTextInput(
          $0, !!$1, UTF8ToString($2), $3, $4, $5, $6);
      }
    }, reinterpret_cast<std::uintptr_t>(this), m_type == PASSWDBOX,
       m_text.c_str(), m_x, m_y, m_width, m_height);
    updateWebGeometry();
  }

  bool syncWebText() {
    if (!m_webTextInput)
      return false;
    const int byteLength = MAIN_THREAD_EM_ASM_INT({
      return globalThis.playgroundGetTextInputLength
        ? globalThis.playgroundGetTextInputLength($0) : -1;
    }, reinterpret_cast<std::uintptr_t>(this));
    if (byteLength < 0)
      return false;
    std::vector<char> value(static_cast<std::size_t>(byteLength) + 1);
    MAIN_THREAD_EM_ASM({
      if (globalThis.playgroundCopyTextInputValue)
        globalThis.playgroundCopyTextInputValue($0, $1, $2);
    }, reinterpret_cast<std::uintptr_t>(this), value.data(), value.size());
    const std::string previous = m_text;
    m_text = value.data();
    constrainLength();
    if (m_text != value.data())
      updateWebText();
    return m_text != previous;
  }

  bool webFocused() const {
    return m_webTextInput && MAIN_THREAD_EM_ASM_INT({
      return globalThis.playgroundTextInputFocused
        ? globalThis.playgroundTextInputFocused($0) : false;
    }, reinterpret_cast<std::uintptr_t>(this));
  }

  void updateWebPlaceholder() {
    if (!m_webTextInput)
      return;
    MAIN_THREAD_EM_ASM({
      if (globalThis.playgroundSetTextInputPlaceholder)
        globalThis.playgroundSetTextInputPlaceholder($0, UTF8ToString($1));
    }, reinterpret_cast<std::uintptr_t>(this), m_placeholder.c_str());
  }
#endif

protected:
  void constrainLength() {
    if (m_maxLength <= 0 || m_text.size() <= static_cast<size_t>(m_maxLength))
      return;
    size_t length = static_cast<size_t>(m_maxLength);
    while (length && length < m_text.size() &&
           (static_cast<unsigned char>(m_text[length]) & 0xc0) == 0x80)
      --length;
    m_text.resize(length);
  }

  IWidget::CONTROL m_type;
  int m_id;
  std::string m_text;
  std::string m_placeholder;
  int m_x;
  int m_y;
  int m_width;
  int m_height;
  int m_maxLength{-1};
  int m_charType{TXCH_7BIT_ASCII | TXCH_UTF8};
  int m_alignment{TX_ALIGN_LEFT};
  int m_status{};
  bool m_visible{true};
  bool m_enabled{true};
#if defined(__EMSCRIPTEN__)
  bool m_webTextInput{};

  void updateWebGeometry() {
    if (!m_webTextInput)
      return;
    MAIN_THREAD_EM_ASM({
      if (globalThis.playgroundUpdateTextInput) {
        globalThis.playgroundUpdateTextInput(
          $0, $1, $2, $3, $4, !!$5, !!$6);
      }
    }, reinterpret_cast<std::uintptr_t>(this), m_x, m_y, m_width, m_height,
       m_visible, m_enabled);
  }

  void updateWebText() {
    if (!m_webTextInput)
      return;
    MAIN_THREAD_EM_ASM({
      if (globalThis.playgroundSetTextInputValue)
        globalThis.playgroundSetTextInputValue($0, UTF8ToString($1));
    }, reinterpret_cast<std::uintptr_t>(this), m_text.c_str());
  }
#endif
};

class DesktopTextWidget final : public DesktopWidget {
public:
  using DesktopWidget::DesktopWidget;
  int getTextMaxLength() override { return m_maxLength; }
#if defined(__WIIU__) || defined(__SWITCH__)
  void *font() const { return m_font; }
  int alignment() const { return m_alignment; }
  unsigned int foreground() const { return m_foreground; }
  unsigned int background() const { return m_background; }
  const std::string &placeholder() const { return m_placeholder; }
  int x() const { return m_x; }
  int y() const { return m_y; }
  int width() const { return m_width; }
  int height() const { return m_height; }
  bool shown() const { return m_visible && m_width > 0 && m_height > 0; }
  const std::string &value() const { return m_text; }
#endif
  void cmd(int command, ...) override {
    va_list arguments;
    va_start(arguments, command);
    switch (command) {
    case TX_FONT:
      m_font = va_arg(arguments, void *);
      break;
    case TX_PLACEHOLDER: {
      const char *placeholder = va_arg(arguments, const char *);
      m_placeholder = placeholder ? placeholder : "";
#if defined(__EMSCRIPTEN__)
      updateWebPlaceholder();
#endif
      break;
    }
    case TX_MAXLEN:
      m_maxLength = va_arg(arguments, int);
      constrainLength();
      break;
    case TX_CHARTYPE:
      m_charType = va_arg(arguments, int);
      break;
    case TX_ALIGNMENTTYPE:
      m_alignment = va_arg(arguments, int);
#if defined(__EMSCRIPTEN__)
      MAIN_THREAD_EM_ASM({
        if (globalThis.playgroundSetTextInputAlignment)
          globalThis.playgroundSetTextInputAlignment($0, $1);
      }, reinterpret_cast<std::uintptr_t>(this), m_alignment);
#endif
      break;
    default: {
      const unsigned int color = va_arg(arguments, unsigned int);
#if defined(__EMSCRIPTEN__)
      int slot = -1;
      if (command == TX_BGCOLOR_NORMAL)
        slot = 0;
      else if (command == TX_FGCOLOR_NORMAL)
        slot = 1;
      else if (command == TX_BGCOLOR_TOUCH)
        slot = 2;
      else if (command == TX_FGCOLOR_TOUCH)
        slot = 3;
      if (slot >= 0) {
        MAIN_THREAD_EM_ASM({
          if (globalThis.playgroundSetTextInputColor)
            globalThis.playgroundSetTextInputColor($0, $1, $2);
        }, reinterpret_cast<std::uintptr_t>(this), slot, color);
      }
#else
      if (command == TX_BGCOLOR_NORMAL)
        m_background = color;
      else if (command == TX_FGCOLOR_NORMAL)
        m_foreground = color;
#endif
      break;
    }
    }
    va_end(arguments);
  }

private:
  void *m_font{};
  unsigned int m_background{0xffffffffu};
  unsigned int m_foreground{0xff000000u};
};

class DesktopWebWidget final : public DesktopWidget {
public:
  DesktopWebWidget(IWidget::CONTROL type, int id, const char *caption, int x,
                   int y, int width, int height)
      : DesktopWidget(type, id, caption, x, y, width, height) {}
  void cmd(int command, ...) override {
    va_list arguments;
    va_start(arguments, command);
    if (command == WEB_SET_SCALESPAGETOFIT)
      m_scaleToFit = va_arg(arguments, int) != 0;
    else if (command == WEB_BGCOLOR_NORMAL) {
      m_backgroundAlpha = va_arg(arguments, unsigned int);
      m_backgroundColor = va_arg(arguments, unsigned int);
    } else if (command == WEB_SET_WHITEURL) {
      const char *url = va_arg(arguments, const char *);
      m_whiteUrl = url ? url : "";
    }
    va_end(arguments);
  }
  const std::string &url() const { return m_text; }

private:
  std::string m_whiteUrl;
  unsigned int m_backgroundAlpha{};
  unsigned int m_backgroundColor{};
  bool m_scaleToFit{};
};

class DesktopMovieWidget final : public DesktopWidget {
public:
  DesktopMovieWidget(RuntimePlatform *platform, IWidget::CONTROL type, int id,
                     const char *caption, int x, int y, int width, int height)
      : DesktopWidget(type, id, caption, x, y, width, height),
        m_platform(platform) {}
  void cmd(int command, ...) override {
    if (command == MV_PLAY || command == MV_RESUME) {
      m_status = 0;
      if (!m_text.empty()) {
        std::string url = m_text;
        if (!url.compare(0, 8, "asset://") || !url.compare(0, 7, "file://")) {
          const char *resolved = m_platform->getFullPath(url.c_str(), nullptr);
          url = std::string("file://") + (resolved ? resolved : "");
          delete[] resolved;
        }
        if (!m_platform->openExternalUrl(url.c_str()))
          m_status = MV_FINISHED;
      }
    } else if (command == MV_STOP) {
      m_status = MV_FINISHED;
    } else if (command == MV_PAUSE) {
      m_status = MV_FINISHED;
    }
  }

private:
  RuntimePlatform *m_platform;
};

class DesktopActivityWidget final : public DesktopWidget {
public:
  using DesktopWidget::DesktopWidget;
  void cmd(int command, ...) override {
    va_list arguments;
    va_start(arguments, command);
    if (command == ACT_STARTANIM)
      m_status = 1;
    else if (command == ACT_STOPANIM)
      m_status = 0;
    else if (command == ACT_SET_STYLE)
      m_style = va_arg(arguments, int);
    va_end(arguments);
  }

private:
  int m_style{};
};

struct WidgetEvent {
  IClientRequest::EVENT_TYPE type;
  IWidget *widget;
  std::string data;
};

#if defined(__WIIU__) || defined(__SWITCH__)
GLuint compileWidgetShader(GLenum type, const char *source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint compiled = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled) {
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

void unpackArgb(unsigned int color, unsigned char &red, unsigned char &green,
                unsigned char &blue, unsigned char &alpha) {
  alpha = static_cast<unsigned char>(color >> 24);
  red = static_cast<unsigned char>(color >> 16);
  green = static_cast<unsigned char>(color >> 8);
  blue = static_cast<unsigned char>(color);
}
#endif

} // namespace

class RuntimeWidgetManager::Impl {
public:
  explicit Impl(RuntimePlatform *platform) : platform(platform) {}

  void notifyText(DesktopWidget *widget) {
    if (!CPFInterface::getInstance().isClient())
      return;
    std::vector<char> text(static_cast<size_t>(widget->getTextLength()) + 1);
    widget->getText(text.data(), static_cast<int>(text.size()));
    CPFInterface::getInstance().client().controlEvent(
        IClientRequest::E_TEXTCHANGE, widget, text.size(), text.data(), 0,
        nullptr);
  }

  RuntimePlatform *platform;
  std::vector<DesktopWidget *> widgets;
  DesktopWidget *activeText{};
  std::deque<WidgetEvent> events;
#if defined(__SWITCH__)
  std::deque<DesktopTextWidget *> pendingSwitchText;
#endif
#if defined(__WIIU__)
  std::deque<DesktopTextWidget *> pendingWiiUText;
#endif
#if defined(__WIIU__) || defined(__SWITCH__)
  GLuint widgetProgram{};
  GLuint widgetTexture{};
  GLint widgetPosition{-1};
  GLint widgetTexcoord{-1};
#endif
};

RuntimeWidgetManager::RuntimeWidgetManager(RuntimePlatform *platform)
    : m_impl(std::make_unique<Impl>(platform)) {}
RuntimeWidgetManager::~RuntimeWidgetManager() {
  for (DesktopWidget *widget : m_impl->widgets)
    delete widget;
#if defined(__WIIU__) || defined(__SWITCH__)
  if (m_impl->widgetTexture)
    glDeleteTextures(1, &m_impl->widgetTexture);
  if (m_impl->widgetProgram)
    glDeleteProgram(m_impl->widgetProgram);
#endif
}

IWidget *RuntimeWidgetManager::create(IWidget::CONTROL type, int id,
                                      const char *caption, int x, int y,
                                      int width, int height,
                                      va_list arguments) {
  DesktopWidget *widget = nullptr;
  if (type == IWidget::TEXTBOX || type == IWidget::PASSWDBOX) {
    auto *text = new DesktopTextWidget(type, id, caption, x, y, width, height);
    text->cmd(IWidget::TX_MAXLEN, va_arg(arguments, int));
    widget = text;
    m_impl->activeText = text;
#if defined(__EMSCRIPTEN__)
    text->createWebTextInput();
#endif
  } else if (type == IWidget::WEBVIEW || type == IWidget::WEBNOJUMP) {
    for (int index = 0; index < 8; ++index)
      (void)va_arg(arguments, const char *);
    widget = new DesktopWebWidget(type, id, caption, x, y, width, height);
    m_impl->events.push_back(
        {IClientRequest::E_DIDSTARTLOADWEB, widget, caption ? caption : ""});
    const bool opened = caption && caption[0] &&
                        m_impl->platform->openExternalUrl(caption);
    m_impl->events.push_back({opened ? IClientRequest::E_DIDLOADENDWEB
                                     : IClientRequest::E_FAILEDLOADWEB,
                              widget, caption ? caption : ""});
  } else if (type == IWidget::MOVIEPLAYER || type == IWidget::BGMOVIEPLAYER) {
    widget = new DesktopMovieWidget(m_impl->platform, type, id, caption, x, y,
                                    width, height);
  } else if (type == IWidget::ACTIVITYINDICATOR) {
    widget = new DesktopActivityWidget(type, id, caption, x, y, width, height);
  }
  if (widget)
    m_impl->widgets.push_back(widget);
  return widget;
}

void RuntimeWidgetManager::destroy(IWidget *widget) {
  auto *desktop = static_cast<DesktopWidget *>(widget);
  if (m_impl->activeText == desktop)
    m_impl->activeText = nullptr;
#if defined(__SWITCH__)
  m_impl->pendingSwitchText.erase(
      std::remove(m_impl->pendingSwitchText.begin(),
                  m_impl->pendingSwitchText.end(), desktop),
      m_impl->pendingSwitchText.end());
#endif
#if defined(__WIIU__)
  m_impl->pendingWiiUText.erase(
      std::remove(m_impl->pendingWiiUText.begin(),
                  m_impl->pendingWiiUText.end(), desktop),
      m_impl->pendingWiiUText.end());
#endif
  auto found =
      std::find(m_impl->widgets.begin(), m_impl->widgets.end(), desktop);
  if (found != m_impl->widgets.end())
    m_impl->widgets.erase(found);
  m_impl->events.erase(std::remove_if(m_impl->events.begin(),
                                      m_impl->events.end(),
                                      [widget](const WidgetEvent &event) {
                                        return event.widget == widget;
                                      }),
                       m_impl->events.end());
  delete desktop;
}

void RuntimeWidgetManager::inputText(const char *text) {
  if (!m_impl->activeText || !m_impl->activeText->editable())
    return;
  m_impl->activeText->append(text);
  m_impl->notifyText(m_impl->activeText);
}

bool RuntimeWidgetManager::inputKey(int key, bool pressed) {
  if (!pressed || !m_impl->activeText || !m_impl->activeText->editable())
    return false;
#if defined(__SWITCH__)
  constexpr int Backspace = 8;
  constexpr int Return = 13;
  constexpr int KeypadEnter = 13;
#else
#if defined(__WIIU__)
  constexpr int Backspace = 8;
  constexpr int Return = 13;
  constexpr int KeypadEnter = 13;
#else
  constexpr int Backspace = SDLK_BACKSPACE;
  constexpr int Return = SDLK_RETURN;
  constexpr int KeypadEnter = SDLK_KP_ENTER;
#endif
#endif
  if (key == Backspace) {
    m_impl->activeText->eraseLastCodepoint();
    m_impl->notifyText(m_impl->activeText);
    return true;
  }
  return key == Return || key == KeypadEnter;
}

bool RuntimeWidgetManager::pointerDown(float x, float y) {
#if defined(__WIIU__) || defined(__SWITCH__)
  for (auto widget = m_impl->widgets.rbegin();
       widget != m_impl->widgets.rend(); ++widget) {
    if ((*widget)->type() != IWidget::TEXTBOX &&
        (*widget)->type() != IWidget::PASSWDBOX)
      continue;
    auto *text = static_cast<DesktopTextWidget *>(*widget);
    if (!text->contains(x, y))
      continue;
    m_impl->activeText = text;
#if defined(__SWITCH__)
    if (std::find(m_impl->pendingSwitchText.begin(),
                  m_impl->pendingSwitchText.end(), text) ==
        m_impl->pendingSwitchText.end())
      m_impl->pendingSwitchText.push_back(text);
#else
    if (std::find(m_impl->pendingWiiUText.begin(),
                  m_impl->pendingWiiUText.end(), text) ==
        m_impl->pendingWiiUText.end())
      m_impl->pendingWiiUText.push_back(text);
#endif
    return true;
  }
#else
  (void)x;
  (void)y;
#endif
  return false;
}

void RuntimeWidgetManager::pumpEvents() {
#if defined(__SWITCH__)
  if (!m_impl->pendingSwitchText.empty()) {
    DesktopTextWidget *text = m_impl->pendingSwitchText.front();
    m_impl->pendingSwitchText.pop_front();
    const int maximum = text->getTextMaxLength();
    const std::uint32_t maximumCharacters =
        maximum > 0 ? static_cast<std::uint32_t>(maximum) : 4096;
    std::vector<char> initial(static_cast<std::size_t>(text->getTextLength()) +
                              1);
    text->getText(initial.data(), static_cast<int>(initial.size()));
    std::string entered;
    if (switch_runtime::showSoftwareKeyboard(
            text->type() == IWidget::PASSWDBOX, initial.data(),
            maximumCharacters, entered)) {
      text->setText(entered.c_str());
      m_impl->notifyText(text);
    }
  }
#endif
#if defined(__WIIU__)
  if (!m_impl->pendingWiiUText.empty()) {
    DesktopTextWidget *text = m_impl->pendingWiiUText.front();
    m_impl->pendingWiiUText.pop_front();
    const int maximum = text->getTextMaxLength();
    const std::uint32_t maximumCharacters =
        maximum > 0 ? static_cast<std::uint32_t>(maximum) : 4096;
    std::vector<char> initial(static_cast<std::size_t>(text->getTextLength()) +
                              1);
    text->getText(initial.data(), static_cast<int>(initial.size()));
    std::vector<char> entered(
        static_cast<std::size_t>(maximumCharacters) * 4 + 1);
    std::fprintf(stderr,
                 "wiiu text widget: opening deferred system keyboard type=%d "
                 "maximum=%u initial-bytes=%u\n",
                 static_cast<int>(text->type()), maximumCharacters,
                 static_cast<unsigned>(initial.size() - 1));
    if (playgroundWiiUHostShowSoftwareKeyboard(
            playgroundWiiUHostGetActive(),
            text->type() == IWidget::PASSWDBOX, initial.data(), nullptr,
            maximumCharacters, entered.data(),
            static_cast<std::uint32_t>(entered.size()))) {
      std::fprintf(stderr,
                   "wiiu text widget: system keyboard accepted bytes=%u\n",
                   static_cast<unsigned>(std::strlen(entered.data())));
      text->setText(entered.data());
    } else {
      std::fprintf(stderr,
                   "wiiu text widget: system keyboard cancelled or failed\n");
    }
  }
#endif
#if defined(__EMSCRIPTEN__)
  for (DesktopWidget *widget : m_impl->widgets) {
    if (widget->webFocused())
      m_impl->activeText = widget;
    if (widget->syncWebText())
      m_impl->notifyText(widget);
  }
#endif
  if (!CPFInterface::getInstance().isClient())
    return;
  while (!m_impl->events.empty()) {
    WidgetEvent event = std::move(m_impl->events.front());
    m_impl->events.pop_front();
    void *data = event.data.empty() ? nullptr : event.data.data();
    const size_t size = event.data.empty() ? 0 : event.data.size() + 1;
    CPFInterface::getInstance().client().controlEvent(event.type, event.widget,
                                                      size, data, 0, nullptr);
  }
}

void RuntimeWidgetManager::render() {
#if defined(__WIIU__) || defined(__SWITCH__)
  if (!m_impl->widgetProgram) {
    static const char vertexSource[] =
        "attribute vec2 position;\n"
        "attribute vec2 texcoord;\n"
        "varying vec2 uv;\n"
        "void main() { gl_Position = vec4(position, 0.0, 1.0); uv = texcoord; }\n";
    static const char fragmentSource[] =
        "precision mediump float;\n"
        "varying vec2 uv;\n"
        "uniform sampler2D image;\n"
        "void main() { gl_FragColor = texture2D(image, uv); }\n";
    const GLuint vertex = compileWidgetShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragment =
        compileWidgetShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vertex || !fragment) {
      if (vertex)
        glDeleteShader(vertex);
      if (fragment)
        glDeleteShader(fragment);
      return;
    }
    m_impl->widgetProgram = glCreateProgram();
    glAttachShader(m_impl->widgetProgram, vertex);
    glAttachShader(m_impl->widgetProgram, fragment);
    glLinkProgram(m_impl->widgetProgram);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(m_impl->widgetProgram, GL_LINK_STATUS, &linked);
    if (!linked) {
      glDeleteProgram(m_impl->widgetProgram);
      m_impl->widgetProgram = 0;
      return;
    }
    m_impl->widgetPosition =
        glGetAttribLocation(m_impl->widgetProgram, "position");
    m_impl->widgetTexcoord =
        glGetAttribLocation(m_impl->widgetProgram, "texcoord");
    glGenTextures(1, &m_impl->widgetTexture);
    glBindTexture(GL_TEXTURE_2D, m_impl->widgetTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  GLint previousProgram = 0;
  GLint previousActiveTexture = GL_TEXTURE0;
  GLint previousTexture = 0;
  GLint previousArrayBuffer = 0;
  GLint previousViewport[4]{};
  const GLboolean previousBlend = glIsEnabled(GL_BLEND);
  const GLboolean previousDepth = glIsEnabled(GL_DEPTH_TEST);
  const GLboolean previousCull = glIsEnabled(GL_CULL_FACE);
  const GLboolean previousScissor = glIsEnabled(GL_SCISSOR_TEST);
  glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
  glGetIntegerv(GL_VIEWPORT, previousViewport);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glUseProgram(m_impl->widgetProgram);
  glBindTexture(GL_TEXTURE_2D, m_impl->widgetTexture);
  glUniform1i(glGetUniformLocation(m_impl->widgetProgram, "image"), 0);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_SCISSOR_TEST);

  for (DesktopWidget *widget : m_impl->widgets) {
    if (widget->type() != IWidget::TEXTBOX &&
        widget->type() != IWidget::PASSWDBOX)
      continue;
    auto *text = static_cast<DesktopTextWidget *>(widget);
    if (!text->shown())
      continue;
    const int width = text->width();
    const int height = text->height();
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
      continue;

    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * height * 4);
    unsigned char red, green, blue, alpha;
    unpackArgb(text->background(), red, green, blue, alpha);
    for (std::size_t pixel = 0; pixel < pixels.size(); pixel += 4) {
      pixels[pixel] = red;
      pixels[pixel + 1] = green;
      pixels[pixel + 2] = blue;
      pixels[pixel + 3] = alpha;
    }
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        if (x >= 2 && x < width - 2 && y >= 2 && y < height - 2)
          continue;
        unsigned char *pixel =
            &pixels[(static_cast<std::size_t>(y) * width + x) * 4];
        pixel[0] = 160;
        pixel[1] = 160;
        pixel[2] = 160;
        pixel[3] = 255;
      }
    }

    std::string displayed =
        text->value().empty() ? text->placeholder() : text->value();
    if (widget->type() == IWidget::PASSWDBOX && !displayed.empty()) {
      std::size_t characters = 0;
      for (unsigned char byte : displayed)
        if ((byte & 0xc0) != 0x80)
          ++characters;
      displayed.assign(characters, '*');
    }
    if (!displayed.empty() && text->font()) {
      STextInfo info;
      CPFInterface::getInstance().platform().getTextInfo(
          displayed.c_str(), text->font(), &info);
      int baseX = 6;
      if (text->alignment() == IWidget::TX_ALIGN_CENTER)
        baseX = std::max(2, static_cast<int>((width - info.width) * 0.5f));
      else if (text->alignment() == IWidget::TX_ALIGN_RIGHT)
        baseX = std::max(2, width - static_cast<int>(info.width) - 6);
      const int baseY = std::max(
          0, static_cast<int>((height - info.height) * 0.5f + info.ascent));
      CPFInterface::getInstance().platform().renderText(
          displayed.c_str(), text->font(), text->foreground(),
          static_cast<u16>(width), static_cast<u16>(height), pixels.data(),
          static_cast<s16>(width * 4), static_cast<s16>(baseX),
          static_cast<s16>(baseY), 4, 1.0f, 1.0f);
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels.data());
    const float left = static_cast<float>(text->x()) / 480.0f - 1.0f;
    const float right =
        static_cast<float>(text->x() + width) / 480.0f - 1.0f;
    const float top = 1.0f - static_cast<float>(text->y()) / 320.0f;
    const float bottom =
        1.0f - static_cast<float>(text->y() + height) / 320.0f;
    const GLfloat vertices[] = {
        left,  top,    0.0f, 0.0f, right, top,    1.0f, 0.0f,
        right, bottom, 1.0f, 1.0f, left,  top,    0.0f, 0.0f,
        right, bottom, 1.0f, 1.0f, left,  bottom, 0.0f, 1.0f};
    glEnableVertexAttribArray(static_cast<GLuint>(m_impl->widgetPosition));
    glEnableVertexAttribArray(static_cast<GLuint>(m_impl->widgetTexcoord));
    glVertexAttribPointer(static_cast<GLuint>(m_impl->widgetPosition), 2,
                          GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 4, vertices);
    glVertexAttribPointer(static_cast<GLuint>(m_impl->widgetTexcoord), 2,
                          GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 4,
                          vertices + 2);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(static_cast<GLuint>(m_impl->widgetPosition));
    glDisableVertexAttribArray(static_cast<GLuint>(m_impl->widgetTexcoord));
  }

  if (previousBlend)
    glEnable(GL_BLEND);
  else
    glDisable(GL_BLEND);
  if (previousDepth)
    glEnable(GL_DEPTH_TEST);
  else
    glDisable(GL_DEPTH_TEST);
  if (previousCull)
    glEnable(GL_CULL_FACE);
  else
    glDisable(GL_CULL_FACE);
  if (previousScissor)
    glEnable(GL_SCISSOR_TEST);
  else
    glDisable(GL_SCISSOR_TEST);
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
  glActiveTexture(static_cast<GLenum>(previousActiveTexture));
  glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
  glUseProgram(static_cast<GLuint>(previousProgram));
  glViewport(previousViewport[0], previousViewport[1], previousViewport[2],
             previousViewport[3]);
#endif
}

} // namespace playground::runtime
