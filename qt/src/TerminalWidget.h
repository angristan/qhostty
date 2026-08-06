#pragma once

#include <QByteArray>
#include <QOpenGLWidget>
#include <QPointer>
#include <QStringList>

#include <ghostty.h>

class QFocusEvent;
class QFrame;
class QHideEvent;
class QInputMethodEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QMouseEvent;
class QProgressBar;
class QResizeEvent;
class QScrollBar;
class QShowEvent;
class QTimer;
class QWheelEvent;
class GhosttyApp;
class InspectorWindow;

class TerminalWidget final : public QOpenGLWidget {
  Q_OBJECT

 public:
  explicit TerminalWidget(GhosttyApp* app,
                          QWidget* parent = nullptr,
                          const ghostty_surface_config_s* baseConfig = nullptr);
  ~TerminalWidget() override;

  [[nodiscard]] ghostty_surface_t surface() const { return m_surface; }
  [[nodiscard]] ghostty_config_t config() const { return m_appliedConfig; }
  [[nodiscard]] const QString& title() const { return m_title; }
  void setTitle(const QString& title);
  [[nodiscard]] const QString& workingDirectory() const {
    return m_workingDirectory;
  }
  [[nodiscard]] ghostty_surface_config_s inheritedConfig(
      ghostty_surface_context_e context) const;
  void freeInheritedConfig(ghostty_surface_config_s& config) const;

  bool readClipboard(ghostty_clipboard_e location, void* state);
  void confirmReadClipboard(const char* contents,
                            void* state,
                            ghostty_clipboard_request_e request);
  void writeClipboard(ghostty_clipboard_e location,
                      const ghostty_clipboard_content_s* contents,
                      size_t count,
                      bool confirm);
  void requestClose(bool processAlive);
  bool handleAction(const ghostty_action_s& action);
  bool runBindingAction(const QString& action);

 signals:
  void focused();
  void titleChanged(const QString& title);
  void tabTitleChanged(const QString& title);
  void closeRequested(TerminalWidget* widget);
  void bellRang();
  void surfaceClosing();

 protected:
  void initializeGL() override;
  void paintGL() override;
  void resizeGL(int width, int height) override;
  void resizeEvent(QResizeEvent* event) override;

  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void inputMethodEvent(QInputMethodEvent* event) override;
  QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

  void mouseMoveEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  bool event(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private slots:
  void cleanupContext();

 private:
  static ghostty_opengl_proc_t getProcAddress(const char* name);
  static uint32_t getFramebuffer(void* userdata);

  void createSurface();
  void destroySurface();
  void syncSurfaceMetrics();
  bool sendKey(QKeyEvent* event, ghostty_input_action_e action);
  void sendMousePosition(const QPointF& position,
                         Qt::KeyboardModifiers modifiers);
  void setMouseShape(ghostty_action_mouse_shape_e shape);
  void setMouseVisible(bool visible);
  void showSearch(const char* needle);
  void updateSearchCount();
  void updateProgress(const ghostty_action_progress_report_s& progress);
  void commandFinished(const ghostty_action_command_finished_s& command);
  void updateScrollbar(const ghostty_action_scrollbar_s& scrollbar);
  void updateKeyState();
  void layoutOverlays();

  static ghostty_input_mods_e modifiers(Qt::KeyboardModifiers modifiers);
  static ghostty_input_mouse_button_e mouseButton(Qt::MouseButton button);

  GhosttyApp* m_app;
  ghostty_surface_t m_surface = nullptr;
  ghostty_config_t m_appliedConfig = nullptr;
  ghostty_surface_config_s m_config{};
  bool m_contextRealized = false;
  bool m_composing = false;
  QString m_title;
  QString m_workingDirectory;
  QByteArray m_workingDirectoryUtf8;
  QByteArray m_commandUtf8;
  QByteArray m_initialInputUtf8;
  QSize m_cellSize;
  QFrame* m_searchFrame;
  QLineEdit* m_searchEdit;
  QLabel* m_searchCount;
  QLabel* m_statusOverlay;
  QLabel* m_readonlyBadge;
  QLabel* m_keyStateBadge;
  QProgressBar* m_progressBar;
  QTimer* m_progressTimer;
  QScrollBar* m_scrollBar;
  QPointer<InspectorWindow> m_inspectorWindow;
  QStringList m_keySequence;
  QStringList m_keyTables;
  uint64_t m_scrollMaximum = 0;
  qsizetype m_searchTotal = -1;
  qsizetype m_searchSelected = -1;
};
