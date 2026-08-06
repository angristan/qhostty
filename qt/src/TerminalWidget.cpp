#include "TerminalWidget.h"

#include "GhosttyApp.h"
#include "InputCoordinates.h"
#include "InspectorWindow.h"
#include "KeyEventTranslation.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFocusEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

TerminalWidget::TerminalWidget(GhosttyApp* app,
                               QWidget* parent,
                               const ghostty_surface_config_s* baseConfig)
    : QOpenGLWidget(parent), m_app(app) {
  setObjectName(QStringLiteral("qhostty-terminal"));
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setAttribute(Qt::WA_InputMethodEnabled, true);
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

  m_config = baseConfig != nullptr ? *baseConfig : ghostty_surface_config_new();
  m_appliedConfig = ghostty_config_clone(m_app->config());
  ghostty_config_get(m_appliedConfig, &m_focusFollowsMouse,
                     "focus-follows-mouse", std::strlen("focus-follows-mouse"));
  if (m_config.working_directory != nullptr) {
    m_workingDirectory = QString::fromUtf8(m_config.working_directory);
    m_workingDirectoryUtf8 = m_workingDirectory.toUtf8();
    m_config.working_directory = nullptr;
  }
  if (m_config.command != nullptr) {
    m_commandUtf8 = QByteArray(m_config.command);
    m_config.command = nullptr;
  }
  if (m_config.initial_input != nullptr) {
    m_initialInputUtf8 = QByteArray(m_config.initial_input);
    m_config.initial_input = nullptr;
  }

  m_searchFrame = new QFrame(this);
  m_searchFrame->setObjectName(QStringLiteral("qhostty-search"));
  m_searchFrame->setFrameShape(QFrame::StyledPanel);
  auto* searchLayout = new QHBoxLayout(m_searchFrame);
  searchLayout->setContentsMargins(6, 4, 6, 4);
  searchLayout->setSpacing(4);
  m_searchEdit = new QLineEdit(m_searchFrame);
  m_searchEdit->setPlaceholderText(tr("Search"));
  m_searchEdit->installEventFilter(this);
  m_searchCount = new QLabel(m_searchFrame);
  auto* previous = new QPushButton(tr("Previous"), m_searchFrame);
  auto* next = new QPushButton(tr("Next"), m_searchFrame);
  auto* close = new QPushButton(tr("Close"), m_searchFrame);
  searchLayout->addWidget(m_searchEdit);
  searchLayout->addWidget(m_searchCount);
  searchLayout->addWidget(previous);
  searchLayout->addWidget(next);
  searchLayout->addWidget(close);
  m_searchFrame->hide();

  connect(m_searchEdit, &QLineEdit::textChanged, this,
          [this](const QString& needle) {
            runBindingAction(QStringLiteral("search:") + needle);
          });
  connect(previous, &QPushButton::clicked, this, [this]() {
    runBindingAction(QStringLiteral("navigate_search:previous"));
  });
  connect(next, &QPushButton::clicked, this, [this]() {
    runBindingAction(QStringLiteral("navigate_search:next"));
  });
  connect(close, &QPushButton::clicked, this,
          [this]() { runBindingAction(QStringLiteral("end_search")); });

  m_statusOverlay = new QLabel(this);
  m_statusOverlay->setObjectName(QStringLiteral("qhostty-status"));
  m_statusOverlay->setFrameShape(QFrame::StyledPanel);
  m_statusOverlay->setAlignment(Qt::AlignCenter);
  m_statusOverlay->setWordWrap(true);
  m_statusOverlay->hide();

  m_readonlyBadge = new QLabel(tr("Read-only"), this);
  m_readonlyBadge->setObjectName(QStringLiteral("qhostty-readonly"));
  m_readonlyBadge->setFrameShape(QFrame::StyledPanel);
  m_readonlyBadge->setContentsMargins(6, 3, 6, 3);
  m_readonlyBadge->hide();

  m_keyStateBadge = new QLabel(this);
  m_keyStateBadge->setObjectName(QStringLiteral("qhostty-key-state"));
  m_keyStateBadge->setFrameShape(QFrame::StyledPanel);
  m_keyStateBadge->setContentsMargins(6, 3, 6, 3);
  m_keyStateBadge->hide();

  m_progressBar = new QProgressBar(this);
  m_progressBar->setObjectName(QStringLiteral("qhostty-progress"));
  m_progressBar->setTextVisible(false);
  m_progressBar->setFixedHeight(4);
  m_progressBar->hide();
  m_progressTimer = new QTimer(this);
  m_progressTimer->setSingleShot(true);
  m_progressTimer->setInterval(15000);
  connect(m_progressTimer, &QTimer::timeout, m_progressBar, &QWidget::hide);

  m_scrollBar = new QScrollBar(Qt::Vertical, this);
  m_scrollBar->setObjectName(QStringLiteral("qhostty-scrollbar"));
  m_scrollBar->hide();
  connect(m_scrollBar, &QScrollBar::valueChanged, this, [this](int value) {
    if (m_scrollMaximum == 0 || m_scrollBar->maximum() == 0) {
      return;
    }
    const uint64_t row = static_cast<uint64_t>(
        std::llround(static_cast<double>(value) / m_scrollBar->maximum() *
                     static_cast<double>(m_scrollMaximum)));
    runBindingAction(QStringLiteral("scroll_to_row:%1").arg(row));
  });

  setupContextMenu();
  m_app->registerSurface(this);
}

