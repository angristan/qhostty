#include "GhosttyApp.h"
#include "MainWindow.h"
#include "TerminalTab.h"
#include "TerminalWidget.h"

#include <QApplication>
#include <QStackedWidget>
#include <QTabBar>
#include <QtTest>

#include <cstdlib>

class WindowActionsTest final : public QObject {
  Q_OBJECT

 private slots:
  void routesTabAndSplitActions();
};

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
