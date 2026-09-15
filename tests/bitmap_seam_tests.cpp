// Assertions for the bitmap half of the drawing seam.
//
// WHAT THIS FILE EXISTS TO CATCH
//
// The THREE scalar bitmaps on App are reached through accessors that rebuild their
// D2DBitmap wrapper on every read, instead of through a seated pointer field like
// renderTarget and the text formats. That is a design claim -- "it cannot go stale"
// -- and a design claim nobody can falsify is just a comment. The failure mode it
// rules out is specifically silent: a stale wrapper still points at a LIVE, VALID
// bitmap, so the app draws the PREVIOUS image at full opacity with no crash, no
// null, and no assertion tripped anywhere else. A screenshot is the only other
// instrument that would notice, and only if a human happened to know which image was
// correct.
//
// So each accessor is read, its native is CHANGED behind it, and it is read again.
// A seated field would return the first wrapper both times and pass a weaker test
// that only checked non-null.
//
// THIS FILE SAID "FOUR" AND COVERED LayoutBitmap::bitmap THE SAME WAY, and both
// changed when the bitmap-creation seam landed. nativeLightboxBitmap and
// LayoutBitmap::bitmap are shared_ptr<tinta::Bitmap> now, so there is no native
// behind them to change and no wrapper to go stale -- the staleness question does not
// apply to a handle that points at the bitmap rather than at a sibling member. What
// replaces those assertions is not nothing: the reallocation test below still runs,
// because the hazard it was written for (a vector that resize()s and push_back()s
// under a live element) is unchanged and only the defence against it moved.
//
// No device is needed. D2DBitmap BORROWS (render_d2d.h), so it never calls a method
// on what it holds -- construction is one pointer store. The addresses below are
// therefore deliberately fake and are never dereferenced; using a real ID2D1Bitmap
// would need a render target and would test nothing extra. That is a property of
// borrowed ownership, and it is the same property that makes reseat-on-read cheap
// enough to be the shape at all.

#include "app.h"

#include <cstdio>
#include <memory>
#include <type_traits>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

// Two distinct non-null addresses that are never dereferenced. Not the address of a
// real object, and not 1/2 either: a debug allocator or a future assert could
// plausibly special-case tiny values, whereas these are merely arbitrary.
ID2D1Bitmap* const kBitmapA = reinterpret_cast<ID2D1Bitmap*>(0xB17A000);
ID2D1Bitmap* const kBitmapB = reinterpret_cast<ID2D1Bitmap*>(0xB17B000);

// The seam pointer an accessor hands back must resolve to the native it was asked
// about. native() is the only way to ask a D2DBitmap what it holds without drawing.
ID2D1Bitmap* heldBy(tinta::Bitmap* seam) {
    return seam ? static_cast<D2DBitmap*>(seam)->native() : nullptr;
}

void nullNativeYieldsNullSeam(App& app) {
    // Not a formality: the accessors return &storage, which is never null, so an
    // accessor that forgot its null check would hand every drawing site a wrapper
    // around nullptr. DrawBitmap would then be called with a non-null Bitmap* whose
    // native is null -- past every `if (bitmap)` guard in the tree.
    check(app.titleIconBitmap() == nullptr, "null native title icon yields a null seam pointer");
    check(app.startPageIconBitmap() == nullptr, "null native start page icon yields a null seam pointer");
    check(app.linkPeekBitmap() == nullptr, "null native link peek yields a null seam pointer");

    // The lightbox is here too, asked the other way round: it is a
    // shared_ptr<tinta::Bitmap> now, so "empty reads as null" is the same guarantee
    // with no accessor in between. Worth one line rather than none, because the
    // drawing site in overlays.cpp guards on exactly this.
    check(app.lightboxBitmap == nullptr, "an unset lightbox handle reads as null");
}

void accessorTracksItsNative(App& app) {
    app.nativeTitleIconBitmap = kBitmapA;
    check(heldBy(app.titleIconBitmap()) == kBitmapA, "title icon seam holds the native it was given");

    app.nativeStartPageIconBitmap = kBitmapA;
    check(heldBy(app.startPageIconBitmap()) == kBitmapA, "start page icon seam holds the native it was given");

    app.nativeLinkPeekBitmap = kBitmapA;
    check(heldBy(app.linkPeekBitmap()) == kBitmapA, "link peek seam holds the native it was given");
}

