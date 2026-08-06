#include "InspectorWindow.h"

#include "TerminalWidget.h"

#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

namespace {
ghostty_input_mods_e inputModifiers(Qt::KeyboardModifiers value) {
  int modifiers = GHOSTTY_MODS_NONE;
  if (value.testFlag(Qt::ShiftModifier)) {
    modifiers |= GHOSTTY_MODS_SHIFT;
  }
  if (value.testFlag(Qt::ControlModifier)) {
    modifiers |= GHOSTTY_MODS_CTRL;
  }
  if (value.testFlag(Qt::AltModifier)) {
    modifiers |= GHOSTTY_MODS_ALT;
  }
  if (value.testFlag(Qt::MetaModifier)) {
    modifiers |= GHOSTTY_MODS_SUPER;
  }
  return static_cast<ghostty_input_mods_e>(modifiers);
}

ghostty_input_key_e inputKey(int key) {
  if (key >= Qt::Key_0 && key <= Qt::Key_9) {
    return static_cast<ghostty_input_key_e>(GHOSTTY_KEY_DIGIT_0 + key -
                                            static_cast<int>(Qt::Key_0));
  }
  if (key >= Qt::Key_A && key <= Qt::Key_Z) {
    return static_cast<ghostty_input_key_e>(GHOSTTY_KEY_A + key -
                                            static_cast<int>(Qt::Key_A));
  }
  if (key >= Qt::Key_F1 && key <= Qt::Key_F25) {
    return static_cast<ghostty_input_key_e>(GHOSTTY_KEY_F1 + key -
                                            static_cast<int>(Qt::Key_F1));
  }
  switch (key) {
    case Qt::Key_QuoteLeft:
      return GHOSTTY_KEY_BACKQUOTE;
    case Qt::Key_Backslash:
      return GHOSTTY_KEY_BACKSLASH;
    case Qt::Key_BracketLeft:
      return GHOSTTY_KEY_BRACKET_LEFT;
    case Qt::Key_BracketRight:
      return GHOSTTY_KEY_BRACKET_RIGHT;
    case Qt::Key_Comma:
      return GHOSTTY_KEY_COMMA;
    case Qt::Key_Equal:
      return GHOSTTY_KEY_EQUAL;
    case Qt::Key_Minus:
      return GHOSTTY_KEY_MINUS;
    case Qt::Key_Period:
      return GHOSTTY_KEY_PERIOD;
    case Qt::Key_Apostrophe:
      return GHOSTTY_KEY_QUOTE;
    case Qt::Key_Semicolon:
      return GHOSTTY_KEY_SEMICOLON;
    case Qt::Key_Slash:
      return GHOSTTY_KEY_SLASH;
    case Qt::Key_Backspace:
      return GHOSTTY_KEY_BACKSPACE;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      return GHOSTTY_KEY_ENTER;
    case Qt::Key_Space:
      return GHOSTTY_KEY_SPACE;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
      return GHOSTTY_KEY_TAB;
    case Qt::Key_Delete:
      return GHOSTTY_KEY_DELETE;
    case Qt::Key_End:
      return GHOSTTY_KEY_END;
    case Qt::Key_Home:
      return GHOSTTY_KEY_HOME;
    case Qt::Key_Insert:
      return GHOSTTY_KEY_INSERT;
    case Qt::Key_PageDown:
      return GHOSTTY_KEY_PAGE_DOWN;
    case Qt::Key_PageUp:
      return GHOSTTY_KEY_PAGE_UP;
    case Qt::Key_Down:
      return GHOSTTY_KEY_ARROW_DOWN;
    case Qt::Key_Left:
      return GHOSTTY_KEY_ARROW_LEFT;
    case Qt::Key_Right:
      return GHOSTTY_KEY_ARROW_RIGHT;
    case Qt::Key_Up:
      return GHOSTTY_KEY_ARROW_UP;
    case Qt::Key_Escape:
      return GHOSTTY_KEY_ESCAPE;
    default:
      return GHOSTTY_KEY_UNIDENTIFIED;
  }
}

ghostty_input_mouse_button_e inputButton(Qt::MouseButton button) {
  switch (button) {
    case Qt::LeftButton:
      return GHOSTTY_MOUSE_LEFT;
    case Qt::RightButton:
      return GHOSTTY_MOUSE_RIGHT;
    case Qt::MiddleButton:
      return GHOSTTY_MOUSE_MIDDLE;
    default:
      return GHOSTTY_MOUSE_UNKNOWN;
  }
}
}  // namespace

InspectorView::InspectorView(TerminalWidget* terminal, QWidget* parent)
    : QOpenGLWidget(parent),
      m_terminal(terminal),
      m_surface(terminal == nullptr ? nullptr : terminal->surface()) {
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setAttribute(Qt::WA_InputMethodEnabled, true);
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
  if (terminal != nullptr) {
    connect(terminal, &TerminalWidget::surfaceClosing, this,
            &InspectorView::closeSurface);
  }
}

InspectorView::~InspectorView() {
  cleanup();
}

void InspectorView::requestRender() {
  update();
}

void InspectorView::closeSurface() {
  cleanup();
  if (window() != nullptr) {
    window()->close();
  }
}

