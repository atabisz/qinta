#ifndef TINTA_POST_SINK_H
#define TINTA_POST_SINK_H

// WHERE postWindowMessage GOES WHEN THERE IS NO HWND.
//
// On the Direct2D stack postWindowMessage was PostMessageW, and the retired main_d2d.cpp's
// window procedure handed WM_APP_IMAGE_READY and WM_APP_FOLDER_SEARCH to completeAsyncImage
// and completeFolderSearch; now every platform posts here, and src/input.cpp's
// handlePostedMessage hands them over. Off the Win32 window stack it returned false, which every owning caller already treats as
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
// ONE ENUM, SO A MESSAGE CANNOT BE HALF-WIRED. The messages were Win32 numbers (WM_APP + n,
// WM_CLOSE, WM_SETTINGCHANGE), and the receiver was two lists that had to agree: an allowlist the
// sink asked first, and the handler's chain of ifs. The Pandoc completion was in the second and
// not the first, so every export after the first was refused silently (2026-10-12). Now the
// sink and src/input.cpp's handlePostedMessage each switch over every enumerator with no
// default, so a new message without a case is a -Wswitch warning, which the Linux job's
// -Werror makes a failed build. The sink takes every message; a false is "no window".
//
// The payload is two integers, as WPARAM and LPARAM were: `value` a number (the Pandoc
// outcome), `pointer` an address. A true hands ownership of what `pointer` points at to the
// receiver, so the owning posters keep their `delete` for a false. This header needs neither
// windows.h nor win32_types.h.

#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>

namespace tinta {

enum class PostedMessage {
    ImageReady,     // render.cpp's image worker; pointer owns the result
    FolderSearch,   // search.cpp's scan; pointer owns the result
    LayoutChunk,    // the next slice of a document layout, posted from the window's thread
    PandocDone,     // pandoc.cpp's worker; value is 1 for success, 0 for failure
    Close,          // close the window once the sender has returned
    SettingChange,  // pointer is a wide string naming the area, e.g. L"ImmersiveColorSet"
};

using PostSink = std::function<bool(PostedMessage message, std::uintptr_t value,
                                    std::intptr_t pointer)>;

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

// What postWindowMessage calls, and its only caller. Any thread.
inline bool deliverPostedMessage(PostedMessage message, std::uintptr_t value,
                                 std::intptr_t pointer) {
    PostSinkSlot& slot = postSinkSlot();
    std::lock_guard<std::mutex> lock(slot.mutex);
    return slot.sink ? slot.sink(message, value, pointer) : false;
}

}  // namespace tinta

#endif  // TINTA_POST_SINK_H