void accessorCannotGoStale(App& app) {
    // THE claim. Every one of these fields is replaced while the app runs -- the link
    // peek re-renders on the next hover, both icons are recreated when the render
    // target is. A seated field would need reseating at every mutation site and would
    // pass the test above while failing this one. (The lightbox used to be the fourth
    // and most convincing case here; it is a shared handle now, so it has no native to
    // replace behind an accessor.)
    app.nativeTitleIconBitmap = kBitmapA;
    (void)app.titleIconBitmap();  // read first, so a cached wrapper exists to go stale
    app.nativeTitleIconBitmap = kBitmapB;
    check(heldBy(app.titleIconBitmap()) == kBitmapB, "title icon seam follows a replaced native");

    app.nativeStartPageIconBitmap = kBitmapA;
    (void)app.startPageIconBitmap();
    app.nativeStartPageIconBitmap = kBitmapB;
    check(heldBy(app.startPageIconBitmap()) == kBitmapB, "start page icon seam follows a replaced native");

    app.nativeLinkPeekBitmap = kBitmapA;
    (void)app.linkPeekBitmap();
    app.nativeLinkPeekBitmap = kBitmapB;
    check(heldBy(app.linkPeekBitmap()) == kBitmapB, "link peek seam follows a replaced native");

    // Release sets the native back to null, and the seam must follow that too --
    // otherwise teardown leaves a wrapper around a released bitmap and the next
    // paint draws through it. The lightbox was the case this was written against
    // (closeLightbox nulls it); the link peek is the surviving field with the same
    // lifecycle, so the assertion moved rather than went.
    app.nativeLinkPeekBitmap = nullptr;
    check(app.linkPeekBitmap() == nullptr, "link peek seam follows its native back to null");
}

void accessorsAreIndependent(App& app) {
    // One shared storage member would make the last accessor called win, which is
    // invisible in any test that reads one accessor at a time.
    app.nativeTitleIconBitmap = kBitmapA;
    app.nativeStartPageIconBitmap = kBitmapB;
    app.nativeLinkPeekBitmap = kBitmapA;

    tinta::Bitmap* title = app.titleIconBitmap();
    tinta::Bitmap* startPage = app.startPageIconBitmap();
    tinta::Bitmap* linkPeek = app.linkPeekBitmap();

    check(heldBy(title) == kBitmapA, "title icon keeps its own native while the others are read");
    check(heldBy(startPage) == kBitmapB, "start page icon keeps its own native while the others are read");
    check(heldBy(linkPeek) == kBitmapA, "link peek keeps its own native while the others are read");

    // Two of the three now hold the SAME native, deliberately: it makes the address
    // check below a real check. If it compared three accessors whose natives were all
    // distinct, a shared storage member would still be caught by the heldBy checks
    // above and this line would be redundant.
    check(title != startPage && title != linkPeek && startPage != linkPeek,
          "the three accessors return three distinct storage addresses");
}

