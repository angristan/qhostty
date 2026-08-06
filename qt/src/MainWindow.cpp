#include "MainWindow.h"

#include "GhosttyApp.h"
#include "TerminalWidget.h"

MainWindow::MainWindow(GhosttyApp* app, QWidget* parent)
    : QMainWindow(parent),
      m_app(app),
      m_terminal(new TerminalWidget(app, this)) {
  setObjectName(QStringLiteral("qhostty-window"));
  setWindowTitle(QStringLiteral("Qhostty"));
  setCentralWidget(m_terminal);
  resize(1000, 650);

  connect(m_terminal, &TerminalWidget::titleChanged, this,
          &QWidget::setWindowTitle);
  connect(m_terminal, &TerminalWidget::closeRequested, this,
          [this](TerminalWidget*) { close(); });
}
