#pragma once

#include <QKeySequence>
#include <QList>
#include <QObject>

#include <ghostty.h>

class QAction;
class GhosttyApp;

class GlobalShortcuts final : public QObject {
  Q_OBJECT

 public:
  explicit GlobalShortcuts(GhosttyApp* app, QObject* parent = nullptr);
  ~GlobalShortcuts() override;

  void refresh(ghostty_config_t config);
  [[nodiscard]] bool supported() const;
  [[nodiscard]] int count() const { return static_cast<int>(m_actions.size()); }
  [[nodiscard]] static QKeySequence sequenceForTrigger(
      ghostty_input_trigger_s trigger);

 private:
  static bool enumerate(void* userdata,
                        ghostty_input_trigger_s trigger,
                        const char* action,
                        uintptr_t actionLength);
  bool add(ghostty_input_trigger_s trigger, const QString& action);
  void clear();

  GhosttyApp* m_app;
  QList<QAction*> m_actions;
};