void InspectorView::initializeGL() {
  if (m_surface == nullptr) {
    return;
  }
  m_inspector = ghostty_surface_inspector(m_surface);
  if (m_inspector == nullptr) {
    return;
  }
  m_realized = ghostty_inspector_opengl_realize(m_inspector);
  if (!m_realized) {
    ghostty_inspector_free(m_surface);
    m_inspector = nullptr;
    return;
  }
  connect(context(), &QOpenGLContext::aboutToBeDestroyed, this,
          &InspectorView::cleanup, Qt::DirectConnection);
  syncMetrics();
}

void InspectorView::paintGL() {
  if (!m_realized || m_inspector == nullptr) {
    return;
  }
  QOpenGLFunctions* functions = context()->functions();
  functions->glClearColor(0x28 / 255.0F, 0x2C / 255.0F, 0x34 / 255.0F, 1.0F);
  functions->glClear(GL_COLOR_BUFFER_BIT);
  ghostty_inspector_opengl_render(m_inspector);
}

void InspectorView::resizeGL(int, int) {
  syncMetrics();
}

void InspectorView::keyPressEvent(QKeyEvent* event) {
  sendKey(event,
          event->isAutoRepeat() ? GHOSTTY_ACTION_REPEAT : GHOSTTY_ACTION_PRESS);
  if (!event->text().isEmpty() && m_inspector != nullptr) {
    const QByteArray text = event->text().toUtf8();
    ghostty_inspector_text(m_inspector, text.constData());
  }
  event->accept();
}

void InspectorView::keyReleaseEvent(QKeyEvent* event) {
  if (!event->isAutoRepeat()) {
    sendKey(event, GHOSTTY_ACTION_RELEASE);
  }
  event->accept();
}

void InspectorView::inputMethodEvent(QInputMethodEvent* event) {
  if (m_inspector != nullptr && !event->commitString().isEmpty()) {
    const QByteArray text = event->commitString().toUtf8();
    ghostty_inspector_text(m_inspector, text.constData());
  }
  event->accept();
}

void InspectorView::mouseMoveEvent(QMouseEvent* event) {
  if (m_inspector != nullptr) {
    ghostty_inspector_mouse_pos(m_inspector, event->position().x(),
                                event->position().y());
  }
  event->accept();
}

void InspectorView::mousePressEvent(QMouseEvent* event) {
  sendMouseButton(event, GHOSTTY_MOUSE_PRESS);
  event->accept();
}

void InspectorView::mouseReleaseEvent(QMouseEvent* event) {
  sendMouseButton(event, GHOSTTY_MOUSE_RELEASE);
  event->accept();
}

void InspectorView::wheelEvent(QWheelEvent* event) {
  if (m_inspector != nullptr) {
    const bool precise = !event->pixelDelta().isNull();
    const QPoint delta = precise ? event->pixelDelta() : event->angleDelta();
    const double divisor = precise ? 1.0 : 120.0;
    ghostty_inspector_mouse_scroll(m_inspector, delta.x() / divisor,
                                   delta.y() / divisor, precise ? 1 : 0);
  }
  event->accept();
}

void InspectorView::focusInEvent(QFocusEvent* event) {
  QOpenGLWidget::focusInEvent(event);
  if (m_inspector != nullptr) {
    ghostty_inspector_set_focus(m_inspector, true);
  }
}

void InspectorView::focusOutEvent(QFocusEvent* event) {
  QOpenGLWidget::focusOutEvent(event);
  if (m_inspector != nullptr) {
    ghostty_inspector_set_focus(m_inspector, false);
  }
}

void InspectorView::cleanup() {
  if (m_inspector == nullptr) {
    return;
  }
  if (m_realized && context() != nullptr && context()->isValid()) {
    makeCurrent();
    ghostty_inspector_opengl_unrealize(m_inspector);
    doneCurrent();
  }
  if (m_surface != nullptr) {
    ghostty_inspector_free(m_surface);
  }
  m_realized = false;
  m_inspector = nullptr;
  m_surface = nullptr;
}

void InspectorView::sendKey(QKeyEvent* event, ghostty_input_action_e action) {
  if (m_inspector != nullptr) {
    ghostty_inspector_key(m_inspector, action, inputKey(event->key()),
                          inputModifiers(event->modifiers()));
  }
}

void InspectorView::sendMouseButton(QMouseEvent* event,
                                    ghostty_input_mouse_state_e state) {
  if (m_inspector != nullptr) {
    ghostty_inspector_mouse_button(m_inspector, state,
                                   inputButton(event->button()),
                                   inputModifiers(event->modifiers()));
  }
}

void InspectorView::syncMetrics() {
  if (m_inspector == nullptr) {
    return;
  }
  const qreal scale = std::max<qreal>(1.0, devicePixelRatioF());
  ghostty_inspector_set_content_scale(m_inspector, scale, scale);
  ghostty_inspector_set_size(m_inspector, qRound(width() * scale),
                             qRound(height() * scale));
}

InspectorWindow::InspectorWindow(TerminalWidget* terminal, QWidget* parent)
    : QDialog(parent), m_view(new InspectorView(terminal, this)) {
  setAttribute(Qt::WA_DeleteOnClose, true);
  setWindowTitle(tr("Terminal Inspector"));
  resize(840, 620);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_view);
}

void InspectorWindow::requestRender() {
  m_view->requestRender();
}
