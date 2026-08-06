#include "InputCoordinates.h"

QPointF ghosttySurfaceMousePosition(const QPointF& logicalPosition,
                                    qreal devicePixelRatio) {
  Q_ASSERT(devicePixelRatio > 0);
  Q_UNUSED(devicePixelRatio);
  return logicalPosition;
}
