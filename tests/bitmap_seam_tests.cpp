// Assertions for the bitmap half of the drawing seam.
//
// WHAT THIS FILE EXISTS TO CATCH
//
// The four scalar bitmaps on App and LayoutBitmap::bitmap are reached through
// accessors that rebuild their D2DBitmap wrapper on every read, instead of through a
// seated pointer field like renderTarget and the text formats. That is a design
// claim -- "it cannot go stale" -- and a design claim nobody can falsify is just a
// comment. The failure mode it rules out is specifically silent: a stale wrapper
// still points at a LIVE, VALID bitmap, so the app draws the PREVIOUS image at full
// opacity with no crash, no null, and no assertion tripped anywhere else. A
// screenshot is the only other instrument that would notice, and only if a human
// happened to know which image was correct.
//
// So each accessor is read, its native is CHANGED behind it, and it is read again.
// A seated field would return the first wrapper both times and pass a weaker test
// that only checked non-null.
//
// No device is needed. D2DBitmap BORROWS (render_d2d.h), so it never calls a method
// on what it holds -- construction is one pointer store. The addresses below are
// therefore deliberately fake and are never dereferenced; using a real ID2D1Bitmap
// would need a render target and would test nothing extra. That is a property of
// borrowed ownership, and it is the same property that makes reseat-on-read cheap
// enough to be the shape at all.

#include "app.h"

#include <cstdio>
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
    check(app.lightboxBitmap() == nullptr, "null native lightbox yields a null seam pointer");
    check(app.linkPeekBitmap() == nullptr, "null native link peek yields a null seam pointer");
}

void accessorTracksItsNative(App& app) {
    app.nativeTitleIconBitmap = kBitmapA;
    check(heldBy(app.titleIconBitmap()) == kBitmapA, "title icon seam holds the native it was given");

    app.nativeStartPageIconBitmap = kBitmapA;
    check(heldBy(app.startPageIconBitmap()) == kBitmapA, "start page icon seam holds the native it was given");

    app.nativeLightboxBitmap = kBitmapA;
    check(heldBy(app.lightboxBitmap()) == kBitmapA, "lightbox seam holds the native it was given");

    app.nativeLinkPeekBitmap = kBitmapA;
    check(heldBy(app.linkPeekBitmap()) == kBitmapA, "link peek seam holds the native it was given");
}

void accessorCannotGoStale(App& app) {
    // THE claim. Every one of these fields is replaced while the app runs -- the
    // lightbox opens on one image and then another, the link peek re-renders on the
    // next hover, both icons are recreated when the render target is. A seated field
    // would need reseating at all 8 mutation sites and would pass the test above
    // while failing this one.
    app.nativeTitleIconBitmap = kBitmapA;
    (void)app.titleIconBitmap();  // read first, so a cached wrapper exists to go stale
    app.nativeTitleIconBitmap = kBitmapB;
    check(heldBy(app.titleIconBitmap()) == kBitmapB, "title icon seam follows a replaced native");

    app.nativeStartPageIconBitmap = kBitmapA;
    (void)app.startPageIconBitmap();
    app.nativeStartPageIconBitmap = kBitmapB;
    check(heldBy(app.startPageIconBitmap()) == kBitmapB, "start page icon seam follows a replaced native");

    app.nativeLightboxBitmap = kBitmapA;
    (void)app.lightboxBitmap();
    app.nativeLightboxBitmap = kBitmapB;
    check(heldBy(app.lightboxBitmap()) == kBitmapB, "lightbox seam follows a replaced native");

    app.nativeLinkPeekBitmap = kBitmapA;
    (void)app.linkPeekBitmap();
    app.nativeLinkPeekBitmap = kBitmapB;
    check(heldBy(app.linkPeekBitmap()) == kBitmapB, "link peek seam follows a replaced native");

    // Release sets the native back to null, and the seam must follow that too --
    // otherwise teardown leaves a wrapper around a released bitmap and the next
    // paint draws through it.
    app.nativeLightboxBitmap = nullptr;
    check(app.lightboxBitmap() == nullptr, "lightbox seam follows its native back to null");
}

