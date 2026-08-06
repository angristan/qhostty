#include "GlobalShortcuts.h"

#include "GhosttyApp.h"

#include <QAction>
#include <QCryptographicHash>
#include <QDebug>
#include <QKeySequence>

#ifdef QHOSTTY_HAS_GLOBAL_ACCEL
#include <KGlobalAccel>
#endif

namespace {
Qt::Key physicalKey(ghostty_input_key_e key) {
  if (key >= GHOSTTY_KEY_DIGIT_0 && key <= GHOSTTY_KEY_DIGIT_9) {
    return static_cast<Qt::Key>(static_cast<int>(Qt::Key_0) +
                                static_cast<int>(key) -
                                static_cast<int>(GHOSTTY_KEY_DIGIT_0));
  }
  if (key >= GHOSTTY_KEY_A && key <= GHOSTTY_KEY_Z) {
    return static_cast<Qt::Key>(static_cast<int>(Qt::Key_A) +
                                static_cast<int>(key) -
                                static_cast<int>(GHOSTTY_KEY_A));
  }
  if (key >= GHOSTTY_KEY_F1 && key <= GHOSTTY_KEY_F25) {
    return static_cast<Qt::Key>(static_cast<int>(Qt::Key_F1) +
                                static_cast<int>(key) -
                                static_cast<int>(GHOSTTY_KEY_F1));
  }

  switch (key) {
    case GHOSTTY_KEY_BACKQUOTE:
      return Qt::Key_QuoteLeft;
    case GHOSTTY_KEY_BACKSLASH:
      return Qt::Key_Backslash;
    case GHOSTTY_KEY_BRACKET_LEFT:
      return Qt::Key_BracketLeft;
    case GHOSTTY_KEY_BRACKET_RIGHT:
      return Qt::Key_BracketRight;
    case GHOSTTY_KEY_COMMA:
      return Qt::Key_Comma;
    case GHOSTTY_KEY_EQUAL:
      return Qt::Key_Equal;
    case GHOSTTY_KEY_MINUS:
      return Qt::Key_Minus;
    case GHOSTTY_KEY_PERIOD:
      return Qt::Key_Period;
    case GHOSTTY_KEY_QUOTE:
      return Qt::Key_Apostrophe;
    case GHOSTTY_KEY_SEMICOLON:
      return Qt::Key_Semicolon;
    case GHOSTTY_KEY_SLASH:
      return Qt::Key_Slash;
    case GHOSTTY_KEY_BACKSPACE:
      return Qt::Key_Backspace;
    case GHOSTTY_KEY_ENTER:
      return Qt::Key_Return;
    case GHOSTTY_KEY_SPACE:
      return Qt::Key_Space;
    case GHOSTTY_KEY_TAB:
      return Qt::Key_Tab;
    case GHOSTTY_KEY_DELETE:
      return Qt::Key_Delete;
    case GHOSTTY_KEY_END:
      return Qt::Key_End;
    case GHOSTTY_KEY_HOME:
      return Qt::Key_Home;
    case GHOSTTY_KEY_INSERT:
      return Qt::Key_Insert;
    case GHOSTTY_KEY_PAGE_DOWN:
      return Qt::Key_PageDown;
    case GHOSTTY_KEY_PAGE_UP:
      return Qt::Key_PageUp;
    case GHOSTTY_KEY_ARROW_DOWN:
      return Qt::Key_Down;
    case GHOSTTY_KEY_ARROW_LEFT:
      return Qt::Key_Left;
    case GHOSTTY_KEY_ARROW_RIGHT:
      return Qt::Key_Right;
    case GHOSTTY_KEY_ARROW_UP:
      return Qt::Key_Up;
    case GHOSTTY_KEY_ESCAPE:
      return Qt::Key_Escape;
    case GHOSTTY_KEY_PRINT_SCREEN:
      return Qt::Key_Print;
    case GHOSTTY_KEY_SCROLL_LOCK:
      return Qt::Key_ScrollLock;
    case GHOSTTY_KEY_PAUSE:
      return Qt::Key_Pause;
    default:
      return Qt::Key_unknown;
  }
}
}  // namespace

