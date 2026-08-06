#pragma once

#include <QDialog>
#include <QList>
#include <QPointer>

#include <ghostty.h>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class TerminalWidget;

class CommandPalette final : public QDialog {
  Q_OBJECT

 public:
  explicit CommandPalette(ghostty_config_t config,
                          TerminalWidget* source,
                          const QList<TerminalWidget*>& terminals,
                          QWidget* parent = nullptr);

  [[nodiscard]] int entryCount() const;

 private:
  struct Entry {
    QString actionKey;
    QString action;
    QString title;
    QString description;
    QPointer<TerminalWidget> focusTarget;
  };

  void load(ghostty_config_t config, const QList<TerminalWidget*>& terminals);
  void filter(const QString& text);
  void select(QListWidgetItem* item);
  void execute(QListWidgetItem* item);

  QPointer<TerminalWidget> m_source;
  QList<Entry> m_entries;
  QLineEdit* m_search;
  QListWidget* m_list;
  QLabel* m_description;
};
