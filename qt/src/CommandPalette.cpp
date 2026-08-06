#include "CommandPalette.h"

#include "TerminalWidget.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <cstring>

namespace {
QString copied(const char* value) {
  return value == nullptr ? QString() : QString::fromUtf8(value);
}

bool supportedCommand(const QString& actionKey) {
  return actionKey != QStringLiteral("toggle_secure_input") &&
         actionKey != QStringLiteral("toggle_background_opacity") &&
         actionKey != QStringLiteral("undo") &&
         actionKey != QStringLiteral("redo") &&
         actionKey != QStringLiteral("check_for_updates") &&
         actionKey != QStringLiteral("show_gtk_inspector");
}
}  // namespace

CommandPalette::CommandPalette(ghostty_config_t config,
                               TerminalWidget* source,
                               const QList<TerminalWidget*>& terminals,
                               QWidget* parent)
    : QDialog(parent),
      m_source(source),
      m_search(new QLineEdit(this)),
      m_list(new QListWidget(this)),
      m_description(new QLabel(this)) {
  setAttribute(Qt::WA_DeleteOnClose, true);
  setWindowTitle(tr("Command Palette"));
  setModal(false);
  resize(620, 480);

  auto* layout = new QVBoxLayout(this);
  m_search->setPlaceholderText(tr("Search commands"));
  m_description->setWordWrap(true);
  m_description->setTextInteractionFlags(Qt::TextSelectableByMouse);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  auto* run = buttons->addButton(tr("Run"), QDialogButtonBox::AcceptRole);
  layout->addWidget(m_search);
  layout->addWidget(m_list, 1);
  layout->addWidget(m_description);
  layout->addWidget(buttons);

  load(config, terminals);
  filter({});
  connect(m_search, &QLineEdit::textChanged, this, &CommandPalette::filter);
  connect(m_list, &QListWidget::currentItemChanged, this,
          [this](QListWidgetItem* current) { select(current); });
  connect(m_list, &QListWidget::itemActivated, this, &CommandPalette::execute);
  connect(run, &QPushButton::clicked, this,
          [this]() { execute(m_list->currentItem()); });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  m_search->setFocus(Qt::ShortcutFocusReason);
}

int CommandPalette::entryCount() const {
  return static_cast<int>(m_entries.size());
}

void CommandPalette::load(ghostty_config_t config,
                          const QList<TerminalWidget*>& terminals) {
  ghostty_config_command_list_s list{};
  if (config != nullptr &&
      ghostty_config_get(config, &list, "command-palette-entry",
                         std::strlen("command-palette-entry"))) {
    m_entries.reserve(static_cast<qsizetype>(list.len) + terminals.size());
    for (size_t index = 0; index < list.len; ++index) {
      const ghostty_command_s& command = list.commands[index];
      Entry entry{copied(command.action_key), copied(command.action),
                  copied(command.title), copied(command.description), nullptr};
      if (supportedCommand(entry.actionKey) && !entry.action.isEmpty()) {
        m_entries.append(std::move(entry));
      }
    }
  }

  int surfaceNumber = 1;
  for (TerminalWidget* terminal : terminals) {
    if (terminal == nullptr) {
      continue;
    }
    Entry entry;
    entry.actionKey = QStringLiteral("qhostty_focus_surface");
    entry.title = terminal->title().isEmpty()
                      ? tr("Focus terminal %1").arg(surfaceNumber)
                      : tr("Focus %1").arg(terminal->title());
    entry.description = tr("Move keyboard focus to this terminal surface.");
    entry.focusTarget = terminal;
    m_entries.append(std::move(entry));
    ++surfaceNumber;
  }
}

void CommandPalette::filter(const QString& text) {
  m_list->clear();
  for (qsizetype index = 0; index < m_entries.size(); ++index) {
    const Entry& entry = m_entries.at(index);
    const bool matches = text.isEmpty() ||
                         entry.actionKey.contains(text, Qt::CaseInsensitive) ||
                         entry.title.contains(text, Qt::CaseInsensitive) ||
                         entry.description.contains(text, Qt::CaseInsensitive);
    if (!matches) {
      continue;
    }
    auto* item = new QListWidgetItem(
        entry.title.isEmpty() ? entry.actionKey : entry.title, m_list);
    item->setData(Qt::UserRole, index);
    item->setToolTip(entry.action);
  }
  if (m_list->count() > 0) {
    m_list->setCurrentRow(0);
  } else {
    m_description->clear();
  }
}

void CommandPalette::select(QListWidgetItem* item) {
  if (item == nullptr) {
    m_description->clear();
    return;
  }
  const qsizetype index = item->data(Qt::UserRole).toLongLong();
  m_description->setText(index >= 0 && index < m_entries.size()
                             ? m_entries.at(index).description
                             : QString());
}

void CommandPalette::execute(QListWidgetItem* item) {
  if (item == nullptr) {
    return;
  }
  const qsizetype index = item->data(Qt::UserRole).toLongLong();
  if (index < 0 || index >= m_entries.size()) {
    return;
  }
  const Entry entry = m_entries.at(index);
  accept();
  if (entry.focusTarget != nullptr) {
    entry.focusTarget->setFocus(Qt::ShortcutFocusReason);
  } else if (m_source != nullptr) {
    QPointer<TerminalWidget> source = m_source;
    QTimer::singleShot(0, source, [source, action = entry.action]() {
      if (source != nullptr) {
        source->runBindingAction(action);
      }
    });
  }
}