QKeySequence GlobalShortcuts::sequenceForTrigger(
    ghostty_input_trigger_s trigger) {
  Qt::KeyboardModifiers modifiers;
  if (trigger.mods & GHOSTTY_MODS_SHIFT) {
    modifiers |= Qt::ShiftModifier;
  }
  if (trigger.mods & GHOSTTY_MODS_CTRL) {
    modifiers |= Qt::ControlModifier;
  }
  if (trigger.mods & GHOSTTY_MODS_ALT) {
    modifiers |= Qt::AltModifier;
  }
  if (trigger.mods & GHOSTTY_MODS_SUPER) {
    modifiers |= Qt::MetaModifier;
  }

  Qt::Key key = Qt::Key_unknown;
  if (trigger.tag == GHOSTTY_TRIGGER_PHYSICAL) {
    key = physicalKey(trigger.key.physical);
  } else if (trigger.tag == GHOSTTY_TRIGGER_UNICODE &&
             trigger.key.unicode >= 0x20 && trigger.key.unicode <= 0x00ffffff) {
    uint32_t codepoint = trigger.key.unicode;
    if (codepoint >= 'a' && codepoint <= 'z') {
      codepoint = codepoint - 'a' + 'A';
    }
    key = static_cast<Qt::Key>(codepoint);
  }
  return key == Qt::Key_unknown ? QKeySequence()
                                : QKeySequence(QKeyCombination(modifiers, key));
}

GlobalShortcuts::GlobalShortcuts(GhosttyApp* app, QObject* parent)
    : QObject(parent), m_app(app) {}

GlobalShortcuts::~GlobalShortcuts() {
  clear();
}

void GlobalShortcuts::refresh(ghostty_config_t config) {
  clear();
  if (config == nullptr ||
      qEnvironmentVariableIntValue("QHOSTTY_DISABLE_GLOBAL_SHORTCUTS") != 0) {
    return;
  }
  ghostty_config_enumerate_global_keybinds(config, this,
                                           &GlobalShortcuts::enumerate);
  if (!supported() && count() > 0) {
    qWarning() << "Global shortcuts require KDE Frameworks GlobalAccel";
  }
}

bool GlobalShortcuts::supported() const {
#ifdef QHOSTTY_HAS_GLOBAL_ACCEL
  return true;
#else
  return false;
#endif
}

bool GlobalShortcuts::enumerate(void* userdata,
                                ghostty_input_trigger_s trigger,
                                const char* action,
                                uintptr_t actionLength) {
  auto* self = static_cast<GlobalShortcuts*>(userdata);
  return self != nullptr &&
         self->add(trigger, QString::fromUtf8(
                                action, static_cast<qsizetype>(actionLength)));
}

bool GlobalShortcuts::add(ghostty_input_trigger_s trigger,
                          const QString& action) {
  const QKeySequence sequence = sequenceForTrigger(trigger);
  if (sequence.isEmpty() || action.isEmpty()) {
    return true;
  }

#ifdef QHOSTTY_HAS_GLOBAL_ACCEL
  auto* shortcut = new QAction(action, this);
  QByteArray identity = action.toUtf8();
  identity.append(':');
  identity.append(QByteArray::number(static_cast<int>(trigger.tag)));
  identity.append(':');
  identity.append(QByteArray::number(static_cast<int>(trigger.mods)));
  identity.append(':');
  identity.append(
      QByteArray::number(trigger.tag == GHOSTTY_TRIGGER_PHYSICAL
                             ? static_cast<uint32_t>(trigger.key.physical)
                             : trigger.key.unicode));
  const QByteArray digest =
      QCryptographicHash::hash(identity, QCryptographicHash::Sha256);
  shortcut->setObjectName(QStringLiteral("ghostty-") +
                          QString::fromLatin1(digest.toHex().left(20)));
  shortcut->setText(action);
  connect(shortcut, &QAction::triggered, this,
          [this, action]() { m_app->performBindingAction(action); });
  KGlobalAccel* globalAccel = KGlobalAccel::self();
  const QList<QKeySequence> sequences{sequence};
  const bool defaultSet = globalAccel->setDefaultShortcut(
      shortcut, sequences, KGlobalAccel::NoAutoloading);
  const bool activeSet = globalAccel->setShortcut(shortcut, sequences,
                                                  KGlobalAccel::NoAutoloading);
  if (!defaultSet || !activeSet) {
    globalAccel->removeAllShortcuts(shortcut);
    shortcut->deleteLater();
    return true;
  }
  m_actions.append(shortcut);
#else
  Q_UNUSED(trigger);
  Q_UNUSED(action);
#endif
  return true;
}

void GlobalShortcuts::clear() {
  for (QAction* action : std::as_const(m_actions)) {
#ifdef QHOSTTY_HAS_GLOBAL_ACCEL
    KGlobalAccel::self()->removeAllShortcuts(action);
#endif
    delete action;
  }
  m_actions.clear();
}
