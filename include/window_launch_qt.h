#ifndef TINTA_WINDOW_LAUNCH_QT_H
#define TINTA_WINDOW_LAUNCH_QT_H

#include <functional>
#include <string>
#include <vector>

// A TEST HOOK FOR THE QT SELF-RELAUNCHES (src/window_launch_qt.cpp), and why it has to exist:
// the relaunch starts QCoreApplication::applicationFilePath(), which inside a test is the TEST
// binary. A test that dropped a tab or pressed Ctrl+Shift+N without this would start the whole
// suite again, with arguments it does not understand. With a hook set, a relaunch hands its
// argument list to the hook and starts nothing; an empty hook restores the real launch.
namespace tinta {

void setRelaunchHookForTests(std::function<void(const std::vector<std::string>&)> hook);

}  // namespace tinta

#endif  // TINTA_WINDOW_LAUNCH_QT_H
