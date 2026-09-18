#ifndef TINTA_SEATED_H
#define TINTA_SEATED_H

#include <memory>
#include <utility>

// App's storage for a seam wrapper, extracted from app.h so it can be ASSERTED.
//
// It went into its own header for the same reason text_utils.h did: it is portable
// logic that was only reachable from a translation unit that could not compile off
// Windows. Nothing here may include "app.h", <windows.h>, or any Direct2D or
// DirectWrite header -- it names no backend and no native type, which is what makes
// it testable with fakes on both platforms (tests/seated_tests.cpp).
//
// The one property everything else depends on is that seating is IDEMPOTENT, and
// that is asserted rather than argued: see the reallocation counters in that test.

// A SEATED WRAPPER: the abstract pointer the tree draws through, plus the native
// it was built from.
//
// WHY THE SECOND MEMBER EXISTS. Seating is idempotent and has to stay that way --
// D2DTextFormat OWNS its native and Releases in its destructor, so a blind
// reset-then-rewrap of an unchanged native would Release the very pointer the new
// wrapper is about to hold. The old code compared `storage->native() != native`,
// which only worked because the storage named the CONCRETE wrapper. It no longer
// does, and tinta::TextFormat has no native() -- that absence is the entire point
// of the seam -- so the comparison key is cached here instead.
//
// WHY THE WRAPPER IS OWNED RATHER THAN HELD BY VALUE. Holding it by value is what
// made App name a backend, and naming a backend in App is what kept every file
// that takes an App& off the portable build: linking one demanded vtables out of
// src/render_d2d.cpp and src/text_d2d.cpp, both Windows-only by design. A pointer
// to the abstract base names no backend at all, so the vtable comes from whichever
// implementation was installed.
//
// WHAT IT COSTS, stated because the previous shape documented the opposite. The
// four bitmap accessors and LayoutBitmap::seam() reseat ON READ, and they run on
// the draw path; the old comment could say rebuilding "costs nothing" because a
// borrowed by-value wrapper was one pointer store. An owned wrapper is a heap
// allocation, so seat() compares FIRST and allocates once per distinct native
// rather than once per read. The cannot-go-stale property the reseat-on-read shape
// was chosen for is preserved -- the native is still checked on every read.
//
// It is also strictly safer than what it replaces for LayoutBitmap: those live in
// a std::vector that render.cpp resizes and push_backs, and the old seam() handed
// out the address of a SIBLING member, which a reallocation invalidated. A heap
// wrapper's address survives the move.
template <class Seam, class Native>
struct Seated {
    std::unique_ptr<Seam> wrapper;
    Native* seated = nullptr;

    // Kept named reset() so all 14 call sites outside this header -- 11 in
    // d2d_init.cpp and 3 in input.cpp -- compile unchanged.
    void reset() { wrapper.reset(); seated = nullptr; }
    Seam* get() const { return wrapper.get(); }
    explicit operator bool() const { return static_cast<bool>(wrapper); }

    // Concrete is the backend wrapper to build, so every caller of this is inside
    // an #ifdef. That is deliberate: the choice of backend is now made in exactly
    // the places that know which one they mean, and nowhere else.
    template <class Concrete>
    Seam* seat(Native* native) {
        if (!native) { reset(); return nullptr; }
        if (seated != native) {
            wrapper = std::make_unique<Concrete>(native);
            seated = native;
        }
        return wrapper.get();
    }

    // Install an already-built implementation. This is the entry point a
    // non-Windows backend needs: nothing off Windows can produce a native COM
    // pointer, so seat() is unreachable there, and without this the storage could
    // only ever be null.
    //
    // IT USED TO SAY "NOT called by anything yet -- the Qt initialiser is separate
    // work", which stopped being true when that initialiser landed and stayed on the
    // page for several slices afterwards. It now has callers in src/qt_init.cpp,
    // src/paint_document.cpp and tests/seated_tests.cpp. Counts are deliberately not
    // written here: this comment's whole failure was asserting a number about the rest
    // of the tree from inside a header that cannot see it, and a count would age the
    // same way the zero did.
    //
    // Worth keeping the original intent, because it was right: this tree has been
    // bitten by a dispatcher with zero callers before, so a genuinely uncalled entry
    // point should say so. The fix is that the claim has to be re-measured by whoever
    // adds or removes a caller, not that the claim should never have been made.
    Seam* install(std::unique_ptr<Seam> impl) {
        wrapper = std::move(impl);
        seated = nullptr;
        return wrapper.get();
    }
};

#endif // TINTA_SEATED_H
