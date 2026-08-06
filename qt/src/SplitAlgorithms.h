#pragma once

#include <QList>
#include <QPoint>

#include <ghostty.h>

namespace qhostty {

int directionalNeighbor(const QList<QPoint>& centers,
                        int current,
                        ghostty_action_goto_split_e direction);

bool resizeAdjacent(QList<int>& sizes, int index, bool positive, int amount);

}  // namespace qhostty