void layoutBitmapSurvivesReallocation() {
    // THE HAZARD IS UNCHANGED AND THE DEFENCE MOVED. This test used to justify
    // LayoutBitmap::seam() rebuilding instead of storing: a tinta::Bitmap* held inside
    // an element and aimed at a SIBLING member keeps pointing at the pre-move address
    // once the vector reallocates. render.cpp both resize()s and push_back()s this
    // vector, so reallocation is a normal event, not an edge case. A
    // shared_ptr<tinta::Bitmap> points at the bitmap rather than at a sibling, so
    // moving the element moves a valid handle -- but that is a claim about a type, and
    // a claim about a type is exactly the kind that reads as obviously true right up
    // until somebody stores a raw .get() beside it.
    std::vector<App::LayoutBitmap> bitmaps;
    auto a = std::make_shared<D2DBitmap>(kBitmapA);
    auto b = std::make_shared<D2DBitmap>(kBitmapB);
    bitmaps.push_back({a, tinta::RectF{}});
    const App::LayoutBitmap* before = bitmaps.data();
    check(heldBy(bitmaps[0].bitmap.get()) == kBitmapA, "a layout bitmap holds its native");

    // Force at least one reallocation. reserve() is deliberately NOT used: the point
    // is to move the elements.
    for (int i = 0; i < 64; ++i) bitmaps.push_back({b, tinta::RectF{}});
    check(bitmaps.data() != before, "the vector actually reallocated (otherwise the next check is vacuous)");

    check(heldBy(bitmaps[0].bitmap.get()) == kBitmapA, "a layout bitmap is correct after reallocation");
    check(heldBy(bitmaps[7].bitmap.get()) == kBitmapB, "a relocated layout bitmap holds its own native");

    // Distinct elements must resolve to distinct bitmaps, or the drawing loop would
    // draw one image for every entry. Note what this checks NOW versus before: the old
    // version compared two per-element STORAGE addresses, because each element built
    // its own wrapper. These two point at two shared bitmaps -- and elements that share
    // one image are legitimate here, so the assertion is about the natives.
    check(heldBy(bitmaps[0].bitmap.get()) != heldBy(bitmaps[7].bitmap.get()),
          "distinct elements resolve to distinct natives");

    // A cleared entry must read as null rather than as the previous image.
    bitmaps[0].bitmap.reset();
    check(bitmaps[0].bitmap == nullptr, "a cleared layout bitmap reads as null");

    // Relayout overwrites these elements IN PLACE -- render.cpp resize()s the vector
    // and refills it -- so an element still resolving to a previous document's bitmap
    // would draw the OLD document's image at the NEW document's rectangle. That is the
    // silent failure the whole file exists for, and it is still worth asserting after
    // the ownership change, because assignment is where a hand-written release used to
    // have to be paired.
    bitmaps[1].bitmap = a;
    bitmaps[1].bitmap = b;
    check(heldBy(bitmaps[1].bitmap.get()) == kBitmapB, "a replaced layout bitmap resolves to the new native");

    // THE NEW CLAIM, and the reason the type changed: a paint walking this vector must
    // survive the cache dropping its entry. `a` is the cache's reference; the element's
    // copy has to keep the bitmap reachable after it goes.
    bitmaps[0].bitmap = a;
    a.reset();
    check(bitmaps[0].bitmap && heldBy(bitmaps[0].bitmap.get()) == kBitmapA,
          "a layout bitmap outlives the handle it was copied from");
}

void layoutBitmapFieldIsOnTheSeam() {
    // THIS ASSERTION USED TO SAY THE OPPOSITE, and it was placed to stop exactly the
    // change that has now been made. Its text: "LayoutBitmap::bitmap must stay a
    // native pointer: print.cpp draws it through a device context that is not on the
    // seam" -- true at the time, and asserted as a type because the way it breaks is
    // somebody "finishing" the port by changing the field.
    //
    // The reason was ADDRESSED rather than overruled: render_d2d.h now exports
    // nativeBitmap(), the same escape hatch nativePathGeometry() already was for the
    // same stated reason, and src/print.cpp's page loop calls it. So the gate is
    // inverted rather than deleted -- the field must now BE a shared handle, because
    // the whole point of the change is that the image cache, the layout vector and an
    // open lightbox each keep the bitmap alive independently.
    static_assert(std::is_same<decltype(App::LayoutBitmap::bitmap),
                               std::shared_ptr<tinta::Bitmap>>::value,
                  "LayoutBitmap::bitmap must stay a shared handle: the image cache can "
                  "evict an entry while a paint is still walking this vector, and "
                  "print.cpp reaches the native through nativeBitmap()");
}

}  // namespace

int main() {
    layoutBitmapFieldIsOnTheSeam();

    App app;
    nullNativeYieldsNullSeam(app);
    accessorTracksItsNative(app);
    accessorCannotGoStale(app);
    accessorsAreIndependent(app);
    layoutBitmapSurvivesReallocation();

    // The natives are fake addresses, so nothing may release them. shutdown() would.
    // No lightbox line any more: it is a shared_ptr<tinta::Bitmap> and this test never
    // sets it, and if it did, D2DBitmap BORROWS -- which is why the reallocation test
    // can put fake addresses in shared_ptrs at all.
    app.nativeTitleIconBitmap = nullptr;
    app.nativeStartPageIconBitmap = nullptr;
    app.nativeLinkPeekBitmap = nullptr;

    if (failures == 0) {
        std::printf("all bitmap-seam assertions run, 0 failed\n");
        return 0;
    }
    std::printf("%d bitmap-seam assertion(s) failed\n", failures);
    return 1;
}
