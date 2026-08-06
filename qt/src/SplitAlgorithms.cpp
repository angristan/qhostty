#include "SplitAlgorithms.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace qhostty {

int directionalNeighbor(const QList<QPoint>& centers,
                        int current,
                        ghostty_action_goto_split_e direction) {
  if (current < 0 || current >= centers.size()) {
    return -1;
  }

  int best = -1;
  qint64 bestScore = std::numeric_limits<qint64>::max();
  for (int index = 0; index < centers.size(); ++index) {
    if (index == current) {
      continue;
    }
    const int dx = centers.at(index).x() - centers.at(current).x();
    const int dy = centers.at(index).y() - centers.at(current).y();
    const bool eligible = (direction == GHOSTTY_GOTO_SPLIT_LEFT && dx < 0) ||
                          (direction == GHOSTTY_GOTO_SPLIT_RIGHT && dx > 0) ||
                          (direction == GHOSTTY_GOTO_SPLIT_UP && dy < 0) ||
                          (direction == GHOSTTY_GOTO_SPLIT_DOWN && dy > 0);
    if (!eligible) {
      continue;
    }

    const bool horizontal = direction == GHOSTTY_GOTO_SPLIT_LEFT ||
                            direction == GHOSTTY_GOTO_SPLIT_RIGHT;
    const qint64 primary = horizontal ? std::abs(dx) : std::abs(dy);
    const qint64 secondary = horizontal ? std::abs(dy) : std::abs(dx);
    const qint64 score = primary * 100000 + secondary;
    if (score < bestScore) {
      bestScore = score;
      best = index;
    }
  }
  return best;
}

bool resizeAdjacent(QList<int>& sizes, int index, bool positive, int amount) {
  if (index < 0 || index >= sizes.size() || amount <= 0) {
    return false;
  }

  int neighbor = -1;
  int activeDelta = 0;
  if (positive && index + 1 < sizes.size()) {
    neighbor = index + 1;
    activeDelta = amount;
  } else if (positive && index > 0) {
    neighbor = index - 1;
    activeDelta = -amount;
  } else if (!positive && index > 0) {
    neighbor = index - 1;
    activeDelta = amount;
  } else if (!positive && index + 1 < sizes.size()) {
    neighbor = index + 1;
    activeDelta = -amount;
  } else {
    return false;
  }

  const int donor = activeDelta > 0 ? neighbor : index;
  const int appliedAmount = std::min(amount, sizes.at(donor) - 1);
  if (appliedAmount <= 0) {
    return false;
  }
  const int applied = activeDelta > 0 ? appliedAmount : -appliedAmount;
  sizes[index] += applied;
  sizes[neighbor] -= applied;
  return true;
}

}  // namespace qhostty
