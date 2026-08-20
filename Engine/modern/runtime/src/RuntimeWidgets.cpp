#include "RuntimeWidgets.h"

#include "Playground/Runtime/RuntimePlatform.h"

#include "CPFInterface.h"

#if defined(__SWITCH__)
#include "Playground/Switch/SwitchSystem.h"
#else
#include <SDL3/SDL_keycode.h>
#endif

#include <algorithm>
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
    return true;
  }
  void move(int x, int y) override {
    m_x = x;
    m_y = y;
  }
  void resize(int width, int height) override {
    m_width = width;
    m_height = height;
  }
  void visible(bool visible) override { m_visible = visible; }
  void enable(bool enabled) override { m_enabled = enabled; }
  int status() override { return m_status; }

  IWidget::CONTROL type() const { return m_type; }
  bool editable() const {
    return (m_type == TEXTBOX || m_type == PASSWDBOX) && m_visible && m_enabled;
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
  }

protected:
  void constrainLength() {
    if (m_maxLength < 0 || m_text.size() <= static_cast<size_t>(m_maxLength))
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
};

class DesktopTextWidget final : public DesktopWidget {
public:
  using DesktopWidget::DesktopWidget;
  int getTextMaxLength() override { return m_maxLength; }
  void cmd(int command, ...) override {
    va_list arguments;
    va_start(arguments, command);
    switch (command) {
    case TX_FONT:
      (void)va_arg(arguments, void *);
      break;
    case TX_PLACEHOLDER: {
      const char *placeholder = va_arg(arguments, const char *);
      m_placeholder = placeholder ? placeholder : "";
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
      break;
    default:
      (void)va_arg(arguments, unsigned int);
      break;
    }
    va_end(arguments);
  }
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
};

RuntimeWidgetManager::RuntimeWidgetManager(RuntimePlatform *platform)
    : m_impl(std::make_unique<Impl>(platform)) {}
RuntimeWidgetManager::~RuntimeWidgetManager() {
  for (DesktopWidget *widget : m_impl->widgets)
    delete widget;
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
#if defined(__SWITCH__)
    std::string entered;
    const int maximum = text->getTextMaxLength();
    if (switch_runtime::showSoftwareKeyboard(
            type == IWidget::PASSWDBOX, caption,
            maximum > 0 ? static_cast<std::uint32_t>(maximum) : 4096,
            entered)) {
      text->setText(entered.c_str());
      m_impl->notifyText(text);
    }
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
  constexpr int Backspace = SDLK_BACKSPACE;
  constexpr int Return = SDLK_RETURN;
  constexpr int KeypadEnter = SDLK_KP_ENTER;
#endif
  if (key == Backspace) {
    m_impl->activeText->eraseLastCodepoint();
    m_impl->notifyText(m_impl->activeText);
    return true;
  }
  return key == Return || key == KeypadEnter;
}

void RuntimeWidgetManager::pumpEvents() {
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

} // namespace playground::runtime
