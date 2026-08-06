#include "TerminalTab.h"

#include "GhosttyApp.h"
#include "TerminalWidget.h"

#include <QBoxLayout>
#include <QMessageBox>
#include <QSplitter>

#include <algorithm>
#include <limits>

TerminalTab::TerminalTab(GhosttyApp* app,
                         QWidget* parent,
                         const ghostty_surface_config_s* baseConfig)
    : QWidget(parent), m_app(app), m_layout(new QVBoxLayout(this)) {
  m_layout->setContentsMargins(0, 0, 0, 0);
  m_layout->setSpacing(0);

  auto* terminal = new TerminalWidget(app, this, baseConfig);
  m_layout->addWidget(terminal);
  connectTerminal(terminal);
  setActive(terminal);
}

QList<TerminalWidget*> TerminalTab::terminals() const {
  return findChildren<TerminalWidget*>({}, Qt::FindChildrenRecursively);
}

QString TerminalTab::title() const {
  if (!m_titleOverride.isEmpty()) {
    return m_titleOverride;
  }
  if (m_active != nullptr && !m_active->title().isEmpty()) {
    return m_active->title();
  }
  return tr("Terminal");
}

void TerminalTab::setTitleOverride(const QString& title) {
  m_titleOverride = title;
  emit titleChanged(this->title());
}

bool TerminalTab::present(TerminalWidget* terminal) {
  if (terminal == nullptr || !terminals().contains(terminal)) {
    return false;
  }
  setActive(terminal);
  terminal->setFocus(Qt::OtherFocusReason);
  return true;
}

TerminalWidget* TerminalTab::newSplit(
    TerminalWidget* source,
    ghostty_action_split_direction_e direction) {
  if (source == nullptr) {
    source = m_active;
  }
  if (source == nullptr) {
    return nullptr;
  }

  ghostty_surface_config_s config =
      source->inheritedConfig(GHOSTTY_SURFACE_CONTEXT_SPLIT);
  auto* terminal = new TerminalWidget(m_app, nullptr, &config);
  source->freeInheritedConfig(config);
  connectTerminal(terminal);

  const bool horizontal = direction == GHOSTTY_SPLIT_DIRECTION_LEFT ||
                          direction == GHOSTTY_SPLIT_DIRECTION_RIGHT;
  const bool before = direction == GHOSTTY_SPLIT_DIRECTION_LEFT ||
                      direction == GHOSTTY_SPLIT_DIRECTION_UP;
  const Qt::Orientation orientation =
      horizontal ? Qt::Horizontal : Qt::Vertical;

  if (auto* parentSplitter = qobject_cast<QSplitter*>(source->parentWidget());
      parentSplitter != nullptr &&
      parentSplitter->orientation() == orientation) {
    const int sourceIndex = parentSplitter->indexOf(source);
    parentSplitter->insertWidget(before ? sourceIndex : sourceIndex + 1,
                                 terminal);
  } else {
    auto* splitter = new QSplitter(orientation);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);

    if (auto* parentSplitter =
            qobject_cast<QSplitter*>(source->parentWidget())) {
      const int sourceIndex = parentSplitter->indexOf(source);
      parentSplitter->replaceWidget(sourceIndex, splitter);
    } else {
      m_layout->replaceWidget(source, splitter);
    }

    if (before) {
      splitter->addWidget(terminal);
      splitter->addWidget(source);
    } else {
      splitter->addWidget(source);
      splitter->addWidget(terminal);
    }
    splitter->setSizes({1, 1});
  }

  setActive(terminal);
  terminal->setFocus(Qt::ShortcutFocusReason);
  return terminal;
}

