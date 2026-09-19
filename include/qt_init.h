#ifndef TINTA_QT_INIT_H
#define TINTA_QT_INIT_H

// The Qt half of App's backend installation: the two calls that put a Qt
// implementation into App's seam storage.
//
// This is the counterpart to what src/d2d_init.cpp does with seat<D2DRenderTarget>
// and seat<D2DTextFactory>, and it is a separate file for the same reason
// render_qt.cpp is separate from render_d2d.cpp: nothing off Windows can produce a
// native COM pointer, so seat() is unreachable there and Seated<>::install() is the
// only door. That door had ZERO callers until this file existed -- include/seated.h
// said so in its own comment, and a template member with no callers had never been
// instantiated, so nothing had ever type-checked it.
//
// WHY TWO LIFETIMES, WHICH IS THE ONE DESIGN DECISION HERE. The text factory is
// process-lifetime: QtTextFactory holds no state at all (see include/text_qt.h --
// QTextBoundaryFinder is constructed per call), so one instance serves the whole
// run, and the TextFormats the app caches in App must outlive any single paint.
// QtRenderTarget is the opposite: it BEGINS a QPainter in its constructor and ENDS
// it in its destructor, so its scope is exactly one paint event. Holding one past
// the end of paintEvent is a Qt warning at best and a painter drawing into a
// device that has moved on at worst. That asymmetry is why installation is one
// function and the render target is an RAII type rather than both being one call.
//
// Direct2D does not have this split -- an ID2D1HwndRenderTarget is device-lifetime
// and BeginDraw/EndDraw bracket the frame -- which is why App's storage has no
// scoping helper today and why this one lives on the Qt side rather than in app.h.

#include <memory>

class QPaintDevice;
struct App;

namespace tinta {
class RenderTarget;
}

// Installs every backend whose lifetime is the process: the text factory, the brush,
// and the sixteen cached DOCUMENT text formats. Call once, after QApplication exists
// and before the first paint.
//
// The brush joined this list when the window started drawing documents -- the paint
// pass reads App::brush at every drawing site -- and it belongs on the
// process-lifetime side because QtBrush holds a colour and no device.
//
// THE FORMATS ARE THE THIRD THING AND THEY WERE FOUND BY A CRASH, which is worth
// recording at the declaration rather than only at the definition. On Windows they come
// from updateTextFormats in src/d2d_init.cpp, a file with no non-Windows half, so off
// Windows App had a text factory and every one of app.textFormat,
// app.headingFormats[0..5], app.codeFormat and the bold/italic combinations null. The
// first document handed to a Qt window segfaulted in src/render.cpp:378 on the null.
// THE CHROME FORMATS ARE HERE TOO SINCE 2026-09-16, and the sentence that stood here --
// "only the DOCUMENT formats are created; the ~25 chrome formats are still null off
// Windows" -- was wrong about the count as well as out of date. Where the 25 came from,
// since guessing at that was itself wrong once: it is the number of lines matching
// `IDWriteTextFormat*` in app.h, and TWENTY-FOUR of those are members. The twenty-fifth is
// the `native` PARAMETER of the seating helper at app.h:1542, which a grep cannot tell from
// a field. So the old figure was a whole-file grep mislabelled as the chrome subset, and the
// off-by-one inside it was a function argument.
//
// The real split, as it was measured then: 11 declarations of DOCUMENT formats -- 16
// objects, because nativeHeadingFormats is a [6] -- plus the 3 of the per-theme preview
// triple, plus TEN chrome (search, signals, theme title, theme header, stats, folder
// browser, both TOC, editor text, editor gutter). The ten agree with what
// updateOverlayFormats assigns in src/d2d_init.cpp, which is the authority on which formats
// are chrome; the member list is only a proxy for it. This call creates those ten.
// Corrected rather than deleted, because the figure appeared in two files and a reader who
// checks one and not the other should find the same number -- and it had TWO different
// wrong explanations before this one, so the derivation is written down here rather than
// the result alone.
//
// THIRD CORRECTION, and this time the subtraction CONFIRMS the split above rather than
// replacing it. The eleven DOCUMENT declarations are gone from app.h: those formats are
// created through tinta::TextFactory in src/text_formats_app.cpp and stored as
// std::unique_ptr<tinta::TextFormat>, so no native member remains for any of them.
// 24 members - 11 document = 13, and the paragraph above independently predicts
// 3 preview + 10 chrome = 13. The two arrive at the same number from different directions,
// which is the first time this comment's figure has been cross-checked rather than merely
// recounted.
//
// AND THE PARAGRAPH ABOVE PREDICTED ITS OWN SUCCESSOR'S MISTAKE. It warns that the
// `native` parameter of the seating helper "a grep cannot tell from a field" -- and the
// re-count for this correction read 14 Seated<> lines against 13 native members before
// noticing the fourteenth was seatFormat's parameter, the same off-by-one in the same
// header, found the same way.
//
// FOURTH CORRECTION, AND THE DECOMPOSITION IS NOW ZERO IN EVERY LIVE ROW. This paragraph
// ended "The whole-file figure is 30 lines and it decomposes exactly: 13 native members,
// 13 Seated<> storages, 2 seatFormat parameters (one of each type on consecutive lines),
// 1 forward declaration, 1 comment. Written as a decomposition, not a total, because every
// wrong version of this figure has been a total." Re-measured rather than adjusted:
//
//   rg -c "IDWriteTextFormat" include/app.h
//
// returns FOUR, and `rg -n` shows all four are comment lines -- two tombstones plus the
// planted-use note at app.h:173 and app.h:1770's own "no IDWriteTextFormat* left in this
// struct". Every native member and every Seated<> storage in that decomposition is gone:
// the chrome and document formats are `std::unique_ptr<tinta::TextFormat>` now. So the
// decomposition did not drift by one, it emptied, and a paragraph three lines above this
// one boasts that this figure had been "cross-checked rather than merely recounted" -- which
// is exactly the state [[a-gated-total-licenses-its-decomposition-to-rot]] describes, in a
// header with no gate over it. FOUND INCIDENTALLY by the dashed-stroke slice's sweep and
// OUT of its subject: nothing that slice measured refutes this, the grep for a neighbouring
// figure is what surfaced it, and it is corrected here rather than carried because the next
// reader cannot tell a stale cross-check from a live one.
//
// QApplication has to exist first. QtTextFactory itself constructs nothing Qt on
// its own, but the QFont/QTextLayout work every TextFormat it hands out performs
// needs the font database, and Qt's font database needs a QGuiApplication.
void installQtBackends(App& app);

