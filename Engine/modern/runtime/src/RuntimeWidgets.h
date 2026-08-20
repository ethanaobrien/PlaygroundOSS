#ifndef PLAYGROUND_RUNTIME_DESKTOP_WIDGETS_H
#define PLAYGROUND_RUNTIME_DESKTOP_WIDGETS_H

#include "OSWidget.h"

#include <cstdarg>
#include <memory>

namespace playground::runtime {

class RuntimePlatform;

class RuntimeWidgetManager {
public:
  explicit RuntimeWidgetManager(RuntimePlatform *platform);
  ~RuntimeWidgetManager();

  IWidget *create(IWidget::CONTROL type, int id, const char *caption, int x,
                  int y, int width, int height, va_list arguments);
  void destroy(IWidget *widget);
  void inputText(const char *text);
  bool inputKey(int key, bool pressed);
  void pumpEvents();

private:
  class Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace playground::runtime

#endif