bool TerminalTab::closeSurface(TerminalWidget* surface) {
  if (surface == nullptr) {
    return false;
  }

  const QList<TerminalWidget*> all = terminals();
  if (all.size() <= 1) {
    emit closeRequested(this);
    return true;
  }

  auto* splitter = qobject_cast<QSplitter*>(surface->parentWidget());
  surface->setParent(nullptr);
  surface->deleteLater();
  if (splitter != nullptr) {
    collapseSplitter(splitter);
  }

  const QList<TerminalWidget*> remaining = terminals();
  setActive(remaining.isEmpty() ? nullptr : remaining.constFirst());
  if (m_active != nullptr) {
    m_active->setFocus(Qt::OtherFocusReason);
  }
  return true;
}

bool TerminalTab::focusSplit(ghostty_action_goto_split_e direction) {
  const QList<TerminalWidget*> all = terminals();
  if (m_active == nullptr || all.size() < 2) {
    return false;
  }

  const int currentIndex = all.indexOf(m_active);
  if (direction == GHOSTTY_GOTO_SPLIT_PREVIOUS ||
      direction == GHOSTTY_GOTO_SPLIT_NEXT) {
    const int delta = direction == GHOSTTY_GOTO_SPLIT_PREVIOUS ? -1 : 1;
    const int index = (currentIndex + delta + all.size()) % all.size();
    setActive(all.at(index));
    m_active->setFocus(Qt::ShortcutFocusReason);
    return true;
  }

  const QPoint current = m_active->mapTo(this, m_active->rect().center());
  TerminalWidget* best = nullptr;
  qint64 bestScore = std::numeric_limits<qint64>::max();

  for (TerminalWidget* candidate : all) {
    if (candidate == m_active) {
      continue;
    }

    const QPoint point = candidate->mapTo(this, candidate->rect().center());
    const int dx = point.x() - current.x();
    const int dy = point.y() - current.y();
    const bool eligible = (direction == GHOSTTY_GOTO_SPLIT_LEFT && dx < 0) ||
                          (direction == GHOSTTY_GOTO_SPLIT_RIGHT && dx > 0) ||
                          (direction == GHOSTTY_GOTO_SPLIT_UP && dy < 0) ||
                          (direction == GHOSTTY_GOTO_SPLIT_DOWN && dy > 0);
    if (!eligible) {
      continue;
    }

    const qint64 primary = direction == GHOSTTY_GOTO_SPLIT_LEFT ||
                                   direction == GHOSTTY_GOTO_SPLIT_RIGHT
                               ? std::abs(dx)
                               : std::abs(dy);
    const qint64 secondary = direction == GHOSTTY_GOTO_SPLIT_LEFT ||
                                     direction == GHOSTTY_GOTO_SPLIT_RIGHT
                                 ? std::abs(dy)
                                 : std::abs(dx);
    const qint64 score = primary * 100000 + secondary;
    if (score < bestScore) {
      bestScore = score;
      best = candidate;
    }
  }

  if (best == nullptr) {
    return false;
  }
  setActive(best);
  best->setFocus(Qt::ShortcutFocusReason);
  return true;
}

