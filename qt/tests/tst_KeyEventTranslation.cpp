#include "KeyEventTranslation.h"

#include <QKeyEvent>
#include <QtTest>

class KeyEventTranslationTest final : public QObject {
  Q_OBJECT

 private slots:
  void normalizesControlLetters();
  void normalizesControlSpace();
  void preservesOrdinaryAndSpecialText();
  void filtersAutoRepeatReleases();
  void mapsLogicalRemappedKeys();
};

void KeyEventTranslationTest::normalizesControlLetters() {
  const QKeyEvent ctrlD(QEvent::KeyPress, Qt::Key_D, Qt::ControlModifier,
                        QString(QChar(0x04)));
  const uint32_t ctrlDUnshifted = ghosttyUnshiftedCodepoint(ctrlD);
  QCOMPARE(ctrlDUnshifted, static_cast<uint32_t>('d'));
  QCOMPARE(ghosttyKeyText(ctrlD, ctrlDUnshifted), QByteArray("d"));

  const QKeyEvent ctrlShiftD(QEvent::KeyPress, Qt::Key_D,
                             Qt::ControlModifier | Qt::ShiftModifier,
                             QString(QChar(0x04)));
  const uint32_t ctrlShiftDUnshifted = ghosttyUnshiftedCodepoint(ctrlShiftD);
  QCOMPARE(ghosttyKeyText(ctrlShiftD, ctrlShiftDUnshifted), QByteArray("D"));
}

void KeyEventTranslationTest::normalizesControlSpace() {
  const QKeyEvent ctrlSpace(QEvent::KeyPress, Qt::Key_Space,
                            Qt::ControlModifier, QString(QChar(0x00)));
  const uint32_t unshifted = ghosttyUnshiftedCodepoint(ctrlSpace);
  QCOMPARE(unshifted, static_cast<uint32_t>(' '));
  QCOMPARE(ghosttyKeyText(ctrlSpace, unshifted), QByteArray(" "));
}

void KeyEventTranslationTest::filtersAutoRepeatReleases() {
  const QKeyEvent repeatedRelease(QEvent::KeyRelease, Qt::Key_D, Qt::NoModifier,
                                  QStringLiteral("d"), true);
  QVERIFY(!ghosttyShouldSendKeyRelease(repeatedRelease));

  const QKeyEvent realRelease(QEvent::KeyRelease, Qt::Key_D, Qt::NoModifier,
                              QStringLiteral("d"), false);
  QVERIFY(ghosttyShouldSendKeyRelease(realRelease));
}

void KeyEventTranslationTest::mapsLogicalRemappedKeys() {
  const QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCOMPARE(ghosttyLogicalKey(escape), GHOSTTY_KEY_ESCAPE);

  const QKeyEvent capsLock(QEvent::KeyPress, Qt::Key_CapsLock, Qt::NoModifier);
  QCOMPARE(ghosttyLogicalKey(capsLock), GHOSTTY_KEY_CAPS_LOCK);

  const QKeyEvent letter(QEvent::KeyPress, Qt::Key_D, Qt::NoModifier,
                         QStringLiteral("d"));
  QCOMPARE(ghosttyLogicalKey(letter), GHOSTTY_KEY_D);
}

void KeyEventTranslationTest::preservesOrdinaryAndSpecialText() {
  const QKeyEvent shiftedD(QEvent::KeyPress, Qt::Key_D, Qt::ShiftModifier,
                           QStringLiteral("D"));
  QCOMPARE(ghosttyKeyText(shiftedD, ghosttyUnshiftedCodepoint(shiftedD)),
           QByteArray("D"));

  const QKeyEvent ctrlEnter(QEvent::KeyPress, Qt::Key_Return,
                            Qt::ControlModifier, QString(QChar(0x0d)));
  QCOMPARE(ghosttyUnshiftedCodepoint(ctrlEnter), uint32_t(0));
  QCOMPARE(ghosttyKeyText(ctrlEnter, 0), QByteArray(1, '\r'));
}

QTEST_APPLESS_MAIN(KeyEventTranslationTest)

#include "tst_KeyEventTranslation.moc"
