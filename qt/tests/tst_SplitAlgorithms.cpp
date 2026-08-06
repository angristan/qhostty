#include "SplitAlgorithms.h"

#include <QtTest>

class SplitAlgorithmsTest final : public QObject {
  Q_OBJECT

 private slots:
  void choosesNearestDirectionalPane();
  void rejectsMissingDirection();
  void resizesFromEitherEdge();
  void clampsAtMinimumSize();
};

void SplitAlgorithmsTest::choosesNearestDirectionalPane() {
  const QList<QPoint> centers{{0, 0}, {100, 10}, {200, 0}, {100, 100}};
  QCOMPARE(qhostty::directionalNeighbor(centers, 0, GHOSTTY_GOTO_SPLIT_RIGHT),
           1);
  QCOMPARE(qhostty::directionalNeighbor(centers, 1, GHOSTTY_GOTO_SPLIT_DOWN),
           3);
  QCOMPARE(qhostty::directionalNeighbor(centers, 2, GHOSTTY_GOTO_SPLIT_LEFT),
           1);
}

void SplitAlgorithmsTest::rejectsMissingDirection() {
  const QList<QPoint> centers{{0, 0}, {100, 0}};
  QCOMPARE(qhostty::directionalNeighbor(centers, 0, GHOSTTY_GOTO_SPLIT_LEFT),
           -1);
  QCOMPARE(qhostty::directionalNeighbor(centers, -1, GHOSTTY_GOTO_SPLIT_RIGHT),
           -1);
}

void SplitAlgorithmsTest::resizesFromEitherEdge() {
  QList<int> left{50, 50};
  QVERIFY(qhostty::resizeAdjacent(left, 0, true, 10));
  QCOMPARE(left, QList<int>({60, 40}));
  QVERIFY(qhostty::resizeAdjacent(left, 0, false, 5));
  QCOMPARE(left, QList<int>({55, 45}));

  QList<int> right{50, 50};
  QVERIFY(qhostty::resizeAdjacent(right, 1, false, 10));
  QCOMPARE(right, QList<int>({40, 60}));
  QVERIFY(qhostty::resizeAdjacent(right, 1, true, 5));
  QCOMPARE(right, QList<int>({45, 55}));
}

void SplitAlgorithmsTest::clampsAtMinimumSize() {
  QList<int> sizes{2, 10};
  QVERIFY(qhostty::resizeAdjacent(sizes, 0, false, 50));
  QCOMPARE(sizes, QList<int>({1, 11}));
  QVERIFY(!qhostty::resizeAdjacent(sizes, 0, false, 1));
}

QTEST_APPLESS_MAIN(SplitAlgorithmsTest)
#include "tst_SplitAlgorithms.moc"
