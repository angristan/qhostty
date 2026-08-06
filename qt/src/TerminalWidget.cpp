#include "TerminalWidget.h"

#include "GhosttyApp.h"

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
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QUrl>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <cstring>
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
  if (m_config.working_directory != nullptr) {
    m_workingDirectory = QString::fromUtf8(m_config.working_directory);
    m_workingDirectoryUtf8 = m_workingDirectory.toUtf8();
    m_config.working_directory = nullptr;
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

  m_app->registerSurface(this);
}

TerminalWidget::~TerminalWidget() {
  destroySurface();
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
  const QClipboard::Mode mode =
      location == GHOSTTY_CLIPBOARD_SELECTION && clipboard->supportsSelection()
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

  QClipboard* clipboard = QGuiApplication::clipboard();
  const QClipboard::Mode mode =
      location == GHOSTTY_CLIPBOARD_SELECTION && clipboard->supportsSelection()
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

bool TerminalWidget::handleAction(const ghostty_action_s& action) {
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
    case GHOSTTY_ACTION_SELECTION_CHANGED:
    case GHOSTTY_ACTION_COLOR_CHANGE:
    case GHOSTTY_ACTION_SCROLLBAR:
    case GHOSTTY_ACTION_PROGRESS_REPORT:
    case GHOSTTY_ACTION_COMMAND_FINISHED:
    case GHOSTTY_ACTION_READONLY:
      return true;
    default:
      return false;
  }
}

void TerminalWidget::initializeGL() {
  connect(context(), &QOpenGLContext::aboutToBeDestroyed, this,
          &TerminalWidget::cleanupContext,
          static_cast<Qt::ConnectionType>(Qt::DirectConnection |
                                          Qt::UniqueConnection));

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
  sendMousePosition(event->position(), event->modifiers());
  event->accept();
}

void TerminalWidget::mousePressEvent(QMouseEvent* event) {
  if (m_surface != nullptr) {
    sendMousePosition(event->position(), event->modifiers());
    ghostty_surface_mouse_button(m_surface, GHOSTTY_MOUSE_PRESS,
                                 mouseButton(event->button()),
                                 modifiers(event->modifiers()));
  }
  setFocus(Qt::MouseFocusReason);
  event->accept();
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (m_surface != nullptr) {
    sendMousePosition(event->position(), event->modifiers());
    ghostty_surface_mouse_button(m_surface, GHOSTTY_MOUSE_RELEASE,
                                 mouseButton(event->button()),
                                 modifiers(event->modifiers()));
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

void TerminalWidget::focusInEvent(QFocusEvent* event) {
  if (m_surface != nullptr) {
    ghostty_surface_set_focus(m_surface, true);
  }
  emit focused();
  QOpenGLWidget::focusInEvent(event);
}

void TerminalWidget::focusOutEvent(QFocusEvent* event) {
  if (m_surface != nullptr) {
    ghostty_surface_set_focus(m_surface, false);
  }
  QOpenGLWidget::focusOutEvent(event);
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

  const QByteArray text = event->text().toUtf8();
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

  uint32_t unshifted = 0;
  const int qtKey = event->key();
  if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
    unshifted = static_cast<uint32_t>('a' + qtKey - Qt::Key_A);
  } else if (qtKey >= 0x20 && qtKey < 0x01000000) {
    unshifted = static_cast<uint32_t>(qtKey);
  }

  int consumedBits = GHOSTTY_MODS_NONE;
  const std::u32string codepoints = event->text().toStdU32String();
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
  return ghostty_surface_key(m_surface, key);
}

void TerminalWidget::sendMousePosition(
    const QPointF& position,
    Qt::KeyboardModifiers keyboardModifiers) {
  if (m_surface != nullptr) {
    const qreal scale = std::max<qreal>(1.0, devicePixelRatioF());
    ghostty_surface_mouse_pos(m_surface, position.x() * scale,
                              position.y() * scale,
                              modifiers(keyboardModifiers));
  }
}

void TerminalWidget::setMouseShape(ghostty_action_mouse_shape_e shape) {
  switch (shape) {
    case GHOSTTY_MOUSE_SHAPE_TEXT:
      setCursor(Qt::IBeamCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_POINTER:
      setCursor(Qt::PointingHandCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_CROSSHAIR:
      setCursor(Qt::CrossCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_GRAB:
      setCursor(Qt::OpenHandCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_GRABBING:
      setCursor(Qt::ClosedHandCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_NOT_ALLOWED:
    case GHOSTTY_MOUSE_SHAPE_NO_DROP:
      setCursor(Qt::ForbiddenCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_EW_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_COL_RESIZE:
      setCursor(Qt::SizeHorCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_NS_RESIZE:
    case GHOSTTY_MOUSE_SHAPE_ROW_RESIZE:
      setCursor(Qt::SizeVerCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_NESW_RESIZE:
      setCursor(Qt::SizeBDiagCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_NWSE_RESIZE:
      setCursor(Qt::SizeFDiagCursor);
      break;
    case GHOSTTY_MOUSE_SHAPE_WAIT:
    case GHOSTTY_MOUSE_SHAPE_PROGRESS:
      setCursor(Qt::WaitCursor);
      break;
    default:
      setCursor(Qt::ArrowCursor);
      break;
  }
}

void TerminalWidget::setMouseVisible(bool visible) {
  if (visible) {
    unsetCursor();
  } else {
    setCursor(Qt::BlankCursor);
  }
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

void TerminalWidget::layoutOverlays() {
  if (m_searchFrame != nullptr) {
    const QSize hint = m_searchFrame->sizeHint().boundedTo(
        QSize(std::max(0, width() - 16), std::max(0, height() - 16)));
    m_searchFrame->setGeometry(std::max(8, width() - hint.width() - 8), 8,
                               hint.width(), hint.height());
  }
  if (m_statusOverlay != nullptr) {
    const int overlayWidth = std::min(420, std::max(0, width() - 32));
    const QSize hint =
        m_statusOverlay->sizeHint().boundedTo(QSize(overlayWidth, height()));
    m_statusOverlay->setGeometry((width() - overlayWidth) / 2,
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
    default:
      return GHOSTTY_MOUSE_UNKNOWN;
  }
}
