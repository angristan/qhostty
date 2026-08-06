#include "GhosttyApp.h"

#include "MainWindow.h"
#include "TerminalWidget.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QMetaObject>
#include <QUrl>

GhosttyApp::GhosttyApp(QObject* parent) : QObject(parent) {}

GhosttyApp::~GhosttyApp() {
  while (!m_windows.isEmpty()) {
    delete *m_windows.begin();
  }
  if (!m_surfaces.isEmpty()) {
    qWarning() << "Destroying GhosttyApp with" << m_surfaces.size()
               << "live surfaces";
  }

  if (m_app != nullptr) {
    ghostty_app_free(m_app);
    m_app = nullptr;
  }
  if (m_config != nullptr) {
    ghostty_config_free(m_config);
    m_config = nullptr;
  }
}

bool GhosttyApp::initialize() {
  m_config = ghostty_config_new();
  if (m_config == nullptr) {
    qCritical() << "Failed to create Ghostty configuration";
    return false;
  }

  ghostty_config_load_default_files(m_config);
  ghostty_config_load_cli_args(m_config);
  ghostty_config_load_recursive_files(m_config);
  ghostty_config_finalize(m_config);

  const uint32_t diagnosticCount = ghostty_config_diagnostics_count(m_config);
  for (uint32_t index = 0; index < diagnosticCount; ++index) {
    const ghostty_diagnostic_s diagnostic =
        ghostty_config_get_diagnostic(m_config, index);
    qWarning().noquote() << "Ghostty config:" << diagnostic.message;
  }

  ghostty_runtime_config_s runtime{};
  runtime.userdata = this;
  runtime.supports_selection_clipboard = true;
  runtime.wakeup_cb = &GhosttyApp::wakeupCallback;
  runtime.action_cb = &GhosttyApp::actionCallback;
  runtime.read_clipboard_cb = &GhosttyApp::readClipboardCallback;
  runtime.confirm_read_clipboard_cb = &GhosttyApp::confirmReadClipboardCallback;
  runtime.write_clipboard_cb = &GhosttyApp::writeClipboardCallback;
  runtime.close_surface_cb = &GhosttyApp::closeSurfaceCallback;

  m_app = ghostty_app_new(&runtime, m_config);
  if (m_app == nullptr) {
    qCritical() << "Failed to create Ghostty application";
    return false;
  }

  ghostty_app_set_focus(m_app, true);
  return true;
}

bool GhosttyApp::reloadConfig(bool soft) {
  if (m_app == nullptr || m_config == nullptr) {
    return false;
  }

  if (soft) {
    ghostty_app_update_config(m_app, m_config);
    return true;
  }

  ghostty_config_t replacement = ghostty_config_new();
  if (replacement == nullptr) {
    return false;
  }

  ghostty_config_load_default_files(replacement);
  ghostty_config_load_cli_args(replacement);
  ghostty_config_load_recursive_files(replacement);
  ghostty_config_finalize(replacement);

  ghostty_app_update_config(m_app, replacement);
  ghostty_config_free(m_config);
  m_config = replacement;
  return true;
}

MainWindow* GhosttyApp::createWindow(
    const ghostty_surface_config_s* baseConfig) {
  auto* window = new MainWindow(this, nullptr, baseConfig);
  window->show();
  return window;
}

void GhosttyApp::registerWindow(MainWindow* window) {
  m_windows.insert(window);
}

void GhosttyApp::unregisterWindow(MainWindow* window) {
  m_windows.remove(window);
}

void GhosttyApp::registerSurface(TerminalWidget* widget) {
  m_surfaces.insert(widget);
}

void GhosttyApp::unregisterSurface(TerminalWidget* widget) {
  m_surfaces.remove(widget);
}

void GhosttyApp::wakeupCallback(void* userdata) {
  static_cast<GhosttyApp*>(userdata)->scheduleTick();
}

bool GhosttyApp::actionCallback(ghostty_app_t app,
                                ghostty_target_s target,
                                ghostty_action_s action) {
  auto* self = static_cast<GhosttyApp*>(ghostty_app_userdata(app));
  return self != nullptr && self->handleAction(target, action);
}

bool GhosttyApp::readClipboardCallback(void* userdata,
                                       ghostty_clipboard_e location,
                                       void* state) {
  auto* widget = static_cast<TerminalWidget*>(userdata);
  return widget != nullptr && widget->readClipboard(location, state);
}

