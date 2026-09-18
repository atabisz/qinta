// Assertions for the bitmap half of the drawing seam.
//
// WHAT THIS FILE EXISTS TO CATCH
//
// The ONE scalar bitmap left on App that is reached through an accessor rebuilds its
// D2DBitmap wrapper on every read, instead of being a seated pointer field like
// renderTarget and the text formats. That is a design claim -- "it cannot go stale"
// -- and a design claim nobody can falsify is just a comment. The failure mode it
// rules out is specifically silent: a stale wrapper still points at a LIVE, VALID
// bitmap, so the app draws the PREVIOUS image at full opacity with no crash, no
// null, and no assertion tripped anywhere else. A screenshot is the only other
// instrument that would notice, and only if a human happened to know which image was
// correct.
//
// So the accessor is read, its native is CHANGED behind it, and it is read again.
// A seated field would return the first wrapper both times and pass a weaker test
// that only checked non-null.
//
// THIS FILE SAID "FOUR", THEN "THREE", AND NOW SAYS "ONE". Each step was the same
// change: nativeLightboxBitmap and LayoutBitmap::bitmap went first when the
// bitmap-creation seam landed, and titleIconBitmap and startPageIconBitmap went when the
// app icon got one portable decode path. All four are shared_ptr<tinta::Bitmap> now, so
// there is no native behind them to change and no wrapper to go stale -- the staleness
// question does not apply to a handle that points at the bitmap rather than at a sibling
// member.
//
// WHAT WAS RETIRED WITH THE ICONS, STATED RATHER THAN QUIETLY DROPPED, because a test
// file that only ever shrinks is worth being suspicious of:
//
//   * Their two staleness assertions and their two tracks-its-native assertions went
//     with the hazard, as the lightbox's did. Not a loss.
//   * accessorsAreIndependent IS GONE ENTIRELY, and that one IS a loss of coverage. It
//     asked whether two accessors could collide on one shared storage member -- a
//     question that needs two accessors to be askable at all, and there is one left.
//     Nothing here can restore it; the check comes back for free if a second borrowed
//     native bitmap is ever added, and it should be added back with it.
//   * What replaces them is iconFieldsAreOnTheSeam() below, which is a different claim
//     rather than a weaker copy of the same one: a static_assert that the two members
//     stay handles. The reason is not tidiness. Their previous type was the ONLY thing
//     the `#ifdef TINTA_D2D_BACKEND` in their accessors existed for, so reverting the
//     type silently reinstates a guard that leaves both icons undrawn off Windows --
//     exactly the way LayoutBitmap::bitmap's gate was inverted rather than deleted.
//
// The reallocation test still runs untouched, because the hazard it was written for (a
// vector that resize()s and push_back()s under a live element) is unchanged.
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
// Counted and PRINTED, because this file has now retired three tests as their
// subjects left the struct, and "0 failed" reads identically whether nine checks
// ran or none did. A count in the output line is what makes a future deletion
// visible in the log rather than only in the diff.
int checksRun = 0;