void TerminalWidget::setupContextMenu() {
  m_contextMenu = new QMenu(this);
  m_contextMenu->setObjectName(QStringLiteral("qhostty-context-menu"));

  const auto addBindingAction = [this](QMenu* menu, const QString& label,
                                       const QString& binding) {
    QAction* item = menu->addAction(label);
    item->setData(binding);
    connect(item, &QAction::triggered, this,
            [this, item]() { runBindingAction(item->data().toString()); });
    return item;
  };

  addBindingAction(m_contextMenu, tr("Copy"),
                   QStringLiteral("copy_to_clipboard"));
  addBindingAction(m_contextMenu, tr("Paste"),
                   QStringLiteral("paste_from_clipboard"));
  m_notifyNextCommandAction =
      m_contextMenu->addAction(tr("Notify on Next Command Finish"));
  m_notifyNextCommandAction->setObjectName(
      QStringLiteral("qhostty-notify-next-command"));
  m_notifyNextCommandAction->setCheckable(true);

  m_contextMenu->addSeparator();
  addBindingAction(m_contextMenu, tr("Clear"), QStringLiteral("clear_screen"));
  addBindingAction(m_contextMenu, tr("Reset"), QStringLiteral("reset"));

  m_contextMenu->addSeparator();
  QMenu* splitMenu = m_contextMenu->addMenu(tr("Split"));
  splitMenu->setObjectName(QStringLiteral("qhostty-context-split-menu"));
  addBindingAction(splitMenu, tr("Change Title…"),
                   QStringLiteral("prompt_surface_title"));
  addBindingAction(splitMenu, tr("Split Up"), QStringLiteral("new_split:up"));
  addBindingAction(splitMenu, tr("Split Down"),
                   QStringLiteral("new_split:down"));
  addBindingAction(splitMenu, tr("Split Left"),
                   QStringLiteral("new_split:left"));
  addBindingAction(splitMenu, tr("Split Right"),
                   QStringLiteral("new_split:right"));
  addBindingAction(splitMenu, tr("Close Split"),
                   QStringLiteral("close_surface"));

  QMenu* tabMenu = m_contextMenu->addMenu(tr("Tab"));
  tabMenu->setObjectName(QStringLiteral("qhostty-context-tab-menu"));
  addBindingAction(tabMenu, tr("Change Tab Title…"),
                   QStringLiteral("prompt_tab_title"));
  addBindingAction(tabMenu, tr("New Tab"), QStringLiteral("new_tab"));
  addBindingAction(tabMenu, tr("Close Tab"), QStringLiteral("close_tab"));

  QMenu* windowMenu = m_contextMenu->addMenu(tr("Window"));
  windowMenu->setObjectName(QStringLiteral("qhostty-context-window-menu"));
  addBindingAction(windowMenu, tr("New Window"), QStringLiteral("new_window"));
  addBindingAction(windowMenu, tr("Close Window"),
                   QStringLiteral("close_window"));

  m_contextMenu->addSeparator();
  QMenu* configMenu = m_contextMenu->addMenu(tr("Config"));
  configMenu->setObjectName(QStringLiteral("qhostty-context-config-menu"));
  addBindingAction(configMenu, tr("Open Configuration"),
                   QStringLiteral("open_config"));
  addBindingAction(configMenu, tr("Reload Configuration"),
                   QStringLiteral("reload_config"));

  connect(m_contextMenu, &QMenu::aboutToHide, this,
          [this]() { setFocus(Qt::PopupFocusReason); });
}

TerminalWidget::~TerminalWidget() {
  if (context() != nullptr) {
    disconnect(context(), &QOpenGLContext::aboutToBeDestroyed, this,
               &TerminalWidget::cleanupContext);
  }
  emit surfaceClosing();
  destroySurface();
  if (m_appliedConfig != nullptr) {
    ghostty_config_free(m_appliedConfig);
    m_appliedConfig = nullptr;
  }
  m_app->unregisterSurface(this);
}

void TerminalWidget::setTitle(const QString& title) {
  m_title = title;
  emit titleChanged(m_title);
}

ghostty_surface_config_s TerminalWidget::inheritedConfig(
    ghostty_surface_context_e context) const {
  if (m_surface != nullptr) {
    return ghostty_surface_inherited_config(m_surface, context);
  }

  ghostty_surface_config_s config = ghostty_surface_config_new();
  config.context = context;
  return config;
}

void TerminalWidget::freeInheritedConfig(
    ghostty_surface_config_s& config) const {
  if (m_surface != nullptr) {
    ghostty_surface_free_inherited_config(m_surface, &config);
  }
}

bool TerminalWidget::readClipboard(ghostty_clipboard_e location, void* state) {
  if (m_surface == nullptr) {
    return false;
  }

  QClipboard* clipboard = QGuiApplication::clipboard();
  if (location == GHOSTTY_CLIPBOARD_SELECTION &&
      !clipboard->supportsSelection()) {
    return false;
  }
  const QClipboard::Mode mode = location == GHOSTTY_CLIPBOARD_SELECTION
                                    ? QClipboard::Selection
                                    : QClipboard::Clipboard;
  const QMimeData* mime = clipboard->mimeData(mode);
  if (mime == nullptr || !mime->hasText()) {
    return false;
  }

  const QByteArray text = mime->text().toUtf8();
  ghostty_surface_complete_clipboard_request(m_surface, text.constData(), state,
                                             false);
  return true;
}

