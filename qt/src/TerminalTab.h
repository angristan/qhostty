#pragma once

#include <QWidget>

#include <ghostty.h>

class GhosttyApp;
class QBoxLayout;
class QSplitter;
class TerminalWidget;

class TerminalTab final : public QWidget {
  Q_OBJECT

 public:
  explicit TerminalTab(GhosttyApp* app,
                       QWidget* parent = nullptr,
                       const ghostty_surface_config_s* baseConfig = nullptr);

  [[nodiscard]] TerminalWidget* activeTerminal() const { return m_active; }
  [[nodiscard]] QList<TerminalWidget*> terminals() const;

  TerminalWidget* newSplit(TerminalWidget* source,
                           ghostty_action_split_direction_e direction);
  bool closeSurface(TerminalWidget* surface);
  bool focusSplit(ghostty_action_goto_split_e direction);
  bool resizeSplit(const ghostty_action_resize_split_s& resize);
  void equalizeSplits();
  void toggleZoom();
  bool canClose() const;

 signals:
  void titleChanged(const QString& title);
  void closeRequested(TerminalTab* tab);

 private:
  void connectTerminal(TerminalWidget* terminal);
  void setActive(TerminalWidget* terminal);
  void collapseSplitter(QSplitter* splitter);
  void equalizeSplitter(QSplitter* splitter);

  GhosttyApp* m_app;
  QBoxLayout* m_layout;
  TerminalWidget* m_active = nullptr;
  bool m_zoomed = false;
};