// The per-theme preview triple -- three formats for every theme in theme_data -- created
// on first use and not at startup. The Qt counterpart of ensureThemePreviewFormats
// (include/d2d_init.h), and public for the same reason that one is: its caller is the
// theme chooser, which lives in src/overlays.cpp and has not been ported.
//
// Idempotent by the same test the Windows version uses: it returns early when
// App::themePreviewFormats is non-empty, which is why releaseQtBackends CLEARS that
// vector rather than merely nulling the wrappers inside it.
void ensureQtThemePreviewFormats(App& app);

// Undoes installQtBackends. Explicit rather than left to App's destructor because
// App's own teardown path is inside #ifdef _WIN32 (see app.h's cleanup members),
// so off Windows nothing would ever release it, and a test that installs twice
// would be measuring a leak rather than a seam.
void releaseQtBackends(App& app);

// One paint event's render target: installs a QtRenderTarget over the device,
// points App::renderTarget at it, and tears both down on scope exit.
//
// The pointer this makes non-null is the one every drawing site in the tree reads.
// That is the whole point -- the call sites do not learn which backend they got.
class QtScopedRenderTarget {
public:
    QtScopedRenderTarget(App& app, QPaintDevice& device);
    ~QtScopedRenderTarget();

    // Non-copyable and non-movable: it owns a QPainter's begin/end pair through the
    // App's storage, and two objects believing they own the same painter is the one
    // way to get Qt's "painter already active" warning out of this design.
    QtScopedRenderTarget(const QtScopedRenderTarget&) = delete;
    QtScopedRenderTarget& operator=(const QtScopedRenderTarget&) = delete;

    // What install() returned. Null would mean the storage refused the
    // implementation, which is worth asserting rather than assuming.
    tinta::RenderTarget* target() const { return target_; }

private:
    App& app_;
    tinta::RenderTarget* target_ = nullptr;
};

#endif // TINTA_QT_INIT_H