void TerminalWidget::confirmReadClipboard(const char* contents,
                                          void* state,
                                          ghostty_clipboard_request_e request) {
  if (m_surface == nullptr) {
    return;
  }

  const QString operation =
      request == GHOSTTY_CLIPBOARD_REQUEST_PASTE
          ? tr("Paste clipboard contents into the terminal?")
          : tr("Allow the terminal to read the clipboard?");
  const QString preview = QString::fromUtf8(contents).left(500);
  const auto answer = QMessageBox::question(
      this, tr("Clipboard confirmation"),
      operation + QStringLiteral("\n\n") + preview,
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

  const QByteArray data =
      answer == QMessageBox::Yes ? QByteArray(contents) : QByteArray();
  ghostty_surface_complete_clipboard_request(m_surface, data.constData(), state,
                                             true);
}

void TerminalWidget::writeClipboard(ghostty_clipboard_e location,
                                    const ghostty_clipboard_content_s* contents,
                                    size_t count,
                                    bool confirm) {
  if (contents == nullptr || count == 0) {
    return;
  }

  QString plainText;
  for (size_t index = 0; index < count; ++index) {
    if (contents[index].mime != nullptr && contents[index].data != nullptr &&
        std::strcmp(contents[index].mime, "text/plain") == 0) {
      plainText = QString::fromUtf8(contents[index].data);
      break;
    }
  }

  if (confirm) {
    const auto answer = QMessageBox::question(
        this, tr("Clipboard confirmation"),
        tr("Allow the terminal to write to the clipboard?\n\n%1")
            .arg(plainText.left(500)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      return;
    }
  }

  QClipboard* clipboard = QGuiApplication::clipboard();
  if (location == GHOSTTY_CLIPBOARD_SELECTION &&
      !clipboard->supportsSelection()) {
    return;
  }

  auto* mime = new QMimeData();
  for (size_t index = 0; index < count; ++index) {
    if (contents[index].mime == nullptr || contents[index].data == nullptr) {
      continue;
    }

    const QByteArray type(contents[index].mime);
    const QByteArray data(contents[index].data);
    if (type == "text/plain") {
      mime->setText(QString::fromUtf8(data));
    } else if (type == "text/html") {
      mime->setHtml(QString::fromUtf8(data));
    } else {
      mime->setData(type, data);
    }
  }

  const QClipboard::Mode mode = location == GHOSTTY_CLIPBOARD_SELECTION
                                    ? QClipboard::Selection
                                    : QClipboard::Clipboard;
  clipboard->setMimeData(mime, mode);
}

void TerminalWidget::requestClose(bool processAlive) {
  if (processAlive && m_surface != nullptr &&
      ghostty_surface_needs_confirm_quit(m_surface)) {
    const auto answer = QMessageBox::question(
        this, tr("Close terminal?"),
        tr("A process is still running in this terminal."),
        QMessageBox::Close | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Close) {
      return;
    }
  }

  emit closeRequested(this);
}

std::optional<bool> TerminalWidget::handleAction(
    const ghostty_action_s& action) {
  switch (action.tag) {
    case GHOSTTY_ACTION_RENDER:
      update();
      return true;
    case GHOSTTY_ACTION_SET_TITLE:
      setTitle(QString::fromUtf8(action.action.set_title.title));
      return true;
    case GHOSTTY_ACTION_SET_TAB_TITLE:
      emit tabTitleChanged(
          QString::fromUtf8(action.action.set_tab_title.title));
      return true;
    case GHOSTTY_ACTION_PWD:
      m_workingDirectory = QString::fromUtf8(action.action.pwd.pwd);
      m_workingDirectoryUtf8 = m_workingDirectory.toUtf8();
      return true;
    case GHOSTTY_ACTION_CELL_SIZE:
      m_cellSize = QSize(static_cast<int>(action.action.cell_size.width),
                         static_cast<int>(action.action.cell_size.height));
      return true;
    case GHOSTTY_ACTION_SIZE_LIMIT: {
      const qreal scale = std::max<qreal>(1.0, devicePixelRatioF());
      setMinimumSize(qCeil(action.action.size_limit.min_width / scale),
                     qCeil(action.action.size_limit.min_height / scale));
      if (action.action.size_limit.max_width > 0 &&
          action.action.size_limit.max_height > 0) {
        setMaximumSize(qFloor(action.action.size_limit.max_width / scale),
                       qFloor(action.action.size_limit.max_height / scale));
      }
      return true;
    }
    case GHOSTTY_ACTION_INITIAL_SIZE: {
      if (window() != nullptr &&
          m_config.context == GHOSTTY_SURFACE_CONTEXT_WINDOW &&
          !window()->property("qhosttyInitialSizeApplied").toBool()) {
        const QSize size(static_cast<int>(action.action.initial_size.width),
                         static_cast<int>(action.action.initial_size.height));
        window()->setProperty("qhosttyInitialSizeApplied", true);
        window()->setProperty("qhosttyDefaultSize", size);
        window()->resize(size);
      }
      return true;
    }
    case GHOSTTY_ACTION_MOUSE_SHAPE:
      setMouseShape(action.action.mouse_shape);
      return true;
    case GHOSTTY_ACTION_MOUSE_VISIBILITY:
      setMouseVisible(action.action.mouse_visibility == GHOSTTY_MOUSE_VISIBLE);
      return true;
    case GHOSTTY_ACTION_RING_BELL:
      QApplication::beep();
      QApplication::alert(window());
      emit bellRang();
      return true;
    case GHOSTTY_ACTION_OPEN_URL: {
      const auto& url = action.action.open_url;
      return QDesktopServices::openUrl(
          QUrl(QString::fromUtf8(url.url, static_cast<qsizetype>(url.len))));
    }
    case GHOSTTY_ACTION_EXPORT_TERMINAL_IO: {
      const auto& data = action.action.export_terminal_io;
      if (data.contents == nullptr) {
        return false;
      }
      const QString path = QFileDialog::getSaveFileName(
          this, tr("Export terminal data"), QStringLiteral("terminal.txt"));
      if (path.isEmpty()) {
        return true;
      }
      QSaveFile file(path);
      if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Export failed"), file.errorString());
        return true;
      }
      file.write(data.contents, static_cast<qint64>(data.len));
      if (!file.commit()) {
        QMessageBox::warning(this, tr("Export failed"), file.errorString());
      }
      return true;
    }
    case GHOSTTY_ACTION_COPY_TITLE_TO_CLIPBOARD:
      QGuiApplication::clipboard()->setText(m_title);
      return true;
    case GHOSTTY_ACTION_MOUSE_OVER_LINK: {
      const auto& link = action.action.mouse_over_link;
      setToolTip(
          link.url == nullptr
              ? QString()
              : QString::fromUtf8(link.url, static_cast<qsizetype>(link.len)));
      return true;
    }
    case GHOSTTY_ACTION_RENDERER_HEALTH:
      if (action.action.renderer_health == GHOSTTY_RENDERER_HEALTH_HEALTHY) {
        m_statusOverlay->hide();
      } else {
        m_statusOverlay->setText(
            tr("The terminal renderer stopped unexpectedly."));
        m_statusOverlay->show();
        layoutOverlays();
      }
      return true;
    case GHOSTTY_ACTION_SHOW_CHILD_EXITED:
      m_statusOverlay->setText(tr("The shell exited with status %1.")
                                   .arg(action.action.child_exited.exit_code));
      m_statusOverlay->show();
      layoutOverlays();
      return true;
    case GHOSTTY_ACTION_INSPECTOR: {
      const auto mode = action.action.inspector;
      const bool shouldShow =
          mode == GHOSTTY_INSPECTOR_SHOW ||
          (mode == GHOSTTY_INSPECTOR_TOGGLE &&
           (m_inspectorWindow == nullptr || !m_inspectorWindow->isVisible()));
      if (!shouldShow) {
        if (m_inspectorWindow != nullptr) {
          m_inspectorWindow->hide();
        }
        return true;
      }
      if (m_inspectorWindow == nullptr) {
        m_inspectorWindow = new InspectorWindow(this, window());
      }
      m_inspectorWindow->show();
      m_inspectorWindow->raise();
      m_inspectorWindow->activateWindow();
      return true;
    }
    case GHOSTTY_ACTION_RENDER_INSPECTOR:
      if (m_inspectorWindow == nullptr) {
        return false;
      }
      m_inspectorWindow->requestRender();
      return true;
    case GHOSTTY_ACTION_START_SEARCH:
      showSearch(action.action.start_search.needle);
      return true;
    case GHOSTTY_ACTION_END_SEARCH:
      m_searchFrame->hide();
      setFocus(Qt::OtherFocusReason);
      return true;
    case GHOSTTY_ACTION_SEARCH_TOTAL:
      m_searchTotal = action.action.search_total.total;
      updateSearchCount();
      return true;
    case GHOSTTY_ACTION_SEARCH_SELECTED:
      m_searchSelected = action.action.search_selected.selected;
      updateSearchCount();
      return true;
    case GHOSTTY_ACTION_SHOW_ON_SCREEN_KEYBOARD:
      QGuiApplication::inputMethod()->show();
      return true;
    case GHOSTTY_ACTION_CONFIG_CHANGE: {
      if (action.action.config_change.config == nullptr) {
        return false;
      }
      ghostty_config_t applied =
          ghostty_config_clone(action.action.config_change.config);
      if (applied == nullptr) {
        return false;
      }
      if (m_appliedConfig != nullptr) {
        ghostty_config_free(m_appliedConfig);
      }
      m_appliedConfig = applied;

      m_focusFollowsMouse = false;
      ghostty_config_get(m_appliedConfig, &m_focusFollowsMouse,
                         "focus-follows-mouse",
                         std::strlen("focus-follows-mouse"));
      bool progressEnabled = true;
      ghostty_config_get(m_appliedConfig, &progressEnabled, "progress-style",
                         std::strlen("progress-style"));
      if (!progressEnabled) {
        m_progressTimer->stop();
        m_progressBar->hide();
      }
      const char* scrollbarMode = nullptr;
      if (ghostty_config_get(m_appliedConfig,
                             static_cast<void*>(&scrollbarMode), "scrollbar",
                             std::strlen("scrollbar")) &&
          scrollbarMode != nullptr && QByteArray(scrollbarMode) == "never") {
        m_scrollMaximum = 0;
        m_scrollBar->hide();
      }
      layoutOverlays();
      return true;
    }
    case GHOSTTY_ACTION_SCROLLBAR:
      updateScrollbar(action.action.scrollbar);
      return true;
    case GHOSTTY_ACTION_PROGRESS_REPORT:
      updateProgress(action.action.progress_report);
      return true;
    case GHOSTTY_ACTION_READONLY:
      m_readonlyBadge->setVisible(action.action.readonly ==
                                  GHOSTTY_READONLY_ON);
      layoutOverlays();
      return true;
    case GHOSTTY_ACTION_KEY_SEQUENCE:
      if (action.action.key_sequence.active) {
        const ghostty_input_trigger_s trigger =
            action.action.key_sequence.trigger;
        QStringList parts;
        if (trigger.mods & GHOSTTY_MODS_CTRL) {
          parts.append(tr("Ctrl"));
        }
        if (trigger.mods & GHOSTTY_MODS_ALT) {
          parts.append(tr("Alt"));
        }
        if (trigger.mods & GHOSTTY_MODS_SHIFT) {
          parts.append(tr("Shift"));
        }
        if (trigger.mods & GHOSTTY_MODS_SUPER) {
          parts.append(tr("Meta"));
        }
        if (trigger.tag == GHOSTTY_TRIGGER_UNICODE) {
          const char32_t codepoint = static_cast<char32_t>(trigger.key.unicode);
          parts.append(QString::fromUcs4(&codepoint, 1));
        } else if (trigger.tag == GHOSTTY_TRIGGER_PHYSICAL) {
          const auto key = trigger.key.physical;
          if (key >= GHOSTTY_KEY_A && key <= GHOSTTY_KEY_Z) {
            parts.append(
                QChar(QLatin1Char('A').unicode() + key - GHOSTTY_KEY_A));
          } else if (key >= GHOSTTY_KEY_DIGIT_0 && key <= GHOSTTY_KEY_DIGIT_9) {
            parts.append(
                QChar(QLatin1Char('0').unicode() + key - GHOSTTY_KEY_DIGIT_0));
          } else {
            parts.append(tr("Key %1").arg(static_cast<int>(key)));
          }
        }
        m_keySequence.append(parts.join(QLatin1Char('+')));
      } else {
        m_keySequence.clear();
      }
      updateKeyState();
      return true;
    case GHOSTTY_ACTION_KEY_TABLE:
      if (action.action.key_table.tag == GHOSTTY_KEY_TABLE_ACTIVATE) {
        const auto& value = action.action.key_table.value.activate;
        m_keyTables.append(
            QString::fromUtf8(value.name, static_cast<qsizetype>(value.len)));
      } else if (action.action.key_table.tag == GHOSTTY_KEY_TABLE_DEACTIVATE) {
        if (!m_keyTables.isEmpty()) {
          m_keyTables.removeLast();
        }
      } else if (action.action.key_table.tag ==
                 GHOSTTY_KEY_TABLE_DEACTIVATE_ALL) {
        m_keyTables.clear();
      }
      updateKeyState();
      return true;
    case GHOSTTY_ACTION_COMMAND_FINISHED:
      commandFinished(action.action.command_finished);
      return true;
    case GHOSTTY_ACTION_SELECTION_CHANGED:
    case GHOSTTY_ACTION_COLOR_CHANGE:
      return true;
    default:
      return std::nullopt;
  }
}

void TerminalWidget::initializeGL() {
  connect(context(), &QOpenGLContext::aboutToBeDestroyed, this,
          &TerminalWidget::cleanupContext, Qt::DirectConnection);

  if (m_surface == nullptr) {
    createSurface();
  } else if (!m_contextRealized) {
    m_contextRealized = ghostty_surface_opengl_realize(m_surface);
  }

  syncSurfaceMetrics();
}

void TerminalWidget::paintGL() {
  if (m_surface == nullptr || !m_contextRealized) {
    return;
  }

  const qreal scale = devicePixelRatioF();
  context()->functions()->glViewport(0, 0, qRound(width() * scale),
                                     qRound(height() * scale));
  ghostty_surface_draw(m_surface);
}

void TerminalWidget::resizeGL(int, int) {
  syncSurfaceMetrics();
}

void TerminalWidget::resizeEvent(QResizeEvent* event) {
  QOpenGLWidget::resizeEvent(event);
  layoutOverlays();
}

void TerminalWidget::keyPressEvent(QKeyEvent* event) {
  const ghostty_input_action_e action =
      event->isAutoRepeat() ? GHOSTTY_ACTION_REPEAT : GHOSTTY_ACTION_PRESS;
  if (sendKey(event, action)) {
    event->accept();
  } else {
    QOpenGLWidget::keyPressEvent(event);
  }
}

void TerminalWidget::keyReleaseEvent(QKeyEvent* event) {
  if (!ghosttyShouldSendKeyRelease(*event)) {
    event->accept();
    return;
  }
  if (sendKey(event, GHOSTTY_ACTION_RELEASE)) {
    event->accept();
  } else {
    QOpenGLWidget::keyReleaseEvent(event);
  }
}

void TerminalWidget::inputMethodEvent(QInputMethodEvent* event) {
  if (m_surface == nullptr) {
    return;
  }

  const QByteArray preedit = event->preeditString().toUtf8();
  m_composing = !preedit.isEmpty();
  ghostty_surface_preedit(m_surface, preedit.constData(),
                          static_cast<uintptr_t>(preedit.size()));

  const QByteArray commit = event->commitString().toUtf8();
  if (!commit.isEmpty()) {
    ghostty_surface_text(m_surface, commit.constData(),
                         static_cast<uintptr_t>(commit.size()));
  }
  event->accept();
}

QVariant TerminalWidget::inputMethodQuery(Qt::InputMethodQuery query) const {
  if (query == Qt::ImCursorRectangle && m_surface != nullptr) {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
    ghostty_surface_ime_point(m_surface, &x, &y, &width, &height);
    const qreal scale = std::max<qreal>(1.0, devicePixelRatioF());
    return QRectF(x / scale, y / scale, width / scale, height / scale);
  }

  return QOpenGLWidget::inputMethodQuery(query);
}

void TerminalWidget::mouseMoveEvent(QMouseEvent* event) {
  if (m_focusFollowsMouse && !hasFocus()) {
    setFocus(Qt::MouseFocusReason);
  }
  sendMousePosition(event->position(), event->modifiers());
  event->accept();
}

void TerminalWidget::mousePressEvent(QMouseEvent* event) {
  const QWidget* focus = QApplication::focusWidget();
  const bool hadFocus =
      hasFocus() || focus == this || (focus != nullptr && isAncestorOf(focus));
  if (event->button() == Qt::LeftButton) {
    m_suppressLeftMouseRelease = !hadFocus;
  } else if (event->button() == Qt::RightButton) {
    m_contextMenuPending = false;
  }
  setFocus(Qt::MouseFocusReason);

  if (event->button() == Qt::LeftButton && !hadFocus) {
    event->accept();
    return;
  }
  if (m_surface != nullptr) {
    sendMousePosition(event->position(), event->modifiers());
    const bool consumed = ghostty_surface_mouse_button(
        m_surface, GHOSTTY_MOUSE_PRESS, mouseButton(event->button()),
        modifiers(event->modifiers()));
    if (event->button() == Qt::RightButton && !consumed) {
      m_contextMenuPosition = event->globalPosition().toPoint();
      m_contextMenuPending = true;
    }
  }
  event->accept();
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && m_suppressLeftMouseRelease) {
    m_suppressLeftMouseRelease = false;
    event->accept();
    return;
  }
  if (m_surface != nullptr) {
    sendMousePosition(event->position(), event->modifiers());
    ghostty_surface_mouse_button(m_surface, GHOSTTY_MOUSE_RELEASE,
                                 mouseButton(event->button()),
                                 modifiers(event->modifiers()));
  }
  if (event->button() == Qt::RightButton && m_contextMenuPending) {
    m_contextMenuPending = false;
    m_contextMenu->popup(m_contextMenuPosition);
  }
  event->accept();
}

