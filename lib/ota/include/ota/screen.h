#pragma once
// What the dish shows over the pet while an update runs: the question, the
// progress, the restart. Drawn on top of the ordinary frame, so the pet stays
// visible and a child can see he is still there.
#include "ota/session.h"
#include "paint/canvas.h"

namespace ota {

void drawUpdate(paint::Canvas240&, const View&);

}  // namespace ota
