#ifndef TINTA_RENDER_FRAME_H
#define TINTA_RENDER_FRAME_H

#include "app.h"

// Draws one whole frame of the application into app.renderTarget: the start page or the
// editor, the document, and every overlay, panel and dialog, in tinta's order. The caller owns
// the frame bracket -- Direct2D's BeginDraw/EndDraw in src/main_d2d.cpp, a QPainter's lifetime
// in src/qt_window.cpp -- and app.renderTarget must be that frame's target while this runs.
void renderFrame(App& app);

#endif  // TINTA_RENDER_FRAME_H