bool TerminalTab::resizeSplit(const ghostty_action_resize_split_s& resize) {
  if (m_active == nullptr || resize.amount == 0) {
    return false;
  }

  const bool horizontal = resize.direction == GHOSTTY_RESIZE_SPLIT_LEFT ||
                          resize.direction == GHOSTTY_RESIZE_SPLIT_RIGHT;
  const Qt::Orientation orientation =
      horizontal ? Qt::Horizontal : Qt::Vertical;

  QWidget* child = m_active;
  auto* splitter = qobject_cast<QSplitter*>(child->parentWidget());
  while (splitter != nullptr && splitter->orientation() != orientation) {
    child = splitter;
    splitter = qobject_cast<QSplitter*>(splitter->parentWidget());
  }
  if (splitter == nullptr) {
    return false;
  }

  QList<int> sizes = splitter->sizes();
  const int index = splitter->indexOf(child);
  if (index < 0 || index >= sizes.size()) {
    return false;
  }

  const bool positive = resize.direction == GHOSTTY_RESIZE_SPLIT_RIGHT ||
                        resize.direction == GHOSTTY_RESIZE_SPLIT_DOWN;
  int neighbor = -1;
  int activeDelta = 0;
  if (positive && index + 1 < sizes.size()) {
    neighbor = index + 1;
    activeDelta = static_cast<int>(resize.amount);
  } else if (positive && index > 0) {
    neighbor = index - 1;
    activeDelta = -static_cast<int>(resize.amount);
  } else if (!positive && index > 0) {
    neighbor = index - 1;
    activeDelta = static_cast<int>(resize.amount);
  } else if (!positive && index + 1 < sizes.size()) {
    neighbor = index + 1;
    activeDelta = -static_cast<int>(resize.amount);
  } else {
    return false;
  }

  const int donor = activeDelta > 0 ? neighbor : index;
  const int amount = std::min(std::abs(activeDelta), sizes.at(donor) - 1);
  if (amount <= 0) {
    return false;
  }
  const int applied = activeDelta > 0 ? amount : -amount;
  sizes[index] += applied;
  sizes[neighbor] -= applied;
  splitter->setSizes(sizes);
  return true;
}

void TerminalTab::equalizeSplits() {
  for (QSplitter* splitter :
       findChildren<QSplitter*>({}, Qt::FindChildrenRecursively)) {
    equalizeSplitter(splitter);
  }
}

void TerminalTab::toggleZoom() {
  if (m_active == nullptr) {
    return;
  }

  m_zoomed = !m_zoomed;
  updateZoomVisibility();
}

bool TerminalTab::canClose() const {
  for (TerminalWidget* terminal : terminals()) {
    if (terminal->surface() != nullptr &&
        ghostty_surface_needs_confirm_quit(terminal->surface())) {
      const auto answer = QMessageBox::question(
          const_cast<TerminalTab*>(this), tr("Close tab?"),
          tr("One or more processes are still running in this tab."),
          QMessageBox::Close | QMessageBox::Cancel, QMessageBox::Cancel);
      return answer == QMessageBox::Close;
    }
  }
  return true;
}

void TerminalTab::connectTerminal(TerminalWidget* terminal) {
  connect(terminal, &TerminalWidget::focused, this,
          [this, terminal]() { setActive(terminal); });
  connect(terminal, &TerminalWidget::titleChanged, this,
          [this, terminal](const QString&) {
            if (terminal == m_active && m_titleOverride.isEmpty()) {
              emit titleChanged(title());
            }
          });
  connect(terminal, &TerminalWidget::tabTitleChanged, this,
          [this](const QString& title) { setTitleOverride(title); });
  connect(terminal, &TerminalWidget::closeRequested, this,
          [this](TerminalWidget* surface) { closeSurface(surface); });
}

void TerminalTab::setActive(TerminalWidget* terminal) {
  if (m_active == terminal) {
    return;
  }
  m_active = terminal;
  updateZoomVisibility();
  emit titleChanged(title());
}

void TerminalTab::collapseSplitter(QSplitter* splitter) {
  if (splitter == nullptr || splitter->count() != 1) {
    return;
  }

  QWidget* remaining = splitter->widget(0);
  remaining->setParent(nullptr);
  if (auto* parentSplitter =
          qobject_cast<QSplitter*>(splitter->parentWidget())) {
    const int index = parentSplitter->indexOf(splitter);
    parentSplitter->replaceWidget(index, remaining);
  } else {
    m_layout->replaceWidget(splitter, remaining);
  }
  splitter->deleteLater();
}

void TerminalTab::equalizeSplitter(QSplitter* splitter) {
  if (splitter != nullptr && splitter->count() > 0) {
    splitter->setSizes(QList<int>(splitter->count(), 1));
  }
}

void TerminalTab::updateZoomVisibility() {
  for (TerminalWidget* terminal : terminals()) {
    terminal->setVisible(!m_zoomed || terminal == m_active);
  }
}