void check(bool condition, const char* what) {
    ++checksRun;
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

// A seam pointer must resolve to the native behind it. native() is the only way to ask
// a D2DBitmap what it holds without drawing. This used to serve the accessor tests as
// well; with those retired its only caller is the reallocation test below, which is the
// one place a wrapper's identity still has to be interrogated rather than asserted as a
// type.
ID2D1Bitmap* heldBy(tinta::Bitmap* seam) {
    return seam ? static_cast<D2DBitmap*>(seam)->native() : nullptr;
}

void unsetHandlesReadAsNull(App& app) {
    // FOUR handles, asked the same way. Worth one line each rather than none, because
    // the drawing sites guard on exactly this -- overlays.cpp for the lightbox and the
    // link peek, tabs.cpp and startpage.cpp for the icons -- and for all four the guard
    // is the whole reason a failed render draws nothing instead of crashing.
    //
    // THIS FUNCTION WAS CALLED nullNativeYieldsNullSeam AND ITS FIRST LINE WAS THE POINT:
    // it asked whether an accessor returning &storage remembered its null check, because
    // a storage address is never null and a forgotten check would hand every drawing site
    // a wrapper around nullptr -- past every `if (bitmap)` guard in the tree. There are no
    // accessors left to forget it, which is why the name changed with the last one.
    check(app.lightboxBitmap == nullptr, "an unset lightbox handle reads as null");
    check(app.titleIconBitmap == nullptr, "an unset title icon handle reads as null");
    check(app.startPageIconBitmap == nullptr, "an unset start page icon handle reads as null");
    check(app.linkPeekBitmap == nullptr, "an unset link peek handle reads as null");
}

// THREE TESTS HAVE NOW BEEN RETIRED HERE AS THEIR SUBJECTS LEFT, and the pattern is the
// point rather than any one of them. accessorsAreIndependent went when only one accessor
// was left (it caught a single shared storage member, a hazard that needs two storages to
// exist). accessorTracksItsNative and accessorCannotGoStale went with the link peek, the
// last accessor of the five.
//
// accessorCannotGoStale was THE claim of this file: the peek is replaced while the app
// runs, so a seated field would need reseating at every mutation site, and a version that
// stored instead of reseating would pass every other test while failing that one. It is
// not rewritten against the handle, and that is deliberate -- the hazard it named cannot
// occur once the member is a shared_ptr<tinta::Bitmap>, because there is no second
// representation of the bitmap to fall out of step with. A test asserting that a
// shared_ptr still points where it was assigned is a test shaped like the old one that
// checks nothing. Saying so here is the alternative to leaving a green stub that looks
// like the coverage is still there.
//
// What replaced it is the static_assert below: the hazard moved from "the wrapper goes
// stale" to "somebody changes the field back", and that is a claim about a TYPE, so it is
// asserted at compile time where it cannot be skipped. git history has all three bodies.
// IF A BORROWED NATIVE BITMAP IS EVER ADDED TO App AGAIN, BRING THEM BACK WITH IT.

// The chrome bitmap members must STAY handles, and this is a gate rather than a tidiness
// check: an assertion placed to stop somebody "finishing" the port by changing a field
// back. Same inversion this file applies to LayoutBitmap::bitmap below.
//
// What breaks if they revert: an ID2D1Bitmap* member cannot be filled by
// tinta::decodeIconBytesToBitmap or by renderPeekBitmap, so the fill sites need Direct2D
// again, so the members need a reseating accessor behind `#ifdef TINTA_D2D_BACKEND` back
// -- and that guard's #else returned nullptr, which draws nothing at all off Windows. The
// revert would compile, link, run, and look correct on the only platform its author was
// testing on.
//
// Named for the three chrome members rather than the two icons since the link peek
// joined: the peek is not an icon, and a function name that has to be read past is the
// same defect as a stale count.
void chromeBitmapFieldsAreOnTheSeam() {
    static_assert(std::is_same<decltype(App::titleIconBitmap),
                               std::shared_ptr<tinta::Bitmap>>::value,
                  "App::titleIconBitmap must stay a shared handle: a native pointer here "
                  "puts Direct2D back at the fill site in tabs.cpp and the #ifdef back in "
                  "app.h, which silently leaves the strip icon undrawn off Windows");
    static_assert(std::is_same<decltype(App::startPageIconBitmap),
                               std::shared_ptr<tinta::Bitmap>>::value,
                  "App::startPageIconBitmap must stay a shared handle: same reason, and it "
                  "has two consumers -- startpage.cpp and editrail.cpp -- drawing one "
                  "64px bitmap at two different DIP sizes");
    static_assert(std::is_same<decltype(App::linkPeekBitmap),
                               std::shared_ptr<tinta::Bitmap>>::value,
                  "App::linkPeekBitmap must stay a shared handle: a native pointer here "
                  "brings back renderPeekBitmap returning ID2D1Bitmap*, the three "
                  "hand-written Release() calls that paired with it, and the reseating "
                  "accessor in app.h whose #else drew no peek panel at all off Windows");
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
    // The reason was ADDRESSED rather than overruled: render_d2d.h exported
    // nativeBitmap(), the same escape hatch nativePathGeometry() already was for the
    // same stated reason, and src/print.cpp's page loop called it. So the gate was
    // inverted rather than deleted -- the field must now BE a shared handle, because
    // the whole point of the change is that the image cache, the layout vector and an
    // open lightbox each keep the bitmap alive independently.
    //
    // AND THAT ESCAPE HATCH IS NOW CLOSED, NOT MERELY UNUSED -- this paragraph said
    // "unused" for one slice and the distinction is the whole point of the one that
    // followed: nativeBitmap() is file-local in src/render_d2d.cpp, so print.cpp could
    // not reach it now even if somebody wanted to invert this gate back. The assertion
    // is unchanged either way.
    // print.cpp's drawDocumentRange takes a tinta::RenderTarget& and draws
    // app.layoutBitmaps straight through the seam, so it no longer unwraps anything.
    // The trailing clause of the old failure text -- "print.cpp reaches the native
    // through nativeBitmap()" -- is therefore false, and it is corrected here rather
    // than left, because a failure MESSAGE is read at exactly the moment somebody is
    // reasoning about why the type matters. The first clause is the whole reason now,
    // and it was always the load-bearing one: the cache can evict mid-paint.
    static_assert(std::is_same<decltype(App::LayoutBitmap::bitmap),
                               std::shared_ptr<tinta::Bitmap>>::value,
                  "LayoutBitmap::bitmap must stay a shared handle: the image cache can "
                  "evict an entry while a paint is still walking this vector, so the "
                  "element's own reference is what keeps the bitmap reachable");
}

}  // namespace

int main() {
    layoutBitmapFieldIsOnTheSeam();
    chromeBitmapFieldsAreOnTheSeam();

    App app;
    unsetHandlesReadAsNull(app);
    layoutBitmapSurvivesReallocation();

    // NO TEARDOWN LINE ANY MORE, and its absence is the cheapest evidence in this file
    // that the port landed. It used to read `app.nativeLinkPeekBitmap = nullptr;` -- the
    // fake addresses above are not real bitmaps, so shutdown() releasing the last native
    // member would have called Release() on 0xB17A000. Every bitmap App holds is now a
    // shared_ptr<tinta::Bitmap>, this test sets none of them, and the reallocation test's
    // handles wrap D2DBitmap, which BORROWS -- which is why fake addresses can go in
    // shared_ptrs at all.

    if (failures == 0) {
        std::printf("all %d bitmap-seam assertions run, 0 failed\n", checksRun);
        return 0;
    }
    std::printf("%d of %d bitmap-seam assertion(s) failed\n", failures, checksRun);
    return 1;
}