void TerminalWidget::wheelEvent(QWheelEvent* event) {
  if (m_surface == nullptr) {
    return;
  }

  const QPoint pixel = event->pixelDelta();
  const bool precise = !pixel.isNull();
  const QPointF delta =
      precise ? QPointF(pixel) : QPointF(event->angleDelta()) / 120.0;
  const ghostty_input_scroll_mods_t scrollMods = precise ? 1 : 0;
  ghostty_surface_mouse_scroll(m_surface, delta.x(), delta.y(), scrollMods);
  event->accept();
}

void TerminalWidget::leaveEvent(QEvent* event) {
  if (m_surface != nullptr && QGuiApplication::mouseButtons() == Qt::NoButton) {
    ghostty_surface_mouse_pos(m_surface, -1, -1,
                              modifiers(QGuiApplication::keyboardModifiers()));
  }
  QOpenGLWidget::leaveEvent(event);
}

void TerminalWidget::focusInEvent(QFocusEvent* event) {
  scheduleSurfaceFocus(true);
  emit focused();
  QOpenGLWidget::focusInEvent(event);
}

void TerminalWidget::focusOutEvent(QFocusEvent* event) {
  scheduleSurfaceFocus(false);
  QOpenGLWidget::focusOutEvent(event);
}

bool TerminalWidget::focusNextPrevChild(bool) {
  // Tab and Backtab are terminal input, not Qt focus traversal.
  return false;
}