void accessorsAreIndependent(App& app) {
    // One shared storage member would make the last accessor called win, which is
    // invisible in any test that reads one accessor at a time.
    app.nativeTitleIconBitmap = kBitmapA;
    app.nativeStartPageIconBitmap = kBitmapB;
    app.nativeLightboxBitmap = kBitmapA;
    app.nativeLinkPeekBitmap = kBitmapB;

    tinta::Bitmap* title = app.titleIconBitmap();
    tinta::Bitmap* startPage = app.startPageIconBitmap();
    tinta::Bitmap* lightbox = app.lightboxBitmap();
    tinta::Bitmap* linkPeek = app.linkPeekBitmap();

    check(heldBy(title) == kBitmapA, "title icon keeps its own native while the others are read");
    check(heldBy(startPage) == kBitmapB, "start page icon keeps its own native while the others are read");
    check(heldBy(lightbox) == kBitmapA, "lightbox keeps its own native while the others are read");
    check(heldBy(linkPeek) == kBitmapB, "link peek keeps its own native while the others are read");

    check(title != startPage && title != lightbox && title != linkPeek &&
          startPage != lightbox && startPage != linkPeek && lightbox != linkPeek,
          "the four accessors return four distinct storage addresses");
}

void layoutBitmapSeamSurvivesReallocation() {
    // The reason LayoutBitmap::seam() rebuilds instead of storing: a tinta::Bitmap*
    // held inside an element and aimed at a sibling member keeps pointing at the
    // PRE-MOVE address once the vector reallocates. render.cpp both resize()s and
    // push_back()s this vector, so reallocation is a normal event, not an edge case.
    std::vector<App::LayoutBitmap> bitmaps;
    bitmaps.push_back({kBitmapA, tinta::RectF{}});
    const App::LayoutBitmap* before = bitmaps.data();
    check(heldBy(bitmaps[0].seam()) == kBitmapA, "a layout bitmap's seam holds its native");

    // Force at least one reallocation. reserve() is deliberately NOT used: the point
    // is to move the elements.
    for (int i = 0; i < 64; ++i) bitmaps.push_back({kBitmapB, tinta::RectF{}});
    check(bitmaps.data() != before, "the vector actually reallocated (otherwise the next check is vacuous)");

    check(heldBy(bitmaps[0].seam()) == kBitmapA, "a layout bitmap's seam is correct after reallocation");
    check(heldBy(bitmaps[7].seam()) == kBitmapB, "a relocated layout bitmap's seam holds its own native");

    // Each element must wrap into its OWN storage, or the drawing loop would draw
    // one image for every entry.
    check(bitmaps[0].seam() != bitmaps[7].seam(), "distinct elements yield distinct storage addresses");

    // A cleared bitmap must read as null rather than as the previous image.
    bitmaps[0].bitmap = nullptr;
    check(bitmaps[0].seam() == nullptr, "a layout bitmap with no native yields a null seam pointer");

    // The staleness assertion for the CONTAINER population, which the four scalar
    // ones do not cover. Added after a mutation to seat-once semantics reddened the
    // scalar test and left this one green: relayout overwrites these elements in
    // place -- render.cpp resize()s the vector and refills it -- so an element whose
    // wrapper was seated on a previous document's bitmap and never rebuilt would
    // draw the OLD document's image at the NEW document's rectangle.
    bitmaps[1].bitmap = kBitmapA;
    (void)bitmaps[1].seam();  // read first, so a cached wrapper exists to go stale
    bitmaps[1].bitmap = kBitmapB;
    check(heldBy(bitmaps[1].seam()) == kBitmapB, "a layout bitmap's seam follows a replaced native");
}

void layoutBitmapFieldStaysNative() {
    // print.cpp draws app.layoutBitmaps through its OWN ID2D1DeviceContext, which has
    // no seam representation, so LayoutBitmap::bitmap must remain an ID2D1Bitmap*
    // rather than being replaced by a wrapper. Asserted as a type, because the way
    // this breaks is somebody "finishing" the port by changing the field.
    static_assert(std::is_same<decltype(App::LayoutBitmap::bitmap), ID2D1Bitmap*>::value,
                  "LayoutBitmap::bitmap must stay a native pointer: print.cpp draws it "
                  "through a device context that is not on the seam");
}

}  // namespace

int main() {
    layoutBitmapFieldStaysNative();

    App app;
    nullNativeYieldsNullSeam(app);
    accessorTracksItsNative(app);
    accessorCannotGoStale(app);
    accessorsAreIndependent(app);
    layoutBitmapSeamSurvivesReallocation();

    // The natives are fake addresses, so nothing may release them. shutdown() would.
    app.nativeTitleIconBitmap = nullptr;
    app.nativeStartPageIconBitmap = nullptr;
    app.nativeLightboxBitmap = nullptr;
    app.nativeLinkPeekBitmap = nullptr;

    if (failures == 0) {
        std::printf("all bitmap-seam assertions run, 0 failed\n");
        return 0;
    }
    std::printf("%d bitmap-seam assertion(s) failed\n", failures);
    return 1;
}
