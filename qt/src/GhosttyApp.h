#pragma once

#include <QList>
#include <QObject>
#include <QSet>

#include <atomic>

#include <ghostty.h>

class MainWindow;
class TerminalWidget;

class GhosttyApp final : public QObject {
  Q_OBJECT

 public:
  explicit GhosttyApp(QObject* parent = nullptr);
  ~GhosttyApp() override;

  bool initialize();
  bool reloadConfig(bool soft = false);
  bool reloadConfig(TerminalWidget* widget, bool soft = false);

  [[nodiscard]] ghostty_app_t handle() const { return m_app; }
  [[nodiscard]] ghostty_config_t config() const { return m_config; }

  MainWindow* createWindow(
      const ghostty_surface_config_s* baseConfig = nullptr);
  void registerWindow(MainWindow* window);
  void unregisterWindow(MainWindow* window);
  void registerSurface(TerminalWidget* widget);
  void unregisterSurface(TerminalWidget* widget);

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
  void syncColorScheme();

  static TerminalWidget* widgetForTarget(ghostty_target_s target);

  ghostty_config_t m_config = nullptr;
  ghostty_app_t m_app = nullptr;
  QList<MainWindow*> m_windows;
  QSet<TerminalWidget*> m_surfaces;
  std::atomic_bool m_tickQueued = false;
};
