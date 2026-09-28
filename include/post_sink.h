#ifndef TINTA_POST_SINK_H
#define TINTA_POST_SINK_H

// WHERE postWindowMessage GOES WHEN THERE IS NO HWND.
//
// On Windows postWindowMessage is PostMessageW, and main_d2d.cpp's window procedure hands
// WM_APP_IMAGE_READY and WM_APP_FOLDER_SEARCH to completeAsyncImage and completeFolderSearch.
// Off the Win32 window stack it returned false, which every owning caller already treats as
// "the window is gone" and answers with a `delete`. That was the right placeholder and the
// wrong behaviour: on qinta the async image worker threw away every download, so a remote
// image stayed `pending` for the life of the document, and the folder scan threw away its
// results.
//
// SAME SHAPE AS timer_sink.h AND repaint_sink.h, for the reasons those headers give: a
// registered closure keeps Qt out of the translation units the Windows build compiles, the
// slot is a function-local static so there is no .cpp, and nothing installed is an honest
// false.
//
// NOT SINGLE-THREADED, AND THAT IS THE ONE REAL DIFFERENCE. Both posters that hand over an
// allocation run on WORKER threads -- that is why they post rather than call. So the slot is
// behind a mutex, and deliverPostedMessage holds it for the whole call: clearPostSink then
// cannot return while a worker is still inside a closure that captures the window being
// destroyed. The closure must therefore be quick and must not post again; the Qt window's
// queues a call onto its own thread and returns.
//
// THE CLOSURE DECIDES WHICH MESSAGES IT TAKES, and it must return false for the rest. A true
// hands ownership of whatever lParam points at to the receiver, so a sink that accepted a
// message it had no handler for would leak it where the caller's `delete` would have freed it.
//
// wParam and lParam travel as the integer types WPARAM and LPARAM are on every stack, so this
// header needs neither windows.h nor win32_types.h.

#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>

namespace tinta {

using PostSink = std::function<bool(unsigned message, std::uintptr_t wParam,
                                    std::intptr_t lParam)>;

// The single slot and its lock. Not part of the surface callers use.
struct PostSinkSlot {
    std::mutex mutex;
    PostSink sink;
};

inline PostSinkSlot& postSinkSlot() {
    static PostSinkSlot slot;
    return slot;
}

// The Qt window calls this once, with a closure over itself.
inline void setPostSink(PostSink sink) {
    PostSinkSlot& slot = postSinkSlot();
    std::lock_guard<std::mutex> lock(slot.mutex);
    slot.sink = std::move(sink);
}

// A window being destroyed MUST call this, and before anything its closure touches goes away.
inline void clearPostSink() {
    PostSinkSlot& slot = postSinkSlot();
    std::lock_guard<std::mutex> lock(slot.mutex);
    slot.sink = PostSink{};
}

inline bool postSinkInstalled() {
    PostSinkSlot& slot = postSinkSlot();
    std::lock_guard<std::mutex> lock(slot.mutex);
    return static_cast<bool>(slot.sink);
}

// What postWindowMessage's off-Win32 arm calls, and its only caller. Any thread.
inline bool deliverPostedMessage(unsigned message, std::uintptr_t wParam,
                                 std::intptr_t lParam) {
    PostSinkSlot& slot = postSinkSlot();
    std::lock_guard<std::mutex> lock(slot.mutex);
    return slot.sink ? slot.sink(message, wParam, lParam) : false;
}

}  // namespace tinta

#endif  // TINTA_POST_SINK_H
