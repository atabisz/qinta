// Assertions for include/seated.h -- App's seam-wrapper storage, on both
// platforms, with fake backends.
//
// WHY THIS TARGET EXISTS. The slice that introduced Seated<> changed App from
// holding the Direct2D wrappers BY VALUE to owning a pointer to the abstract
// seam type, which is what lets a non-Windows build link a target that
// instantiates an App at all. The Windows build proves it compiles and the app
// still runs; nothing proved the SEATING LOGIC, because the only callers were the
// seat* functions in app.h and their only caller was src/d2d_init.cpp, which was
// Windows-only (both retired in 0b39ae6). So the mechanism the whole slice rested on had
// no assertion.
//
// THE PROPERTY THAT MATTERS is that seating is idempotent: reseating an
// UNCHANGED native must not rebuild the wrapper. That is not a performance
// nicety. D2DTextFormat OWNS its native and Releases in its destructor, so a
// blind reset-then-rewrap would Release the very pointer the new wrapper is
// about to hold. The old by-value code got this right by comparing
// storage->native(); the abstract base has no native() to ask, so Seated caches
// the key itself -- and that cache is the thing most likely to rot silently,
// because getting it wrong still compiles and still draws the right picture.
// It is asserted here with construction and destruction counters.

#include "seated.h"

#include <cstdio>
#include <type_traits>

static int failures = 0;
static int checks = 0;

static void checkBool(bool got, bool want, const char* what) {
    checks++;
    if (got != want) {
        failures++;
        std::printf("FAIL %s: got %s, want %s\n", what,
                    got ? "true" : "false", want ? "true" : "false");
    }
}

static void checkInt(int got, int want, const char* what) {
    checks++;
    if (got != want) {
        failures++;
        std::printf("FAIL %s: got %d, want %d\n", what, got, want);
    }
}

static void checkPtr(const void* got, const void* want, const char* what) {
    checks++;
    if (got != want) {
        failures++;
        std::printf("FAIL %s: pointers differ (got %s, want %s)\n", what,
                    got ? "non-null" : "null", want ? "non-null" : "null");
    }
}

static void checkStr(const char* got, const char* want, const char* what) {
    checks++;
    bool same = got && want;
    for (int i = 0; same; ++i) {
        if (got[i] != want[i]) { same = false; break; }
        if (got[i] == '\0') break;
    }
    if (!same) {
        failures++;
        std::printf("FAIL %s: got \"%s\", want \"%s\"\n", what,
                    got ? got : "(null)", want ? want : "(null)");
    }
}

// --- the fakes -------------------------------------------------------------
//
// FakeNative stands in for a COM interface: only ever held as a pointer and
// never dereferenced by Seated, exactly like ID2D1Bitmap* off Windows.
struct FakeNative {
    int id = 0;
};

// The abstract seam type. Mirrors tinta::Bitmap and friends: a virtual
// destructor and one pure virtual, so the storage genuinely cannot be a value.
struct FakeSeam {
    static int constructed;
    static int destroyed;

    FakeSeam() { ++constructed; }
    virtual ~FakeSeam() { ++destroyed; }
    virtual const char* backend() const = 0;
    virtual const FakeNative* native() const = 0;

    static void resetCounters() { constructed = 0; destroyed = 0; }
};
int FakeSeam::constructed = 0;
int FakeSeam::destroyed = 0;

// Two concrete backends, so the polymorphism the slice exists for is exercised
// rather than assumed: the storage names only FakeSeam, and which implementation
// answers depends solely on what was seated.
struct FakeBackendA final : FakeSeam {
    explicit FakeBackendA(FakeNative* n) : n_(n) {}
    const char* backend() const override { return "A"; }
    const FakeNative* native() const override { return n_; }
private:
    FakeNative* n_;
};

struct FakeBackendB final : FakeSeam {
    explicit FakeBackendB(FakeNative* n) : n_(n) {}
    const char* backend() const override { return "B"; }
    const FakeNative* native() const override { return n_; }
private:
    FakeNative* n_;
};

using Storage = Seated<FakeSeam, FakeNative>;

// --- an empty storage is empty --------------------------------------------

static void testDefault() {
    FakeSeam::resetCounters();
    Storage s;
    checkPtr(s.get(), nullptr, "default storage get() is null");
    checkBool(static_cast<bool>(s), false, "default storage is falsy");
    checkInt(FakeSeam::constructed, 0, "default storage builds nothing");
}

