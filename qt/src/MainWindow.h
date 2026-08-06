#pragma once

#include <QMainWindow>
#include <QPointer>

#include <ghostty.h>

#include <optional>

class CommandPalette;
class GhosttyApp;
class QCloseEvent;
class QEvent;
class QStackedWidget;
class QTabBar;
class TerminalTab;
class TerminalWidget;

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  enum class Role {
    Normal,
    QuickTerminal,
  };

  explicit MainWindow(GhosttyApp* app,
                      QWidget* parent = nullptr,
                      const ghostty_surface_config_s* baseConfig = nullptr,
                      bool createInitialTab = true,
                      Role role = Role::Normal);
  ~MainWindow() override;

  int addTab(const ghostty_surface_config_s* baseConfig = nullptr);
  int adoptTab(TerminalTab* tab, const QString& title);
  [[nodiscard]] std::optional<bool> handleAction(
      TerminalWidget* source,
      const ghostty_action_s& action);
  [[nodiscard]] TerminalTab* currentTab() const;
  [[nodiscard]] bool canClose() const;
  [[nodiscard]] bool isQuickTerminal() const {
    return m_role == Role::QuickTerminal;
  }
  void setQuickTerminalAutohide(bool enabled) {
    m_quickTerminalAutohide = enabled;
  }
  void closeConfirmed();

 protected:
  void changeEvent(QEvent* event) override;
  void closeEvent(QCloseEvent* event) override;

 private:
  TerminalTab* tabFor(TerminalWidget* terminal) const;
  void closeTab(int index);
  void detachTab(int index);
  void connectTab(TerminalTab* tab);
  void updateTabTitle(TerminalTab* tab, const QString& title);
  bool selectTab(int value);
  void toggleCommandPalette(TerminalWidget* source);

  GhosttyApp* m_app;
  QTabBar* m_tabBar;
  QStackedWidget* m_stack;
  Role m_role;
  QPointer<CommandPalette> m_commandPalette;
  bool m_quickTerminalAutohide = false;
};
