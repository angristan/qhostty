#pragma once

#include <QDialog>
#include <QOpenGLWidget>
#include <QPointer>

#include <ghostty.h>

class QFocusEvent;
class QInputMethodEvent;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class TerminalWidget;

class InspectorView final : public QOpenGLWidget {
  Q_OBJECT

 public:
  explicit InspectorView(TerminalWidget* terminal, QWidget* parent = nullptr);
  ~InspectorView() override;

 public slots:
  void requestRender();
  void closeSurface();

 protected:
  void initializeGL() override;
  void paintGL() override;
  void resizeGL(int width, int height) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void inputMethodEvent(QInputMethodEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;

 private:
  void cleanup();
  void sendKey(QKeyEvent* event, ghostty_input_action_e action);
  void sendMouseButton(QMouseEvent* event, ghostty_input_mouse_state_e state);
  void syncMetrics();

  QPointer<TerminalWidget> m_terminal;
  ghostty_surface_t m_surface = nullptr;
  ghostty_inspector_t m_inspector = nullptr;
  bool m_realized = false;
};

class InspectorWindow final : public QDialog {
  Q_OBJECT

 public:
  explicit InspectorWindow(TerminalWidget* terminal, QWidget* parent = nullptr);

  void requestRender();

 private:
  InspectorView* m_view;
};