void TerminalWidget::showEvent(QShowEvent* event) {
  if (m_surface != nullptr) {
    ghostty_surface_set_occlusion(m_surface, true);
  }
  QOpenGLWidget::showEvent(event);
}

void TerminalWidget::hideEvent(QHideEvent* event) {
  if (m_surface != nullptr) {
    ghostty_surface_set_occlusion(m_surface, false);
  }
  QOpenGLWidget::hideEvent(event);
}

bool TerminalWidget::event(QEvent* event) {
  if (event->type() == QEvent::DevicePixelRatioChange && m_surface != nullptr) {
    syncSurfaceMetrics();
  }
  return QOpenGLWidget::event(event);
}

bool TerminalWidget::eventFilter(QObject* watched, QEvent* event) {
  if (watched == m_searchEdit && event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape) {
      runBindingAction(QStringLiteral("end_search"));
      return true;
    }
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
      runBindingAction(key->modifiers().testFlag(Qt::ShiftModifier)
                           ? QStringLiteral("navigate_search:previous")
                           : QStringLiteral("navigate_search:next"));
      return true;
    }
  }
  return QOpenGLWidget::eventFilter(watched, event);
}

void TerminalWidget::cleanupContext() {
  if (m_surface == nullptr || !m_contextRealized || context() == nullptr) {
    return;
  }

  makeCurrent();
  ghostty_surface_opengl_unrealize(m_surface);
  m_contextRealized = false;
  doneCurrent();
}

