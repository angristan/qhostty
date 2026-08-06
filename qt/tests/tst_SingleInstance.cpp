#include "SingleInstance.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QUuid>
#include <QtTest>

#include <future>

class SingleInstanceTest final : public QObject {
  Q_OBJECT

 private slots:
  void forwardsActivationRequests();
};

void SingleInstanceTest::forwardsActivationRequests() {
  const QString name = QStringLiteral("qhostty-test-") +
                       QUuid::createUuid().toString(QUuid::WithoutBraces);
  SingleInstance primary(name);
  QCOMPARE(primary.start({QStringLiteral("qhostty")}, QStringLiteral("/tmp")),
           SingleInstance::StartResult::Primary);
  QVERIFY(primary.isPrimary());

  QSignalSpy activations(&primary, &SingleInstance::activationRequested);
  const QStringList arguments{QStringLiteral("qhostty"),
                              QStringLiteral("--working-directory=/var"),
                              QStringLiteral("-e"), QStringLiteral("printf"),
                              QStringLiteral("hello world")};
  auto secondary = std::async(std::launch::async, [name, arguments]() {
    SingleInstance instance(name);
    return instance.start(arguments, QStringLiteral("/home"), 3000);
  });

  QTRY_COMPARE_WITH_TIMEOUT(activations.size(), 1, 3000);
  QCOMPARE(secondary.get(), SingleInstance::StartResult::Forwarded);
  const QList<QVariant> activation = activations.takeFirst();
  QCOMPARE(activation.at(0).toStringList(), arguments);
  QCOMPARE(activation.at(1).toString(), QStringLiteral("/home"));
}

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  SingleInstanceTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "tst_SingleInstance.moc"