// --- seating --------------------------------------------------------------

static void testSeatNull() {
    FakeSeam::resetCounters();
    Storage s;
    FakeSeam* got = s.seat<FakeBackendA>(nullptr);
    checkPtr(got, nullptr, "seat(nullptr) returns null");
    checkPtr(s.get(), nullptr, "seat(nullptr) leaves storage empty");
    checkInt(FakeSeam::constructed, 0, "seat(nullptr) builds nothing");
}

static void testSeatOnce() {
    FakeSeam::resetCounters();
    FakeNative n{1};
    Storage s;
    FakeSeam* got = s.seat<FakeBackendA>(&n);
    checkBool(got != nullptr, true, "seat(native) returns non-null");
    checkPtr(s.get(), got, "seat returns what get() returns");
    checkBool(static_cast<bool>(s), true, "seated storage is truthy");
    checkInt(FakeSeam::constructed, 1, "seat builds exactly one wrapper");
    checkInt(FakeSeam::destroyed, 0, "seat destroys nothing");
    checkPtr(got->native(), &n, "the wrapper holds the native it was given");
}

// THE CENTRAL ASSERTION OF THIS FILE. Reseating the same native must not
// rebuild, because for an OWNING wrapper rebuilding Releases the native the new
// wrapper is about to hold. A regression here still compiles, still returns a
// usable pointer, and on the Direct2D side would free a live text format.
static void testSeatSameNativeIsIdempotent() {
    FakeSeam::resetCounters();
    FakeNative n{1};
    Storage s;
    FakeSeam* first = s.seat<FakeBackendA>(&n);
    FakeSeam* again = s.seat<FakeBackendA>(&n);
    checkPtr(again, first, "reseating the same native returns the SAME wrapper");
    checkInt(FakeSeam::constructed, 1,
             "reseating the same native builds nothing new");
    checkInt(FakeSeam::destroyed, 0,
             "reseating the same native destroys nothing");

    // Ten more reads, as the draw-path accessors do on every frame.
    for (int i = 0; i < 10; ++i) s.seat<FakeBackendA>(&n);
    checkInt(FakeSeam::constructed, 1,
             "twelve reads of an unchanged native still build one wrapper");
}

static void testSeatDifferentNativeRebuilds() {
    FakeSeam::resetCounters();
    FakeNative a{1};
    FakeNative b{2};
    Storage s;
    FakeSeam* first = s.seat<FakeBackendA>(&a);
    FakeSeam* second = s.seat<FakeBackendA>(&b);
    checkBool(second != first, true, "a changed native yields a new wrapper");
    checkInt(FakeSeam::constructed, 2, "a changed native builds a second wrapper");
    checkInt(FakeSeam::destroyed, 1, "the previous wrapper is destroyed");
    checkPtr(s.get()->native(), &b, "the storage now holds the new native");
}

static void testSeatNullAfterRealClears() {
    FakeSeam::resetCounters();
    FakeNative n{1};
    Storage s;
    s.seat<FakeBackendA>(&n);
    FakeSeam* got = s.seat<FakeBackendA>(nullptr);
    checkPtr(got, nullptr, "seating null after a real native returns null");
    checkPtr(s.get(), nullptr, "seating null empties the storage");
    checkInt(FakeSeam::destroyed, 1, "seating null destroys the wrapper");
    // ...and the cached key went with it, so the same native reseats rather than
    // being mistaken for one already installed. This is the bug the cache
    // invites, so it gets its own assertion.
    FakeSeam* back = s.seat<FakeBackendA>(&n);
    checkBool(back != nullptr, true,
              "the same native seats again after being cleared");
    checkInt(FakeSeam::constructed, 2, "and it really was rebuilt");
}

// --- reset ---------------------------------------------------------------

static void testReset() {
    FakeSeam::resetCounters();
    FakeNative n{1};
    Storage s;
    s.seat<FakeBackendA>(&n);
    s.reset();
    checkPtr(s.get(), nullptr, "reset empties the storage");
    checkBool(static_cast<bool>(s), false, "reset makes the storage falsy");
    checkInt(FakeSeam::destroyed, 1, "reset destroys the wrapper");
    // Same cache-invalidation check as above, by the other door: the 14 reset()
    // call sites outside app.h are followed by re-seating on the next init.
    s.seat<FakeBackendA>(&n);
    checkInt(FakeSeam::constructed, 2, "the native seats again after reset");
}

