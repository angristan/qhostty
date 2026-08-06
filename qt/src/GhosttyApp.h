#pragma once

#include <QObject>
#include <QSet>

#include <atomic>

#include <ghostty.h>

class TerminalWidget;

class GhosttyApp final : public QObject {
  Q_OBJECT

 public:
  explicit GhosttyApp(QObject* parent = nullptr);
  ~GhosttyApp() override;

  bool initialize();
  bool reloadConfig(bool soft = false);

  [[nodiscard]] ghostty_app_t handle() const { return m_app; }
  [[nodiscard]] ghostty_config_t config() const { return m_config; }

  void registerSurface(TerminalWidget* widget);
  void unregisterSurface(TerminalWidget* widget);

 signals:
  void newWindowRequested();
  void quitRequested();

 private:
  static void wakeupCallback(void* userdata);
  static bool actionCallback(ghostty_app_t app,
                             ghostty_target_s target,
                             ghostty_action_s action);
  static bool readClipboardCallback(void* userdata,
                                    ghostty_clipboard_e location,
                                    void* state);
  static void confirmReadClipboardCallback(void* userdata,
                                           const char* contents,
                                           void* state,
                                           ghostty_clipboard_request_e request);
  static void writeClipboardCallback(
      void* userdata,
      ghostty_clipboard_e location,
      const ghostty_clipboard_content_s* contents,
      size_t count,
      bool confirm);
  static void closeSurfaceCallback(void* userdata, bool processAlive);

  bool handleAction(ghostty_target_s target, ghostty_action_s action);
  void scheduleTick();
  void tick();

  static TerminalWidget* widgetForTarget(ghostty_target_s target);

  ghostty_config_t m_config = nullptr;
  ghostty_app_t m_app = nullptr;
  QSet<TerminalWidget*> m_surfaces;
  std::atomic_bool m_tickQueued = false;
};
