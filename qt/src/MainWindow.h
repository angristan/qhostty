#pragma once

#include <QMainWindow>

class GhosttyApp;
class TerminalWidget;

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(GhosttyApp* app, QWidget* parent = nullptr);

 private:
  GhosttyApp* m_app;
  TerminalWidget* m_terminal;
};
