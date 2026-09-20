// THE WIN32 GHOST, CREATED AND MEASURED WITH NO `App` ANYWHERE.
//
// This file is the reason the seam was worth cutting. The code it exercises is the same layered
// popup src/tabs.cpp has always created, but it used to read `app.contentScale`, `app.theme`,
// `tabStripBackground(app)` and `app.tabGhostWnd` -- so testing it meant a real window, a theme, a
// loaded document and a message loop, and the honest thing to write in an ISA was "deferred, not
// reachable from a test". Re-parameterising the arm off a TabGhostCard is what turned that
// deferral into the nine lines below.
//
// IT ASKS THE OS WHAT IT GOT, rather than trusting what it asked for. `GetClassNameW`,
// `GetWindowLongW` and `GetWindowRect` all read back from the window manager, so a CreateWindowExW
// whose style argument silently failed -- or a geometry the seam computed one way and passed another
// -- shows up here. Asserting on the arguments we just passed would be an assertion over one
// capture, which cannot fail.
//
// A REAL WINDOW APPEARS ON THE DESKTOP FOR A FEW MILLISECONDS while this runs, at 100,100. That is
// unavoidable -- the claim is about a window the OS actually made -- but it is WS_EX_NOACTIVATE,
// WS_EX_TRANSPARENT and WS_EX_TOOLWINDOW, so it cannot take focus, cannot take a click, and never
// reaches the taskbar. Every arm destroys its window whether or not its assertions held; a failing
// run must not leave a topmost card pinned over Alex's screen.
#include "tab_ghost_win32.h"

#include <cstdio>
#include <cwchar>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* message) {
    checks++;
    if (condition) return;
    std::printf("FAIL: %s\n", message);
    failures++;
}

tinta::TabGhostCard testCard(float scale) {
    // Colours chosen so nothing here can pass by matching a default: an all-zero Color would be
    // indistinguishable from an uninitialised one, and RGB(0,0,0) is also what a failed brush
    // resolves to.
    const tinta::Color fill{0.15f, 0.17f, 0.20f, 1.0f};
    const tinta::Color border{0.80f, 0.82f, 0.85f, 1.0f};
    return tinta::tabGhostCard(scale, fill, border, border);
}

}  // namespace

