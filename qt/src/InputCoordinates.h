#pragma once

#include <QPointF>

// QMouseEvent positions use logical widget coordinates. The embedded Ghostty
// runtime applies the surface content scale, so the host must not scale again.
[[nodiscard]] QPointF ghosttySurfaceMousePosition(
    const QPointF& logicalPosition,
    qreal devicePixelRatio);