ghostty_opengl_proc_t TerminalWidget::getProcAddress(const char* name) {
  QOpenGLContext* current = QOpenGLContext::currentContext();
  return current == nullptr ? nullptr : current->getProcAddress(name);
}

uint32_t TerminalWidget::getFramebuffer(void* userdata) {
  auto* widget = static_cast<TerminalWidget*>(userdata);
  return widget == nullptr ? 0 : widget->defaultFramebufferObject();
}

void TerminalWidget::createSurface() {
  m_config.platform_tag = GHOSTTY_PLATFORM_OPENGL;
  m_config.platform.opengl.userdata = this;
  m_config.platform.opengl.get_proc_address = &TerminalWidget::getProcAddress;
  m_config.platform.opengl.get_framebuffer = &TerminalWidget::getFramebuffer;
  m_config.userdata = this;
  m_config.scale_factor = devicePixelRatioF();
  m_config.working_directory = m_workingDirectoryUtf8.isEmpty()
                                   ? nullptr
                                   : m_workingDirectoryUtf8.constData();
  m_config.command =
      m_commandUtf8.isEmpty() ? nullptr : m_commandUtf8.constData();
  m_config.initial_input =
      m_initialInputUtf8.isEmpty() ? nullptr : m_initialInputUtf8.constData();

  m_surface = ghostty_surface_new(m_app->handle(), &m_config);
  m_contextRealized = m_surface != nullptr;
  if (m_surface == nullptr) {
    qCritical("Failed to create Ghostty OpenGL surface");
  }
}

void TerminalWidget::destroySurface() {
  if (m_surface == nullptr) {
    return;
  }

  if (context() != nullptr && context()->isValid()) {
    makeCurrent();
    ghostty_surface_free(m_surface);
    doneCurrent();
  } else {
    ghostty_surface_free(m_surface);
  }
  m_surface = nullptr;
  m_contextRealized = false;
}

void TerminalWidget::syncSurfaceMetrics() {
  if (m_surface == nullptr) {
    return;
  }

  const qreal scale = std::max<qreal>(1.0, devicePixelRatioF());
  ghostty_surface_set_content_scale(m_surface, scale, scale);
  ghostty_surface_set_size(m_surface, qRound(width() * scale),
                           qRound(height() * scale));
}

bool TerminalWidget::sendKey(QKeyEvent* event, ghostty_input_action_e action) {
  if (m_surface == nullptr) {
    return false;
  }

  int modifierBits = modifiers(event->modifiers());
  switch (event->nativeScanCode()) {
    case 0x3e:
      modifierBits |= GHOSTTY_MODS_SHIFT_RIGHT;
      break;
    case 0x69:
      modifierBits |= GHOSTTY_MODS_CTRL_RIGHT;
      break;
    case 0x6c:
      modifierBits |= GHOSTTY_MODS_ALT_RIGHT;
      break;
    case 0x86:
      modifierBits |= GHOSTTY_MODS_SUPER_RIGHT;
      break;
    default:
      break;
  }

  const uint32_t unshifted = ghosttyUnshiftedCodepoint(*event);
  const QByteArray text = ghosttyKeyText(*event, unshifted);

  int consumedBits = GHOSTTY_MODS_NONE;
  const std::u32string codepoints = QString::fromUtf8(text).toStdU32String();
  if (event->modifiers().testFlag(Qt::ShiftModifier) && unshifted != 0 &&
      !codepoints.empty() && codepoints.front() != unshifted) {
    consumedBits |= GHOSTTY_MODS_SHIFT;
  }

  ghostty_input_key_s key{};
  key.action = action;
  key.mods = static_cast<ghostty_input_mods_e>(modifierBits);
  key.consumed_mods = static_cast<ghostty_input_mods_e>(consumedBits);
  key.keycode = event->nativeScanCode();
  key.text = text.isEmpty() ? nullptr : text.constData();
  key.unshifted_codepoint = unshifted;
  key.composing = m_composing;
  key.logical_key = ghosttyLogicalKey(*event);
  return ghostty_surface_key(m_surface, key);
}

void TerminalWidget::sendMousePosition(
    const QPointF& position,
    Qt::KeyboardModifiers keyboardModifiers) {
  if (m_surface != nullptr) {
    const QPointF surfacePosition =
        ghosttySurfaceMousePosition(position, devicePixelRatioF());
    ghostty_surface_mouse_pos(m_surface, surfacePosition.x(),
                              surfacePosition.y(),
                              modifiers(keyboardModifiers));
  }
}

void TerminalWidget::setMouseShape(ghostty_action_mouse_shape_e shape) {
  m_mouseShape = shape;
  applyMouseCursor();
}

void TerminalWidget::setMouseVisible(bool visible) {
  m_mouseVisible = visible;
  applyMouseCursor();
}