void GhosttyApp::confirmReadClipboardCallback(
    void* userdata,
    const char* contents,
    void* state,
    ghostty_clipboard_request_e request) {
  auto* widget = static_cast<TerminalWidget*>(userdata);
  if (widget != nullptr) {
    widget->confirmReadClipboard(contents, state, request);
  }
}

void GhosttyApp::writeClipboardCallback(
    void* userdata,
    ghostty_clipboard_e location,
    const ghostty_clipboard_content_s* contents,
    size_t count,
    bool confirm) {
  auto* widget = static_cast<TerminalWidget*>(userdata);
  if (widget != nullptr) {
    widget->writeClipboard(location, contents, count, confirm);
  }
}

void GhosttyApp::closeSurfaceCallback(void* userdata, bool processAlive) {
  auto* widget = static_cast<TerminalWidget*>(userdata);
  if (widget == nullptr) {
    return;
  }

  QMetaObject::invokeMethod(
      widget, [widget, processAlive]() { widget->requestClose(processAlive); },
      Qt::QueuedConnection);
}

bool GhosttyApp::handleAction(ghostty_target_s target,
                              ghostty_action_s action) {
  TerminalWidget* widget = widgetForTarget(target);
  if (widget != nullptr && widget->handleAction(action)) {
    return true;
  }

  MainWindow* window =
      widget != nullptr
          ? qobject_cast<MainWindow*>(widget->window())
          : qobject_cast<MainWindow*>(QApplication::activeWindow());
  if (window != nullptr && window->handleAction(widget, action)) {
    return true;
  }

  switch (action.tag) {
    case GHOSTTY_ACTION_QUIT:
    case GHOSTTY_ACTION_CLOSE_ALL_WINDOWS:
      QCoreApplication::quit();
      return true;
    case GHOSTTY_ACTION_NEW_WINDOW: {
      ghostty_surface_config_s config =
          widget != nullptr
              ? widget->inheritedConfig(GHOSTTY_SURFACE_CONTEXT_WINDOW)
              : ghostty_surface_config_new();
      createWindow(&config);
      return true;
    }
    case GHOSTTY_ACTION_GOTO_WINDOW: {
      if (m_windows.size() < 2 || window == nullptr) {
        return false;
      }
      const QList<MainWindow*> windows = m_windows.values();
      int index = windows.indexOf(window);
      const int delta =
          action.action.goto_window == GHOSTTY_GOTO_WINDOW_PREVIOUS ? -1 : 1;
      index = (index + delta + windows.size()) % windows.size();
      windows.at(index)->show();
      windows.at(index)->raise();
      windows.at(index)->activateWindow();
      return true;
    }
    case GHOSTTY_ACTION_OPEN_CONFIG: {
      const ghostty_string_s path = ghostty_config_open_path();
      if (path.ptr != nullptr) {
        const QString value =
            QString::fromUtf8(path.ptr, static_cast<qsizetype>(path.len));
        QDesktopServices::openUrl(QUrl::fromLocalFile(value));
      }
      ghostty_string_free(path);
      return true;
    }
    case GHOSTTY_ACTION_RELOAD_CONFIG:
      return reloadConfig(action.action.reload_config.soft);
    case GHOSTTY_ACTION_QUIT_TIMER:
    case GHOSTTY_ACTION_CONFIG_CHANGE:
    case GHOSTTY_ACTION_CHECK_FOR_UPDATES:
      return true;
    default:
      return false;
  }
}

void GhosttyApp::scheduleTick() {
  if (m_tickQueued.exchange(true)) {
    return;
  }

  QMetaObject::invokeMethod(this, [this]() { tick(); }, Qt::QueuedConnection);
}

void GhosttyApp::tick() {
  m_tickQueued.store(false);
  if (m_app != nullptr) {
    ghostty_app_tick(m_app);
  }
}

TerminalWidget* GhosttyApp::widgetForTarget(ghostty_target_s target) {
  if (target.tag != GHOSTTY_TARGET_SURFACE ||
      target.target.surface == nullptr) {
    return nullptr;
  }

  return static_cast<TerminalWidget*>(
      ghostty_surface_userdata(target.target.surface));
}
