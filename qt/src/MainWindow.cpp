#include "MainWindow.h"

#include "GhosttyApp.h"
#include "TerminalTab.h"
#include "TerminalWidget.h"

#include <QCloseEvent>
#include <QInputDialog>
#include <QMenu>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

#include <algorithm>

MainWindow::MainWindow(GhosttyApp* app,
                       QWidget* parent,
                       const ghostty_surface_config_s* baseConfig,
                       bool createInitialTab)
    : QMainWindow(parent),
      m_app(app),
      m_tabBar(new QTabBar(this)),
      m_stack(new QStackedWidget(this)) {
  setAttribute(Qt::WA_DeleteOnClose, true);
  setObjectName(QStringLiteral("qhostty-window"));
  setWindowTitle(QStringLiteral("Qhostty"));

  m_tabBar->setObjectName(QStringLiteral("qhostty-tabs"));
  m_tabBar->setDocumentMode(true);
  m_tabBar->setMovable(true);
  m_tabBar->setTabsClosable(true);
  m_tabBar->setExpanding(false);
  m_tabBar->setElideMode(Qt::ElideRight);
  m_tabBar->setContextMenuPolicy(Qt::CustomContextMenu);

  auto* central = new QWidget(this);
  auto* layout = new QVBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_tabBar);
  layout->addWidget(m_stack, 1);
  setCentralWidget(central);
  resize(1000, 650);

  connect(m_tabBar, &QTabBar::currentChanged, m_stack,
          &QStackedWidget::setCurrentIndex);
  connect(m_tabBar, &QTabBar::currentChanged, this, [this](int) {
    if (TerminalTab* tab = currentTab();
        tab != nullptr && tab->activeTerminal() != nullptr) {
      tab->activeTerminal()->setFocus(Qt::OtherFocusReason);
    }
  });
  connect(m_tabBar, &QTabBar::tabCloseRequested, this, &MainWindow::closeTab);
  connect(m_tabBar, &QTabBar::tabMoved, this, [this](int from, int to) {
    QWidget* page = m_stack->widget(from);
    if (page == nullptr) {
      return;
    }
    m_stack->removeWidget(page);
    m_stack->insertWidget(to, page);
    m_stack->setCurrentIndex(to);
  });
  connect(m_tabBar, &QTabBar::customContextMenuRequested, this,
          [this](const QPoint& position) {
            const int index = m_tabBar->tabAt(position);
            if (index < 0) {
              return;
            }
            QMenu menu(this);
            QAction* detach = menu.addAction(tr("Detach Tab"));
            if (menu.exec(m_tabBar->mapToGlobal(position)) == detach) {
              detachTab(index);
            }
          });

  m_app->registerWindow(this);
  if (createInitialTab) {
    addTab(baseConfig);
  }
}

MainWindow::~MainWindow() {
  m_app->unregisterWindow(this);
}

int MainWindow::addTab(const ghostty_surface_config_s* baseConfig) {
  auto* tab = new TerminalTab(m_app, m_stack, baseConfig);
  return adoptTab(tab, tr("Terminal"));
}

int MainWindow::adoptTab(TerminalTab* tab, const QString& title) {
  if (tab == nullptr) {
    return -1;
  }
  tab->setParent(m_stack);
  const int index = m_stack->addWidget(tab);
  m_tabBar->insertTab(index, title);
  m_tabBar->setCurrentIndex(index);
  connectTab(tab);
  return index;
}

