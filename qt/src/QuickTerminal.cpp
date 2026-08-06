#include "QuickTerminal.h"

#include "MainWindow.h"
#include "TerminalTab.h"
#include "TerminalWidget.h"

#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#ifdef QHOSTTY_HAS_LAYER_SHELL
#include <LayerShellQt/window.h>
#endif
#ifdef QHOSTTY_HAS_KWINDOWSYSTEM
#include <KWindowEffects>
#include <KX11Extras>
#include <NETWM>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
QString configString(ghostty_config_t config,
                     const char* key,
                     const QString& fallback) {
  const char* value = nullptr;
  if (ghostty_config_get(config, &value, key, std::strlen(key)) &&
      value != nullptr) {
    return QString::fromUtf8(value);
  }
  return fallback;
}

int configSize(const ghostty_quick_terminal_size_s& value,
               int parent,
               int fallback) {
  switch (value.tag) {
    case GHOSTTY_QUICK_TERMINAL_SIZE_PERCENTAGE:
      return qRound(value.value.percentage / 100.0 * parent);
    case GHOSTTY_QUICK_TERMINAL_SIZE_PIXELS:
      return static_cast<int>(value.value.pixels);
    case GHOSTTY_QUICK_TERMINAL_SIZE_NONE:
    default:
      return fallback;
  }
}
}  // namespace

QuickTerminalSettings QuickTerminalSettings::fromConfig(
    ghostty_config_t config) {
  QuickTerminalSettings result;
  const QString position =
      configString(config, "quick-terminal-position", QStringLiteral("top"));
  if (position == QStringLiteral("bottom")) {
    result.position = Position::Bottom;
  } else if (position == QStringLiteral("left")) {
    result.position = Position::Left;
  } else if (position == QStringLiteral("right")) {
    result.position = Position::Right;
  } else if (position == QStringLiteral("center")) {
    result.position = Position::Center;
  }

  const QString screen =
      configString(config, "quick-terminal-screen", QStringLiteral("main"));
  result.screen =
      screen == QStringLiteral("mouse") ? Screen::Mouse : Screen::Main;

  const QString layer =
      configString(config, "gtk-quick-terminal-layer", QStringLiteral("top"));
  if (layer == QStringLiteral("background")) {
    result.layer = Layer::Background;
  } else if (layer == QStringLiteral("bottom")) {
    result.layer = Layer::Bottom;
  } else if (layer == QStringLiteral("overlay")) {
    result.layer = Layer::Overlay;
  }

  const QString keyboard =
      configString(config, "quick-terminal-keyboard-interactivity",
                   QStringLiteral("on-demand"));
  if (keyboard == QStringLiteral("none")) {
    result.keyboardInteractivity = KeyboardInteractivity::None;
  } else if (keyboard == QStringLiteral("exclusive")) {
    result.keyboardInteractivity = KeyboardInteractivity::Exclusive;
  }

  ghostty_config_get(config, &result.size, "quick-terminal-size",
                     std::strlen("quick-terminal-size"));
  ghostty_config_get(config, &result.autohide, "quick-terminal-autohide",
                     std::strlen("quick-terminal-autohide"));
  result.scope = configString(config, "gtk-quick-terminal-namespace",
                              QStringLiteral("qhostty-quick-terminal"));
  return result;
}

QScreen* QuickTerminalSettings::targetScreen() const {
  if (screen == Screen::Mouse) {
    if (QScreen* hovered = QGuiApplication::screenAt(QCursor::pos())) {
      return hovered;
    }
  }
  if (QScreen* primary = QGuiApplication::primaryScreen()) {
    return primary;
  }
  return QGuiApplication::screens().value(0, nullptr);
}

QRect QuickTerminalSettings::geometry(const QRect& screenGeometry) const {
  const int screenWidth = screenGeometry.width();
  const int screenHeight = screenGeometry.height();
  int width = 0;
  int height = 0;

  switch (position) {
    case Position::Left:
    case Position::Right:
      width = configSize(size.primary, screenWidth, 400);
      height = configSize(size.secondary, screenHeight, screenHeight);
      break;
    case Position::Top:
    case Position::Bottom:
      width = configSize(size.secondary, screenWidth, screenWidth);
      height = configSize(size.primary, screenHeight, 400);
      break;
    case Position::Center:
      if (screenWidth >= screenHeight) {
        width = configSize(size.primary, screenWidth, 800);
        height = configSize(size.secondary, screenHeight, 400);
      } else {
        width = configSize(size.secondary, screenWidth, 400);
        height = configSize(size.primary, screenHeight, 800);
      }
      break;
  }

  width = std::clamp(width, 1, screenWidth);
  height = std::clamp(height, 1, screenHeight);
  int x = screenGeometry.x() + (screenWidth - width) / 2;
  int y = screenGeometry.y() + (screenHeight - height) / 2;
  if (position == Position::Top) {
    y = screenGeometry.top();
  } else if (position == Position::Bottom) {
    y = screenGeometry.bottom() - height + 1;
  } else if (position == Position::Left) {
    x = screenGeometry.left();
  } else if (position == Position::Right) {
    x = screenGeometry.right() - width + 1;
  }
  return {x, y, width, height};
}

