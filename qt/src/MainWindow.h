#pragma once

#include <QMainWindow>

#include <ghostty.h>

class GhosttyApp;
class QCloseEvent;
class QStackedWidget;
class QTabBar;
class TerminalTab;
class TerminalWidget;

class MainWindow final : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(GhosttyApp* app,
                      QWidget* parent = nullptr,
                      const ghostty_surface_config_s* baseConfig = nullptr,
                      bool createInitialTab = true);
  ~MainWindow() override;

  int addTab(const ghostty_surface_config_s* baseConfig = nullptr);
  int adoptTab(TerminalTab* tab, const QString& title);
  bool handleAction(TerminalWidget* source, const ghostty_action_s& action);
  [[nodiscard]] TerminalTab* currentTab() const;
  [[nodiscard]] bool canClose() const;
  void closeConfirmed();

 protected:
  void closeEvent(QCloseEvent* event) override;

 private:
  TerminalTab* tabFor(TerminalWidget* terminal) const;
  void closeTab(int index);
  void detachTab(int index);
  void connectTab(TerminalTab* tab);
  void updateTabTitle(TerminalTab* tab, const QString& title);
  bool selectTab(int value);

  GhosttyApp* m_app;
  QTabBar* m_tabBar;
  QStackedWidget* m_stack;
};
