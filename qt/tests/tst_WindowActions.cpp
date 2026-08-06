#include "GhosttyApp.h"
#include "MainWindow.h"
#include "QuickTerminal.h"
#include "TerminalTab.h"
#include "TerminalWidget.h"

#include <QApplication>
#include <QStackedWidget>
#include <QTabBar>
#include <QTemporaryFile>
#include <QtTest>

#include <cstdlib>

namespace {
struct EnumeratedBinding {
  int count = 0;
  ghostty_input_trigger_s trigger{};
  QString action;
};

bool captureGlobalBinding(void* userdata,
                          ghostty_input_trigger_s trigger,
                          const char* action,
                          uintptr_t actionLength) {
  auto* result = static_cast<EnumeratedBinding*>(userdata);
  ++result->count;
  result->trigger = trigger;
  result->action =
      QString::fromUtf8(action, static_cast<qsizetype>(actionLength));
  return true;
}
}  // namespace

class WindowActionsTest final : public QObject {
  Q_OBJECT

 private slots:
  void routesTabAndSplitActions();
  void calculatesQuickTerminalGeometry();
  void enumeratesGlobalBindings();
};

void WindowActionsTest::enumeratesGlobalBindings() {
  QTemporaryFile file;
  QVERIFY(file.open());
  QCOMPARE(file.write("keybind = global:ctrl+shift+g=toggle_quick_terminal\n"),
           52);
  QVERIFY(file.flush());

  ghostty_config_t config = ghostty_config_new();
  QVERIFY(config != nullptr);
  const QByteArray path = file.fileName().toUtf8();
  ghostty_config_load_file(config, path.constData());
  ghostty_config_finalize(config);

  EnumeratedBinding binding;
  ghostty_config_enumerate_global_keybinds(config, &binding,
                                           &captureGlobalBinding);
  QCOMPARE(binding.count, 1);
  QCOMPARE(binding.action, QStringLiteral("toggle_quick_terminal"));
  QCOMPARE(binding.trigger.tag, GHOSTTY_TRIGGER_UNICODE);
  QCOMPARE(binding.trigger.key.unicode, static_cast<uint32_t>('g'));
  QVERIFY(binding.trigger.mods & GHOSTTY_MODS_CTRL);
  QVERIFY(binding.trigger.mods & GHOSTTY_MODS_SHIFT);
  ghostty_config_free(config);
}

void WindowActionsTest::calculatesQuickTerminalGeometry() {
  QuickTerminalSettings settings;
  const QRect landscape(100, 50, 1920, 1080);
  QCOMPARE(settings.geometry(landscape), QRect(100, 50, 1920, 400));

  settings.position = QuickTerminalSettings::Position::Left;
  QCOMPARE(settings.geometry(landscape), QRect(100, 50, 400, 1080));

  settings.position = QuickTerminalSettings::Position::Top;
  settings.size.primary.tag = GHOSTTY_QUICK_TERMINAL_SIZE_PERCENTAGE;
  settings.size.primary.value.percentage = 50;
  settings.size.secondary.tag = GHOSTTY_QUICK_TERMINAL_SIZE_PIXELS;
  settings.size.secondary.value.pixels = 800;
  QCOMPARE(settings.geometry(landscape), QRect(660, 50, 800, 540));

  settings = {};
  settings.position = QuickTerminalSettings::Position::Center;
  QCOMPARE(settings.geometry(QRect(0, 0, 1080, 1920)),
           QRect(340, 560, 400, 800));
}

void WindowActionsTest::routesTabAndSplitActions() {
  GhosttyApp ghostty;
  QVERIFY(ghostty.initialize());

  MainWindow window(&ghostty);
  auto* tabBar = window.findChild<QTabBar*>(QStringLiteral("qhostty-tabs"));
  auto* stack = window.findChild<QStackedWidget*>();
  QVERIFY(tabBar != nullptr);
  QVERIFY(stack != nullptr);
  QCOMPARE(tabBar->count(), 1);

  TerminalTab* firstTab = window.currentTab();
  QVERIFY(firstTab != nullptr);
  TerminalWidget* source = firstTab->activeTerminal();
  QVERIFY(source != nullptr);

  ghostty_action_s action{};
  action.tag = GHOSTTY_ACTION_NEW_TAB;
  QVERIFY(window.handleAction(source, action));
  QCOMPARE(tabBar->count(), 2);

  action = {};
  action.tag = GHOSTTY_ACTION_NEW_SPLIT;
  action.action.new_split = GHOSTTY_SPLIT_DIRECTION_RIGHT;
  QVERIFY(window.handleAction(source, action));
  QCOMPARE(firstTab->terminals().size(), 2);

  action = {};
  action.tag = GHOSTTY_ACTION_GOTO_SPLIT;
  action.action.goto_split = GHOSTTY_GOTO_SPLIT_NEXT;
  QVERIFY(window.handleAction(source, action));

  action = {};
  action.tag = GHOSTTY_ACTION_RESIZE_SPLIT;
  action.action.resize_split = {10, GHOSTTY_RESIZE_SPLIT_RIGHT};
  QVERIFY(window.handleAction(source, action));

  action = {};
  action.tag = GHOSTTY_ACTION_EQUALIZE_SPLITS;
  QVERIFY(window.handleAction(source, action));

  action = {};
  action.tag = GHOSTTY_ACTION_TOGGLE_SPLIT_ZOOM;
  QVERIFY(window.handleAction(source, action));
  QVERIFY(window.handleAction(source, action));

  action = {};
  action.tag = GHOSTTY_ACTION_MOVE_TAB;
  action.action.move_tab.amount = 1;
  QVERIFY(window.handleAction(source, action));
  QCOMPARE(stack->indexOf(firstTab), 1);
  QCOMPARE(tabBar->count(), 2);
}

int main(int argc, char** argv) {
  if (ghostty_init(static_cast<uintptr_t>(argc), argv) != 0) {
    return EXIT_FAILURE;
  }
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QApplication application(argc, argv);
  WindowActionsTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "tst_WindowActions.moc"
