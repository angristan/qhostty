#include "CommandPalette.h"
#include "GhosttyApp.h"
#include "InspectorWindow.h"
#include "MainWindow.h"
#include "QuickTerminal.h"
#include "TerminalTab.h"
#include "TerminalWidget.h"

#include <QApplication>
#include <QLabel>
#include <QProgressBar>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabBar>
#include <QTemporaryFile>
#include <QtTest>

#include <cstdlib>
#include <cstring>

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
  void routesTerminalStateActions();
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

void WindowActionsTest::routesTerminalStateActions() {
  GhosttyApp ghostty;
  QVERIFY(ghostty.initialize());
  MainWindow window(&ghostty);
  TerminalWidget* terminal = window.currentTab()->activeTerminal();
  QVERIFY(terminal != nullptr);

  ghostty_action_s action{};
  ghostty_config_t previousConfig = terminal->config();
  action.tag = GHOSTTY_ACTION_CONFIG_CHANGE;
  action.action.config_change.config = ghostty.config();
  QVERIFY(terminal->handleAction(action));
  QVERIFY(terminal->config() != nullptr);
  QVERIFY(terminal->config() != previousConfig);

  action = {};
  action.tag = GHOSTTY_ACTION_READONLY;
  action.action.readonly = GHOSTTY_READONLY_ON;
  QVERIFY(terminal->handleAction(action));
  auto* readonly =
      terminal->findChild<QLabel*>(QStringLiteral("qhostty-readonly"));
  QVERIFY(readonly != nullptr);
  QVERIFY(!readonly->isHidden());

  action = {};
  action.tag = GHOSTTY_ACTION_SCROLLBAR;
  action.action.scrollbar = {100, 20, 10};
  QVERIFY(terminal->handleAction(action));
  auto* scrollbar =
      terminal->findChild<QScrollBar*>(QStringLiteral("qhostty-scrollbar"));
  QVERIFY(scrollbar != nullptr);
  QVERIFY(!scrollbar->isHidden());
  QCOMPARE(scrollbar->value(), 20);

  action = {};
  action.tag = GHOSTTY_ACTION_KEY_SEQUENCE;
  action.action.key_sequence.active = true;
  action.action.key_sequence.trigger.tag = GHOSTTY_TRIGGER_UNICODE;
  action.action.key_sequence.trigger.key.unicode = 'g';
  action.action.key_sequence.trigger.mods = GHOSTTY_MODS_CTRL;
  QVERIFY(terminal->handleAction(action));
  auto* keyState =
      terminal->findChild<QLabel*>(QStringLiteral("qhostty-key-state"));
  QVERIFY(keyState != nullptr);
  QVERIFY(!keyState->isHidden());
  QVERIFY(keyState->text().contains(QStringLiteral("Ctrl+g")));

  action = {};
  action.tag = GHOSTTY_ACTION_PROGRESS_REPORT;
  action.action.progress_report.state = GHOSTTY_PROGRESS_STATE_SET;
  action.action.progress_report.progress = 42;
  QVERIFY(terminal->handleAction(action));
  auto* progress =
      terminal->findChild<QProgressBar*>(QStringLiteral("qhostty-progress"));
  QVERIFY(progress != nullptr);
  bool progressEnabled = true;
  ghostty_config_get(ghostty.config(), &progressEnabled, "progress-style",
                     std::strlen("progress-style"));
  QCOMPARE(!progress->isHidden(), progressEnabled);
  if (progressEnabled) {
    QCOMPARE(progress->value(), 42);
  }

  action = {};
  action.tag = GHOSTTY_ACTION_INSPECTOR;
  action.action.inspector = GHOSTTY_INSPECTOR_SHOW;
  QVERIFY(terminal->handleAction(action));
  auto* inspector = window.findChild<InspectorWindow*>();
  QVERIFY(inspector != nullptr);
  action.action.inspector = GHOSTTY_INSPECTOR_HIDE;
  QVERIFY(terminal->handleAction(action));
  QVERIFY(inspector->isHidden());
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

  CommandPalette palette(source->config(), source, {source});
  QVERIFY(palette.entryCount() >= 1);

  ghostty_action_s action{};
  action.tag = GHOSTTY_ACTION_FLOAT_WINDOW;
  action.action.float_window = GHOSTTY_FLOAT_WINDOW_ON;
  QVERIFY(window.handleAction(source, action));
  QVERIFY(window.isHidden());

  action = {};
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