void TerminalWidget::applyMouseCursor() {
  if (!m_mouseVisible) {
    setCursor(Qt::BlankCursor);
    return;
  }

  switch (m_mouseShape) {
    case GHOSTTY_MOUSE_SHAPE_HELP:
      setCursor(Qt::WhatsThisCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_POINTER:
    case GHOSTTY_MOUSE_SHAPE_ZOOM_IN:
    case GHOSTTY_MOUSE_SHAPE_ZOOM_OUT:
      setCursor(Qt::PointingHandCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_PROGRESS:
      setCursor(Qt::BusyCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_WAIT:
      setCursor(Qt::WaitCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_CELL:
    case GHOSTTY_MOUSE_SHAPE_CROSSHAIR:
      setCursor(Qt::CrossCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_TEXT:
    case GHOSTTY_MOUSE_SHAPE_VERTICAL_TEXT:
      setCursor(Qt::IBeamCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_ALIAS:
      setCursor(Qt::DragLinkCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_COPY:
      setCursor(Qt::DragCopyCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_MOVE:
    case GHOSTTY_MOUSE_SHAPE_ALL_SCROLL:
      setCursor(Qt::SizeAllCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_NOT_ALLOWED:
    case GHOSTTY_MOUSE_SHAPE_NO_DROP:
      setCursor(Qt::ForbiddenCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_GRAB:
      setCursor(Qt::OpenHandCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_GRABBING:
      setCursor(Qt::ClosedHandCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_E_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_W_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_EW_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_COL_RESIZE:
      setCursor(Qt::SizeHorCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_N_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_S_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_NS_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_ROW_RESIZE:
      setCursor(Qt::SizeVerCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_NE_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_SW_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_NESW_RESIZE:
      setCursor(Qt::SizeBDiagCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_NW_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_SE_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_NWSE_RESIZE:
      setCursor(Qt::SizeFDiagCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_DEFAULT:
    case GHOSTTY_MOUSE_SHAPE_CONTEXT_MENU:
    default:
      setCursor(Qt::ArrowCursor);
      break;
  }
}

void TerminalWidget::scheduleSurfaceFocus(bool focused) {
  m_pendingSurfaceFocus = focused;
  if (m_focusUpdateScheduled) {
    return;
  }
  m_focusUpdateScheduled = true;
  QTimer::singleShot(0, this, [this]() {
    m_focusUpdateScheduled = false;
    if (!m_pendingSurfaceFocus.has_value()) {
      return;
    }
    const bool focused = m_pendingSurfaceFocus.value();
    m_pendingSurfaceFocus.reset();
    if (m_surface != nullptr) {
      ghostty_surface_set_focus(m_surface, focused);
    }
  });
}

bool TerminalWidget::runBindingAction(const QString& action) {
  if (m_surface == nullptr) {
    return false;
  }
  const QByteArray bytes = action.toUtf8();
  return ghostty_surface_binding_action(m_surface, bytes.constData(),
                                        static_cast<uintptr_t>(bytes.size()));
}

void TerminalWidget::showSearch(const char* needle) {
  if (needle != nullptr && *needle != '\0') {
    const QSignalBlocker blocker(m_searchEdit);
    m_searchEdit->setText(QString::fromUtf8(needle));
  }
  m_searchTotal = -1;
  m_searchSelected = -1;
  updateSearchCount();
  m_searchFrame->show();
  m_searchFrame->raise();
  layoutOverlays();
  m_searchEdit->setFocus(Qt::ShortcutFocusReason);
  m_searchEdit->selectAll();
}

void TerminalWidget::updateSearchCount() {
  if (m_searchSelected >= 0 && m_searchTotal >= 0) {
    m_searchCount->setText(
        tr("%1/%2").arg(m_searchSelected).arg(m_searchTotal));
  } else if (m_searchTotal >= 0) {
    m_searchCount->setText(tr("–/%1").arg(m_searchTotal));
  } else {
    m_searchCount->clear();
  }
  layoutOverlays();
}

void TerminalWidget::updateProgress(
    const ghostty_action_progress_report_s& progress) {
  bool enabled = true;
  ghostty_config_get(m_appliedConfig, &enabled, "progress-style",
                     std::strlen("progress-style"));
  if (!enabled || progress.state == GHOSTTY_PROGRESS_STATE_REMOVE) {
    m_progressTimer->stop();
    m_progressBar->hide();
    return;
  }

  QPalette palette = this->palette();
  if (progress.state == GHOSTTY_PROGRESS_STATE_ERROR) {
    palette.setColor(QPalette::Highlight, QColor(210, 55, 55));
  } else if (progress.state == GHOSTTY_PROGRESS_STATE_PAUSE) {
    palette.setColor(QPalette::Highlight, QColor(220, 145, 35));
  }
  m_progressBar->setPalette(palette);
  if (progress.state == GHOSTTY_PROGRESS_STATE_INDETERMINATE ||
      (progress.progress < 0 &&
       progress.state != GHOSTTY_PROGRESS_STATE_PAUSE)) {
    m_progressBar->setRange(0, 0);
  } else {
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(progress.progress < 0
                                ? 100
                                : std::clamp<int>(progress.progress, 0, 100));
  }
  m_progressBar->show();
  m_progressBar->raise();
  m_progressTimer->start();
  layoutOverlays();
}

void TerminalWidget::commandFinished(
    const ghostty_action_command_finished_s& command) {
  const bool notifyNext = m_notifyNextCommandAction->isChecked();
  if (notifyNext) {
    m_notifyNextCommandAction->setChecked(false);
  }

  const char* modeValue = nullptr;
  if (!ghostty_config_get(m_appliedConfig, static_cast<void*>(&modeValue),
                          "notify-on-command-finish",
                          std::strlen("notify-on-command-finish")) ||
      modeValue == nullptr) {
    return;
  }
  const QByteArray mode(modeValue);
  const QWidget* focus = QApplication::focusWidget();
  const bool surfaceFocused =
      focus == this || (focus != nullptr && isAncestorOf(focus));
  if (!notifyNext &&
      (mode == "never" || (mode == "unfocused" && surfaceFocused))) {
    return;
  }

  uint64_t threshold = 0;
  ghostty_config_get(m_appliedConfig, &threshold,
                     "notify-on-command-finish-after",
                     std::strlen("notify-on-command-finish-after"));
  if (command.duration <= threshold) {
    return;
  }

  uint32_t configuredActions = 0;
  ghostty_config_get(m_appliedConfig, &configuredActions,
                     "notify-on-command-finish-action",
                     std::strlen("notify-on-command-finish-action"));
  if ((configuredActions & 1U) != 0) {
    QApplication::beep();
    QApplication::alert(window());
    emit bellRang();
  }
  if ((configuredActions & 2U) == 0) {
    return;
  }

  const QString title = command.exit_code < 0    ? tr("Command Finished")
                        : command.exit_code == 0 ? tr("Command Succeeded")
                                                 : tr("Command Failed");
  const double seconds = static_cast<double>(command.duration) / 1.0e9;
  QString elapsed;
  if (seconds >= 3600) {
    elapsed = tr("%1 h").arg(seconds / 3600, 0, 'f', 1);
  } else if (seconds >= 60) {
    elapsed = tr("%1 min").arg(seconds / 60, 0, 'f', 1);
  } else if (seconds >= 1) {
    elapsed = tr("%1 s").arg(seconds, 0, 'f', 1);
  } else {
    elapsed = tr("%1 ms").arg(command.duration / 1000000);
  }
  const QString body = command.exit_code < 0
                           ? tr("Command took %1.").arg(elapsed)
                           : tr("Command took %1 and exited with code %2.")
                                 .arg(elapsed)
                                 .arg(command.exit_code);
  m_app->sendNotification(title, body);
}

void TerminalWidget::updateScrollbar(
    const ghostty_action_scrollbar_s& scrollbar) {
  const char* mode = nullptr;
  if (ghostty_config_get(m_appliedConfig, static_cast<void*>(&mode),
                         "scrollbar", std::strlen("scrollbar")) &&
      mode != nullptr && QByteArray(mode) == "never") {
    m_scrollMaximum = 0;
    m_scrollBar->hide();
    layoutOverlays();
    return;
  }

  m_scrollMaximum =
      scrollbar.total > scrollbar.len ? scrollbar.total - scrollbar.len : 0;
  const uint64_t displayMaximum =
      std::min<uint64_t>(m_scrollMaximum, std::numeric_limits<int>::max());
  const int displayValue = m_scrollMaximum == 0
                               ? 0
                               : static_cast<int>(std::llround(
                                     static_cast<double>(scrollbar.offset) /
                                     static_cast<double>(m_scrollMaximum) *
                                     static_cast<double>(displayMaximum)));
  const int pageStep =
      scrollbar.total == 0
          ? 1
          : std::max(1, static_cast<int>(
                            std::llround(static_cast<double>(scrollbar.len) /
                                         static_cast<double>(scrollbar.total) *
                                         static_cast<double>(displayMaximum))));

  const QSignalBlocker blocker(m_scrollBar);
  m_scrollBar->setRange(0, static_cast<int>(displayMaximum));
  m_scrollBar->setPageStep(pageStep);
  m_scrollBar->setValue(displayValue);
  m_scrollBar->setVisible(m_scrollMaximum > 0);
  layoutOverlays();
}

void TerminalWidget::updateKeyState() {
  QStringList state;
  if (!m_keyTables.isEmpty()) {
    state.append(tr("Table: %1").arg(m_keyTables.join(QStringLiteral(", "))));
  }
  if (!m_keySequence.isEmpty()) {
    state.append(tr("Sequence: %1").arg(m_keySequence.join(QLatin1Char(' '))));
  }
  m_keyStateBadge->setText(state.join(QStringLiteral("  ")));
  m_keyStateBadge->setVisible(!state.isEmpty());
  layoutOverlays();
}

void TerminalWidget::layoutOverlays() {
  const int scrollWidth = m_scrollBar != nullptr && m_scrollBar->isVisible()
                              ? m_scrollBar->sizeHint().width()
                              : 0;
  if (m_progressBar != nullptr) {
    m_progressBar->setGeometry(0, 0, std::max(0, width() - scrollWidth), 4);
  }
  if (m_scrollBar != nullptr) {
    m_scrollBar->setGeometry(std::max(0, width() - scrollWidth), 0, scrollWidth,
                             height());
    m_scrollBar->raise();
  }
  if (m_searchFrame != nullptr) {
    const QSize hint = m_searchFrame->sizeHint().boundedTo(QSize(
        std::max(0, width() - scrollWidth - 16), std::max(0, height() - 16)));
    m_searchFrame->setGeometry(
        std::max(8, width() - scrollWidth - hint.width() - 8), 8, hint.width(),
        hint.height());
  }
  if (m_readonlyBadge != nullptr) {
    const QSize hint = m_readonlyBadge->sizeHint();
    m_readonlyBadge->setGeometry(8, std::max(0, height() - hint.height() - 8),
                                 hint.width(), hint.height());
    m_readonlyBadge->raise();
  }
  if (m_keyStateBadge != nullptr) {
    const QSize hint = m_keyStateBadge->sizeHint();
    m_keyStateBadge->setGeometry(
        std::max(8, width() - scrollWidth - hint.width() - 8),
        std::max(0, height() - hint.height() - 8), hint.width(), hint.height());
    m_keyStateBadge->raise();
  }
  if (m_statusOverlay != nullptr) {
    const int overlayWidth =
        std::min(420, std::max(0, width() - scrollWidth - 32));
    const QSize hint =
        m_statusOverlay->sizeHint().boundedTo(QSize(overlayWidth, height()));
    m_statusOverlay->setGeometry((width() - scrollWidth - overlayWidth) / 2,
                                 (height() - hint.height()) / 2, overlayWidth,
                                 hint.height());
    m_statusOverlay->raise();
  }
}

ghostty_input_mods_e TerminalWidget::modifiers(
    Qt::KeyboardModifiers keyboardModifiers) {
  int result = GHOSTTY_MODS_NONE;
  if (keyboardModifiers.testFlag(Qt::ShiftModifier)) {
    result |= GHOSTTY_MODS_SHIFT;
  }
  if (keyboardModifiers.testFlag(Qt::ControlModifier)) {
    result |= GHOSTTY_MODS_CTRL;
  }
  if (keyboardModifiers.testFlag(Qt::AltModifier)) {
    result |= GHOSTTY_MODS_ALT;
  }
  if (keyboardModifiers.testFlag(Qt::MetaModifier)) {
    result |= GHOSTTY_MODS_SUPER;
  }
  return static_cast<ghostty_input_mods_e>(result);
}

ghostty_input_mouse_button_e TerminalWidget::mouseButton(
    Qt::MouseButton button) {
  switch (button) {
    case Qt::LeftButton:
      return GHOSTTY_MOUSE_LEFT;
    case Qt::RightButton:
      return GHOSTTY_MOUSE_RIGHT;
    case Qt::MiddleButton:
      return GHOSTTY_MOUSE_MIDDLE;
    case Qt::BackButton:
      return GHOSTTY_MOUSE_FOUR;
    case Qt::ForwardButton:
      return GHOSTTY_MOUSE_FIVE;
    case Qt::TaskButton:
      return GHOSTTY_MOUSE_SIX;
    case Qt::ExtraButton4:
      return GHOSTTY_MOUSE_SEVEN;
    case Qt::ExtraButton5:
      return GHOSTTY_MOUSE_EIGHT;
    case Qt::ExtraButton6:
      return GHOSTTY_MOUSE_NINE;
    case Qt::ExtraButton7:
      return GHOSTTY_MOUSE_TEN;
    case Qt::ExtraButton8:
      return GHOSTTY_MOUSE_ELEVEN;
    default:
      return GHOSTTY_MOUSE_UNKNOWN;
  }
}