static void testResetIsIdempotent() {
    FakeSeam::resetCounters();
    Storage s;
    s.reset();
    s.reset();
    checkInt(FakeSeam::destroyed, 0, "resetting an empty storage destroys nothing");
    checkPtr(s.get(), nullptr, "an emptied storage stays empty");
}

// --- destruction ---------------------------------------------------------

static void testDestructorReleases() {
    FakeSeam::resetCounters();
    FakeNative n{1};
    {
        Storage s;
        s.seat<FakeBackendA>(&n);
        checkInt(FakeSeam::destroyed, 0, "wrapper alive while the storage is");
    }
    checkInt(FakeSeam::destroyed, 1,
             "destroying the storage destroys the wrapper");
}

// --- install: the non-Windows entry point --------------------------------

static void testInstall() {
    FakeSeam::resetCounters();
    FakeNative n{1};
    Storage s;
    FakeSeam* got = s.install(std::make_unique<FakeBackendB>(&n));
    checkBool(got != nullptr, true, "install returns the installed pointer");
    checkPtr(s.get(), got, "install seats the storage");
    checkInt(FakeSeam::constructed, 1, "install builds exactly one wrapper");
}

static void testInstallReplaces() {
    FakeSeam::resetCounters();
    FakeNative a{1};
    FakeNative b{2};
    Storage s;
    s.install(std::make_unique<FakeBackendB>(&a));
    s.install(std::make_unique<FakeBackendB>(&b));
    checkInt(FakeSeam::destroyed, 1, "install destroys what it replaces");
    checkPtr(s.get()->native(), &b, "install leaves the newest implementation");
}

// install() leaves the cached native key NULL on purpose: an installed
// implementation was not built from a native, so there is nothing to compare
// against, and claiming otherwise would let a later seat() of that same native
// keep an implementation it never built. Asserted because it looks like an
// oversight and is not.
static void testInstallThenSeatRebuilds() {
    FakeSeam::resetCounters();
    FakeNative n{1};
    Storage s;
    s.install(std::make_unique<FakeBackendB>(&n));
    FakeSeam* after = s.seat<FakeBackendA>(&n);
    checkInt(FakeSeam::constructed, 2,
             "seat after install rebuilds rather than trusting the pointer");
    checkStr(after ? after->backend() : nullptr, "A",
             "seat after install leaves the backend seat() was asked for");
}

// --- polymorphism: the reason the slice exists ---------------------------

static void testDispatchFollowsTheSeatedBackend() {
    FakeNative n{1};
    // The storage type names only FakeSeam in both cases; which implementation
    // answers is decided entirely by what was seated. This is the property that
    // lets App hold a backend it does not name, and therefore the property the
    // whole slice buys.
    Storage a;
    FakeSeam* seamA = a.seat<FakeBackendA>(&n);
    checkStr(seamA ? seamA->backend() : nullptr, "A",
             "a storage seated with backend A dispatches to A");
    Storage b;
    FakeSeam* seamB = b.seat<FakeBackendB>(&n);
    checkStr(seamB ? seamB->backend() : nullptr, "B",
             "a storage seated with backend B dispatches to B");
    // Both storages have the same static type. If that ever stops being true --
    // if someone reintroduces a per-platform concrete type here -- this stops
    // compiling, which is the point.
    static_assert(std::is_same<decltype(a), decltype(b)>::value,
                  "both backends must live in the same storage type");
}

int main() {
    testDefault();
    testSeatNull();
    testSeatOnce();
    testSeatSameNativeIsIdempotent();
    testSeatDifferentNativeRebuilds();
    testSeatNullAfterRealClears();
    testReset();
    testResetIsIdempotent();
    testDestructorReleases();
    testInstall();
    testInstallReplaces();
    testInstallThenSeatRebuilds();
    testDispatchFollowsTheSeatedBackend();

    if (failures) {
        std::printf("%d of %d seated assertion(s) failed\n", failures, checks);
        return 1;
    }
    std::printf("all %d seated assertions run, 0 failed\n", checks);
    return 0;
}
