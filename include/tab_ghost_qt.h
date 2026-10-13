#ifndef TINTA_TAB_GHOST_QT_H
#define TINTA_TAB_GHOST_QT_H

#include <QFont>
#include <QImage>
#include <QString>
#include <QWidget>

#include "tab_ghost.h"

// THE QT ARM OF THE DRAG GHOST: a frameless, translucent, input-transparent widget.
//
// This file names Qt types because it IS the Qt arm; include/tab_ghost.h names neither Qt's nor
// Windows'. Every number this arm draws with comes from there, which is the whole reason it could be
// written at all without reverse-engineering a Win32 function.
//
// THE PAINT IS SEPARATE FROM THE WIDGET, deliberately. tabGhostPaint draws the card into a plain
// QImage, so what the card looks like can be asserted pixel by pixel with no window, no screen and
// no compositor -- which is what makes this arm's correctness a reading rather than a screenshot
// somebody looked at once. The widget is then a thin shell that paints itself with the same call.
namespace tinta {

// Draws the card into `target`, which must be at least the card's geometry in size and should be
// QImage::Format_ARGB32_Premultiplied.
//
// IT OWNS EVERY PIXEL IN THAT RECTANGLE: opaque inside the rounded card, fully transparent outside.
// It does not composite onto what was already there, and that is worth stating because it is what
// lets a test pre-fill the image with an obvious sentinel and then read the result as evidence the
// function ran -- rather than as evidence it did nothing to an already-transparent buffer.
//
// The rounded-corner cut is tabGhostCovers, the same predicate the Win32 arm's alpha rebuild calls.
// Not a QPainterPath clip that happens to agree with it: two implementations of one shape is the
// drift this seam exists to prevent, and an antialiased path would put fringe alpha in the corners
// where this arm must match Windows' hard cut.
void tabGhostPaint(QImage& target, const TabGhostCard& card, const QString& title);

// The title's font: the chrome's UI family (kDefaultFont, which the tab strip's own titles
// use) at `pixelSize`. Separate so a test can read which family the card asks for.
QFont tabGhostFont(int pixelSize);

// The card as a live widget. Deliberately no Q_OBJECT: it has no signals, no slots and no
// translations, so it needs no moc pass and cannot acquire one by accident.
class TabGhostWidget : public QWidget {
public:
    TabGhostWidget(const TabGhostCard& card, const QString& title);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    TabGhostCard card_;
    QString title_;
};

// Creates and shows the card at a screen position without activating it. The caller owns the
// returned widget and must pass it to tabGhostDestroy.
TabGhostWidget* tabGhostCreate(const TabGhostCard& card, const QString& title, int screenX,
                               int screenY);

void tabGhostMove(TabGhostWidget* ghost, int screenX, int screenY);

// Deletes the card and nulls the caller's pointer, matching the Win32 arm's signature shape for the
// same reason: the destroy-and-forget pair must not be able to come apart.
void tabGhostDestroy(TabGhostWidget*& ghost);

}  // namespace tinta

#endif  // TINTA_TAB_GHOST_QT_H