void showQuickTerminal(MainWindow* window,
                       const QuickTerminalSettings& settings) {
  if (window == nullptr) {
    return;
  }
  QScreen* screen = settings.targetScreen();
  if (screen == nullptr) {
    return;
  }
  const QRect target = settings.geometry(screen->geometry());
  window->setQuickTerminalAutohide(settings.autohide);
  window->setWindowFlag(Qt::Tool, true);
  window->setWindowFlag(Qt::FramelessWindowHint, true);

  window->winId();
  if (window->windowHandle() != nullptr) {
    window->windowHandle()->setScreen(screen);
  }

  bool layerShell = false;
#ifdef QHOSTTY_HAS_LAYER_SHELL
  if (QGuiApplication::platformName().startsWith(QStringLiteral("wayland")) &&
      window->windowHandle() != nullptr) {
    auto* layer = LayerShellQt::Window::get(window->windowHandle());
    LayerShellQt::Window::Anchors anchors;
    QMargins margins(20, 20, 20, 20);
    switch (settings.position) {
      case QuickTerminalSettings::Position::Top:
        anchors = LayerShellQt::Window::AnchorTop;
        margins.setTop(0);
        break;
      case QuickTerminalSettings::Position::Bottom:
        anchors = LayerShellQt::Window::AnchorBottom;
        margins.setBottom(0);
        break;
      case QuickTerminalSettings::Position::Left:
        anchors = LayerShellQt::Window::AnchorLeft;
        margins.setLeft(0);
        break;
      case QuickTerminalSettings::Position::Right:
        anchors = LayerShellQt::Window::AnchorRight;
        margins.setRight(0);
        break;
      case QuickTerminalSettings::Position::Center:
        break;
    }
    layer->setAnchors(anchors);
    layer->setMargins(margins);
    layer->setDesiredSize(target.size());
    layer->setExclusiveZone(0);
    layer->setScope(settings.scope);
    layer->setScreen(screen);
    switch (settings.layer) {
      case QuickTerminalSettings::Layer::Background:
        layer->setLayer(LayerShellQt::Window::LayerBackground);
        break;
      case QuickTerminalSettings::Layer::Bottom:
        layer->setLayer(LayerShellQt::Window::LayerBottom);
        break;
      case QuickTerminalSettings::Layer::Top:
        layer->setLayer(LayerShellQt::Window::LayerTop);
        break;
      case QuickTerminalSettings::Layer::Overlay:
        layer->setLayer(LayerShellQt::Window::LayerOverlay);
        break;
    }
    switch (settings.keyboardInteractivity) {
      case QuickTerminalSettings::KeyboardInteractivity::None:
        layer->setKeyboardInteractivity(
            LayerShellQt::Window::KeyboardInteractivityNone);
        break;
      case QuickTerminalSettings::KeyboardInteractivity::OnDemand:
        layer->setKeyboardInteractivity(
            LayerShellQt::Window::KeyboardInteractivityOnDemand);
        break;
      case QuickTerminalSettings::KeyboardInteractivity::Exclusive:
        layer->setKeyboardInteractivity(
            LayerShellQt::Window::KeyboardInteractivityExclusive);
        break;
    }
    layer->setActivateOnShow(
        settings.keyboardInteractivity !=
        QuickTerminalSettings::KeyboardInteractivity::None);
    layerShell = true;
  }
#endif
  if (!layerShell) {
    window->setGeometry(target);
  }
#ifdef QHOSTTY_HAS_KWINDOWSYSTEM
  if (QGuiApplication::platformName() == QStringLiteral("xcb")) {
    KX11Extras::setOnAllDesktops(window->winId(), true);
    NET::States states = NET::SkipTaskbar | NET::SkipPager | NET::SkipSwitcher;
    if (settings.layer == QuickTerminalSettings::Layer::Background ||
        settings.layer == QuickTerminalSettings::Layer::Bottom) {
      states |= NET::KeepBelow;
    } else {
      states |= NET::KeepAbove;
    }
    KX11Extras::setState(window->winId(), states);
    KX11Extras::setType(window->winId(), NET::Utility);
  }
  if (KWindowEffects::isEffectAvailable(KWindowEffects::Slide) &&
      window->windowHandle() != nullptr) {
    KWindowEffects::SlideFromLocation edge = KWindowEffects::NoEdge;
    switch (settings.position) {
      case QuickTerminalSettings::Position::Top:
        edge = KWindowEffects::TopEdge;
        break;
      case QuickTerminalSettings::Position::Bottom:
        edge = KWindowEffects::BottomEdge;
        break;
      case QuickTerminalSettings::Position::Left:
        edge = KWindowEffects::LeftEdge;
        break;
      case QuickTerminalSettings::Position::Right:
        edge = KWindowEffects::RightEdge;
        break;
      case QuickTerminalSettings::Position::Center:
        break;
    }
    KWindowEffects::slideWindow(window->windowHandle(), edge);
  }
#endif
  window->show();
  window->raise();
  window->activateWindow();
  if (TerminalTab* tab = window->currentTab();
      tab != nullptr && tab->activeTerminal() != nullptr) {
    tab->activeTerminal()->setFocus(Qt::ShortcutFocusReason);
  }
}