bool MainWindow::handleAction(TerminalWidget* source,
                              const ghostty_action_s& action) {
  TerminalTab* tab = source != nullptr ? tabFor(source) : currentTab();
  switch (action.tag) {
    case GHOSTTY_ACTION_NEW_TAB: {
      ghostty_surface_config_s config =
          source != nullptr
              ? source->inheritedConfig(GHOSTTY_SURFACE_CONTEXT_TAB)
              : ghostty_surface_config_new();
      addTab(&config);
      return true;
    }
    case GHOSTTY_ACTION_CLOSE_TAB: {
      const int current = m_tabBar->currentIndex();
      if (action.action.close_tab_mode == GHOSTTY_ACTION_CLOSE_TAB_MODE_OTHER) {
        for (int index = m_tabBar->count() - 1; index >= 0; --index) {
          if (index != current) {
            closeTab(index);
          }
        }
      } else if (action.action.close_tab_mode ==
                 GHOSTTY_ACTION_CLOSE_TAB_MODE_RIGHT) {
        for (int index = m_tabBar->count() - 1; index > current; --index) {
          closeTab(index);
        }
      } else {
        closeTab(current);
      }
      return true;
    }
    case GHOSTTY_ACTION_NEW_SPLIT:
      return tab != nullptr &&
             tab->newSplit(source, action.action.new_split) != nullptr;
    case GHOSTTY_ACTION_GOTO_SPLIT:
      return tab != nullptr && tab->focusSplit(action.action.goto_split);
    case GHOSTTY_ACTION_RESIZE_SPLIT:
      return tab != nullptr && tab->resizeSplit(action.action.resize_split);
    case GHOSTTY_ACTION_EQUALIZE_SPLITS:
      if (tab != nullptr) {
        tab->equalizeSplits();
        return true;
      }
      return false;
    case GHOSTTY_ACTION_TOGGLE_SPLIT_ZOOM:
      if (tab != nullptr) {
        tab->toggleZoom();
        return true;
      }
      return false;
    case GHOSTTY_ACTION_MOVE_TAB: {
      const int from = m_tabBar->currentIndex();
      const int count = m_tabBar->count();
      if (from < 0 || count < 2) {
        return false;
      }
      int to = (from + static_cast<int>(action.action.move_tab.amount)) % count;
      if (to < 0) {
        to += count;
      }
      m_tabBar->moveTab(from, to);
      return true;
    }
    case GHOSTTY_ACTION_GOTO_TAB:
      return selectTab(static_cast<int>(action.action.goto_tab));
    case GHOSTTY_ACTION_TOGGLE_MAXIMIZE:
      isMaximized() ? showNormal() : showMaximized();
      return true;
    case GHOSTTY_ACTION_TOGGLE_FULLSCREEN:
      isFullScreen() ? showNormal() : showFullScreen();
      return true;
    case GHOSTTY_ACTION_TOGGLE_WINDOW_DECORATIONS: {
      const bool frameless = windowFlags().testFlag(Qt::FramelessWindowHint);
      setWindowFlag(Qt::FramelessWindowHint, !frameless);
      show();
      return true;
    }
    case GHOSTTY_ACTION_TOGGLE_VISIBILITY:
      if (isVisible()) {
        hide();
      } else {
        show();
        raise();
        activateWindow();
      }
      return true;
    case GHOSTTY_ACTION_FLOAT_WINDOW: {
      const auto mode = action.action.float_window;
      const bool current = windowFlags().testFlag(Qt::WindowStaysOnTopHint);
      const bool enabled = mode == GHOSTTY_FLOAT_WINDOW_TOGGLE
                               ? !current
                               : mode == GHOSTTY_FLOAT_WINDOW_ON;
      setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
      show();
      return true;
    }
    case GHOSTTY_ACTION_TOGGLE_BACKGROUND_OPACITY:
      return true;
    case GHOSTTY_ACTION_TOGGLE_TAB_OVERVIEW:
      m_tabBar->setVisible(!m_tabBar->isVisible());
      return true;
    case GHOSTTY_ACTION_RESET_WINDOW_SIZE:
      resize(1000, 650);
      return true;
    case GHOSTTY_ACTION_PROMPT_TITLE: {
      const int index = m_tabBar->currentIndex();
      if (index < 0) {
        return false;
      }
      bool accepted = false;
      const QString title = QInputDialog::getText(
          this, tr("Change title"), tr("Title:"), QLineEdit::Normal,
          m_tabBar->tabText(index), &accepted);
      if (accepted && !title.isEmpty()) {
        m_tabBar->setTabText(index, title);
        setWindowTitle(title);
      }
      return true;
    }
    case GHOSTTY_ACTION_CLOSE_WINDOW:
      close();
      return true;
    default:
      return false;
  }
}

TerminalTab* MainWindow::currentTab() const {
  return qobject_cast<TerminalTab*>(m_stack->currentWidget());
}

void MainWindow::closeEvent(QCloseEvent* event) {
  for (int index = 0; index < m_stack->count(); ++index) {
    auto* tab = qobject_cast<TerminalTab*>(m_stack->widget(index));
    if (tab != nullptr && !tab->canClose()) {
      event->ignore();
      return;
    }
  }
  event->accept();
}

TerminalTab* MainWindow::tabFor(TerminalWidget* terminal) const {
  QWidget* current = terminal;
  while (current != nullptr) {
    if (auto* tab = qobject_cast<TerminalTab*>(current)) {
      return tab;
    }
    current = current->parentWidget();
  }
  return nullptr;
}

void MainWindow::closeTab(int index) {
  if (index < 0 || index >= m_stack->count()) {
    return;
  }

  auto* tab = qobject_cast<TerminalTab*>(m_stack->widget(index));
  if (tab == nullptr || !tab->canClose()) {
    return;
  }

  m_stack->removeWidget(tab);
  m_tabBar->removeTab(index);
  tab->deleteLater();
  if (m_tabBar->count() == 0) {
    close();
  }
}

void MainWindow::detachTab(int index) {
  if (index < 0 || index >= m_stack->count()) {
    return;
  }

  auto* tab = qobject_cast<TerminalTab*>(m_stack->widget(index));
  if (tab == nullptr) {
    return;
  }
  const QString title = m_tabBar->tabText(index);
  m_stack->removeWidget(tab);
  m_tabBar->removeTab(index);
  tab->setParent(nullptr);

  auto* detached = new MainWindow(m_app, nullptr, nullptr, false);
  detached->adoptTab(tab, title);
  detached->resize(size());
  detached->show();
  if (m_tabBar->count() == 0) {
    close();
  }
}

void MainWindow::connectTab(TerminalTab* tab) {
  disconnect(tab, nullptr, this, nullptr);
  connect(tab, &TerminalTab::titleChanged, this,
          [this, tab](const QString& title) { updateTabTitle(tab, title); });
  connect(tab, &TerminalTab::closeRequested, this,
          [this](TerminalTab* page) { closeTab(m_stack->indexOf(page)); });
}

void MainWindow::updateTabTitle(TerminalTab* tab, const QString& title) {
  const int index = m_stack->indexOf(tab);
  if (index < 0 || title.isEmpty()) {
    return;
  }
  m_tabBar->setTabText(index, title);
  m_tabBar->setTabToolTip(index, title);
  if (index == m_tabBar->currentIndex()) {
    setWindowTitle(title);
  }
}

bool MainWindow::selectTab(int value) {
  const int count = m_tabBar->count();
  if (count == 0) {
    return false;
  }

  int index = value;
  if (value == GHOSTTY_GOTO_TAB_PREVIOUS) {
    index = (m_tabBar->currentIndex() - 1 + count) % count;
  } else if (value == GHOSTTY_GOTO_TAB_NEXT) {
    index = (m_tabBar->currentIndex() + 1) % count;
  } else if (value == GHOSTTY_GOTO_TAB_LAST) {
    index = count - 1;
  }

  if (index < 0 || index >= count) {
    return false;
  }
  m_tabBar->setCurrentIndex(index);
  return true;
}