int main() {
    const float scale = 1.0f;
    const tinta::TabGhostCard card = testCard(scale);

    // --- The window the OS actually made -------------------------------------
    HWND ghost = tinta::tabGhostCreate(card, L"probe-document.md", 100, 100);
    check(ghost != nullptr && IsWindow(ghost) != FALSE,
          "WIN32: tabGhostCreate returns a live window handle, built from a card and two integers "
          "with no App in scope");
    if (ghost == nullptr) {
        // Nothing below can say anything without a window, and a run that reported a string of
        // passes over a null handle would be worse than a short one.
        std::printf("FAIL: no window was created -- the remaining assertions cannot run\n");
        std::printf("FAIL: %d/%d assertions passed\n", checks - failures, checks);
        return 1;
    }

    wchar_t className[64] = {0};
    const int nameLength = GetClassNameW(ghost, className, 64);
    check(nameLength > 0 && std::wcscmp(className, tinta::kTabGhostWindowClass) == 0,
          "WIN32: and the OS reports its class as TintaTabGhost -- read back from the window "
          "manager, not from the string we passed in");
    check(std::wcscmp(tinta::kTabGhostWindowClass, L"TintaTabGhost") == 0,
          "WIN32: which is the name the shipped build registered, unchanged by the move -- a "
          "renamed class would break nothing visible and quietly orphan any tool keyed on it");

    const LONG style = GetWindowLongW(ghost, GWL_STYLE);
    const LONG exStyle = GetWindowLongW(ghost, GWL_EXSTYLE);
    check((style & WS_POPUP) == WS_POPUP,
          "WIN32: the card is a WS_POPUP, so it has no frame, no caption and no border to draw");
    check((style & WS_CHILD) == 0,
          "WIN32: and not a child window, which would clip it to the tab strip -- the whole point "
          "is that it leaves the window");
    // FIVE extended styles, not four. Each one is load-bearing and each removes a different way
    // for a decoration to behave like an application window, so they are asserted individually:
    // a single combined mask would report "some style is missing" without saying which.
    check((exStyle & WS_EX_LAYERED) == WS_EX_LAYERED,
          "WIN32: WS_EX_LAYERED, without which UpdateLayeredWindow fails and the card never paints");
    check((exStyle & WS_EX_TRANSPARENT) == WS_EX_TRANSPARENT,
          "WIN32: WS_EX_TRANSPARENT, so hit-testing falls through to whatever is underneath -- the "
          "card is following the cursor, so anything else would make it eat its own drag");
    check((exStyle & WS_EX_TOOLWINDOW) == WS_EX_TOOLWINDOW,
          "WIN32: WS_EX_TOOLWINDOW, which keeps it out of the taskbar and out of Alt-Tab");
    check((exStyle & WS_EX_NOACTIVATE) == WS_EX_NOACTIVATE,
          "WIN32: WS_EX_NOACTIVATE, so showing it cannot steal focus from the document being "
          "dragged");
    check((exStyle & WS_EX_TOPMOST) == WS_EX_TOPMOST,
          "WIN32: and WS_EX_TOPMOST, so it stays above the window it was dragged out of");

    // --- The size is the PORTABLE geometry, measured through the OS ----------
    RECT rect = {0, 0, 0, 0};
    check(GetWindowRect(ghost, &rect) != FALSE, "WIN32: GetWindowRect succeeds on the card");
    check(rect.right - rect.left == card.geometry.width,
          "WIN32: the window is exactly as wide as tabGhostGeometry says -- the arm does not carry "
          "its own copy of 190");
    check(rect.bottom - rect.top == card.geometry.height,
          "WIN32: and exactly as tall, so the portable geometry is what reached the OS");
    check(rect.left == 100 && rect.top == 100,
          "WIN32: and it sits where it was asked to, which is what makes the placement arithmetic "
          "in tab_ghost.cpp observable at all");

    // --- Move follows -------------------------------------------------------
    tinta::tabGhostMove(ghost, 340, 260);
    RECT moved = {0, 0, 0, 0};
    GetWindowRect(ghost, &moved);
    check(moved.left == 340 && moved.top == 260,
          "WIN32: a move lands where it was told");
    check(moved.right - moved.left == card.geometry.width &&
              moved.bottom - moved.top == card.geometry.height,
          "WIN32: and does not resize the card -- SWP_NOSIZE, because a layered window that "
          "resizes loses the surface it was composited from");

    // --- Destroy ------------------------------------------------------------
    tinta::tabGhostDestroy(ghost);
    check(ghost == nullptr,
          "WIN32: destroy NULLS THE CALLER'S HANDLE -- it takes the handle by reference for this, "
          "because the shipped code repeated the destroy-and-forget pair at three call sites and "
          "each one was a chance to keep a dangling HWND");

    // --- CONTROL: the geometry is not hardcoded in the arm -------------------
    // Every size assertion above compares the window against the same card that built it, so an
    // arm ignoring the card entirely and using its own 190x34 would pass all of them. This is the
    // arm being handed a different card and having to produce a different window.
    const tinta::TabGhostCard bigger = testCard(2.0f);
    HWND large = tinta::tabGhostCreate(bigger, L"probe-document.md", 100, 100);
    if (large != nullptr) {
        RECT largeRect = {0, 0, 0, 0};
        GetWindowRect(large, &largeRect);
        const int width = largeRect.right - largeRect.left;
        const int height = largeRect.bottom - largeRect.top;
        tinta::tabGhostDestroy(large);
        check(width == bigger.geometry.width && height == bigger.geometry.height,
              "WIN32 CONTROL: a card at scale 2.0 produces a 380x68 window, so the arm reads the "
              "geometry it is handed rather than holding its own copy of the numbers");
        check(width != card.geometry.width,
              "WIN32 CONTROL: and that is a different size from the scale-1.0 card, so the two "
              "readings above are not the same reading twice");
    } else {
        check(false, "WIN32 CONTROL: a second card at scale 2.0 could be created");
    }

    // --- CONTROL: the class survives a second registration -------------------
    // ensureClassRegistered() runs once per process behind a static bool. Both creations above went
    // through it, and the second succeeding is the evidence that the guard works -- a second
    // RegisterClassExW would fail and, without the guard, so would every card after the first.
    HWND third = tinta::tabGhostCreate(card, L"x", 100, 100);
    const bool thirdLives = third != nullptr;
    tinta::tabGhostDestroy(third);
    check(thirdLives,
          "WIN32 CONTROL: a third card creates fine, so registering the window class once per "
          "process holds across cards");

    std::printf("%s: %d/%d assertions passed\n", failures == 0 ? "PASS" : "FAIL",
                checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
