#pragma once

#include <QRect>
#include <QString>

#include <ghostty.h>

class MainWindow;
class QScreen;

struct QuickTerminalSettings {
  enum class Position {
    Top,
    Bottom,
    Left,
    Right,
    Center,
  };
  enum class Screen {
    Main,
    Mouse,
  };
  enum class Layer {
    Background,
    Bottom,
    Top,
    Overlay,
  };
  enum class KeyboardInteractivity {
    None,
    OnDemand,
    Exclusive,
  };

  Position position = Position::Top;
  Screen screen = Screen::Main;
  Layer layer = Layer::Top;
  KeyboardInteractivity keyboardInteractivity = KeyboardInteractivity::OnDemand;
  ghostty_config_quick_terminal_size_s size{};
  QString scope = QStringLiteral("qhostty-quick-terminal");
  bool autohide = false;

  static QuickTerminalSettings fromConfig(ghostty_config_t config);
  [[nodiscard]] QScreen* targetScreen() const;
  [[nodiscard]] QRect geometry(const QRect& screenGeometry) const;
};

void showQuickTerminal(MainWindow* window,
                       const QuickTerminalSettings& settings);
