#include "tab_ghost_qt.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QRect>

#include <cstdint>

namespace tinta {
namespace {

QColor toQColor(const Color& c) {
    // Rounding, not truncation, and matching the Win32 arm's `(BYTE)(c.r * 255.0f + 0.5f)` exactly.
    // A half-LSB difference is invisible on any one card and is precisely the kind of thing that
    // makes two arms' screenshots differ for reasons nobody can attribute.
    return QColor(static_cast<int>(c.r * 255.0f + 0.5f), static_cast<int>(c.g * 255.0f + 0.5f),
                  static_cast<int>(c.b * 255.0f + 0.5f));
}

}  // namespace

void tabGhostPaint(QImage& target, const TabGhostCard& card, const QString& title) {
    const TabGhostGeometry& g = card.geometry;
    if (target.width() < g.width || target.height() < g.height) return;

    {
        QPainter painter(&target);
        // FILL THE WHOLE RECTANGLE OPAQUE FIRST, then cut the corners out afterwards. The reverse
        // order -- clip to a rounded path and paint inside it -- is the obvious Qt idiom and is
        // wrong here: QPainter antialiases the clip, so the corner pixels come back with fringe
        // alpha while the Win32 arm's per-pixel rebuild gives them exactly zero. The arms would
        // then differ at the only pixels anybody could point at.
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(0, 0, g.width, g.height, toQColor(card.fill));
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

        // The border: one pixel, inset half a pixel so the stroke lands ON the edge row rather than
        // straddling it. The Win32 arm gets this from FrameRgn(region, brush, 1, 1).
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(toQColor(card.border), 1.0));
        painter.drawRoundedRect(QRectF(0.5, 0.5, g.width - 1.0, g.height - 1.0), g.radius, g.radius);

        // The title, elided rather than clipped, which is what DT_END_ELLIPSIS does on the other
        // arm. Same family, same pixel size, same insets, same vertical centring.
        QFont font("Segoe UI");
        font.setPixelSize(static_cast<int>(g.fontHeight));
        painter.setFont(font);
        painter.setPen(toQColor(card.text));
        const QRect textRect(g.textLeftInset, 0, g.width - g.textRightInset - g.textLeftInset,
                             g.height);
        const QString shown =
            QFontMetrics(font).elidedText(title, Qt::ElideRight, textRect.width());
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, shown);
    }

    // Cut the corners. The same predicate the Win32 alpha rebuild calls, over the same pixels, so
    // the two arms produce the same silhouette by construction rather than by agreement.
    //
    // Writing 0 to a premultiplied pixel clears colour and alpha together, which is what
    // "transparent" means in this format -- setting alpha alone would leave premultiplied colour
    // behind and show as a dark halo wherever the compositor reads it.
    if (target.format() == QImage::Format_ARGB32_Premultiplied ||
        target.format() == QImage::Format_ARGB32) {
        for (int py = 0; py < g.height; py++) {
            uint32_t* row = reinterpret_cast<uint32_t*>(target.scanLine(py));
            for (int px = 0; px < g.width; px++) {
                if (!tabGhostCovers(g, px, py)) row[px] = 0u;
            }
        }
    }
}

TabGhostWidget::TabGhostWidget(const TabGhostCard& card, const QString& title)
    : card_(card), title_(title) {
    // These five flags and attributes are the Qt spelling of the Win32 arm's five extended styles,
    // and they line up one for one:
    //   WS_EX_LAYERED     -> WA_TranslucentBackground (a per-pixel alpha surface)
    //   WS_EX_TRANSPARENT -> WindowTransparentForInput + WA_TransparentForMouseEvents
    //   WS_EX_TOOLWINDOW  -> Qt::Tool (out of the taskbar, out of the window list)
    //   WS_EX_NOACTIVATE  -> WA_ShowWithoutActivating + WindowDoesNotAcceptFocus
    //   WS_EX_TOPMOST     -> WindowStaysOnTopHint
    // Each removes one way for a decoration to behave like an application window. The mapping is
    // spelled out because the next person to touch either arm will be looking for it.
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint |
                   Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    // The whole-card opacity, applied at the window rather than in the paint. The Win32 arm does the
    // same thing -- its 235 rides on the BLENDFUNCTION handed to UpdateLayeredWindow, not on the
    // pixels in the DIB -- so the painted surface stays opaque on both arms and only the composite
    // is translucent.
    setWindowOpacity(static_cast<double>(kTabGhostAlpha) / 255.0);
    setFixedSize(card_.geometry.width, card_.geometry.height);
}

void TabGhostWidget::paintEvent(QPaintEvent* event) {
    (void)event;
    QImage surface(card_.geometry.width, card_.geometry.height,
                   QImage::Format_ARGB32_Premultiplied);
    tabGhostPaint(surface, card_, title_);
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(0, 0, surface);
}

TabGhostWidget* tabGhostCreate(const TabGhostCard& card, const QString& title, int screenX,
                               int screenY) {
    TabGhostWidget* ghost = new TabGhostWidget(card, title);
    ghost->move(screenX, screenY);
    ghost->show();
    return ghost;
}

void tabGhostMove(TabGhostWidget* ghost, int screenX, int screenY) {
    if (ghost == nullptr) return;
    ghost->move(screenX, screenY);
}

void tabGhostDestroy(TabGhostWidget*& ghost) {
    if (ghost == nullptr) return;
    delete ghost;
    ghost = nullptr;
}

}  // namespace tinta
